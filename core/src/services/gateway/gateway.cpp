/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/gateway/gateway.hpp"

#include "core/msg.hpp"
#include "core/pipe.hpp"
#include "core/recv_internal.hpp"
#include "core/send_internal.hpp"
#include "core/multipart_send_txn.hpp"
#include "services/common/advertise_endpoint.hpp"
#include "services/common/monitor_decode.hpp"
#include "services/common/socket_monitor_bridge.hpp"
#include "services/gateway/gateway_access.hpp"
#include "services/gateway/gateway_runtime.hpp"
#include "services/gateway/routing_id_utils.hpp"
#include "services/control/service_control_runtime.hpp"
#include "services/discovery/discovery_protocol.hpp"
#include "utils/sleep.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <cstdio>

#include <zlink.h>

namespace zlink
{
namespace
{
static const uint32_t gateway_tag_value = 0x1e6700d7;

static void gateway_router_msg_handler (const zlink_routing_id_t *source_rid_,
                                        zlink_msg_t *parts_,
                                        size_t part_count_,
                                        void *)
{
    gateway_t *gateway = static_cast<gateway_t *> (
      socket_base_t::current_socket_msg_dispatch_subject ());
    if (!gateway) {
        for (size_t i = 0; i < part_count_; ++i)
            zlink_msg_close (&parts_[i]);
        return;
    }

    gateway_access_t::dispatch_message (gateway, source_rid_, parts_, part_count_);
}

static void gateway_send_ready_handler (void *subject_, void *)
{
    gateway_t *gateway = static_cast<gateway_t *> (subject_);
    if (!gateway)
        return;
    gateway_access_t::dispatch_send_ready (gateway);
}

// Ensure the ROUTER socket has a routing id so peers can reply.
static void ensure_gateway_routing_id (socket_base_t *socket_,
                                       const std::string *override_id_)
{
    if (!socket_)
        return;
    if (override_id_ && !override_id_->empty ()) {
        // Explicit routing id must take precedence over any auto-generated id.
        zlink::discovery::set_socket_routing_id (socket_, override_id_, NULL);
        return;
    }
    unsigned char buf[256];
    size_t size = sizeof (buf);
    if (socket_->getsockopt (ZLINK_INTERNAL_OPT_ROUTING_ID, buf, &size) != 0)
        return;
    if (size > 0)
        return;
    zlink::discovery::set_socket_routing_id (socket_, override_id_, NULL);
}

static int allocate_router (ctx_t *ctx_, socket_base_t **socket_)
{
    *socket_ = ctx_->create_socket (ZLINK_CORE_SOCKET_ROUTER);
    if (!*socket_)
        return -1;
    return 0;
}

// Apply TLS client settings to the ROUTER socket (optional).
static int apply_tls_client (socket_base_t *socket_,
                             const std::string &ca_cert_,
                             const std::string &hostname_,
                             int trust_system_)
{
    if (!socket_)
        return -1;
    if (ca_cert_.empty () || hostname_.empty ())
        return 0;
    if (socket_->setsockopt (ZLINK_INTERNAL_OPT_TLS_CA, ca_cert_.data (),
                             ca_cert_.size ())
        != 0)
        return -1;
    if (socket_->setsockopt (ZLINK_INTERNAL_OPT_TLS_HOSTNAME, hostname_.data (),
                             hostname_.size ())
        != 0)
        return -1;
    if (socket_->setsockopt (ZLINK_INTERNAL_OPT_TLS_TRUST_SYSTEM, &trust_system_,
                             sizeof (trust_system_))
        != 0)
        return -1;
    return 0;
}

static int monitor_event_mask ()
{
    return ZLINK_EVENT_CONNECTION_READY_CHANGED | ZLINK_EVENT_DISCONNECTED
           | ZLINK_EVENT_HANDSHAKE_FAILED_NO_DETAIL
           | ZLINK_EVENT_HANDSHAKE_FAILED_PROTOCOL
           | ZLINK_EVENT_HANDSHAKE_FAILED_AUTH;
}

static int resolve_gateway_refresh_sleep_ms ()
{
    static int cached = -1;
    if (cached >= 0)
        return cached;

    int value = 1;
    const char *env = getenv ("ZLINK_GATEWAY_REFRESH_SLEEP_MS");
    if (env && *env) {
        char *end = NULL;
        const long parsed = strtol (env, &end, 10);
        if (end != env && parsed >= 0 && parsed <= 1000)
            value = static_cast<int> (parsed);
    }
    cached = value;
    return cached;
}

static std::string routing_id_key (const zlink_routing_id_t &rid_)
{
    if (rid_.size == 0)
        return std::string ();
    return std::string (reinterpret_cast<const char *> (rid_.data), rid_.size);
}

static bool routing_id_equals (const zlink_routing_id_t &a_,
                               const zlink_routing_id_t &b_)
{
    if (a_.size != b_.size || a_.size == 0)
        return false;
    return memcmp (a_.data, b_.data, a_.size) == 0;
}

static bool pipe_routing_id_equals (const pipe_t *pipe_,
                                    const zlink_routing_id_t *rid_)
{
    if (!pipe_ || !rid_)
        return false;

    const pipe_t *routing_pipe = pipe_;
    if (routing_pipe->get_peer ())
        routing_pipe = routing_pipe->get_peer ();

    const blob_t &routing_id = routing_pipe->get_routing_id ();
    if (routing_id.size () != rid_->size)
        return false;
    if (routing_id.size () == 0)
        return false;
    return memcmp (routing_id.data (), rid_->data, rid_->size) == 0;
}

static void rollback_gateway_runtime_socket_init (gateway_runtime_t *runtime_)
{
    if (!runtime_)
        return;

    if (runtime_->monitor_socket) {
        socket_base_t *monitor_socket =
          static_cast<socket_base_t *> (runtime_->monitor_socket);
        close_socket_monitor_bridge (runtime_->router_socket, monitor_socket);
        (void) runtime_->lifecycle.close_socket_and_wait (monitor_socket, 2000);
        runtime_->monitor_socket = NULL;
    }
    if (runtime_->router_socket)
        (void) runtime_->lifecycle.close_socket_and_wait (runtime_->router_socket,
                                                         2000);
    (void) runtime_->lifecycle.wait_drained (2000);
}

static int send_parts_via_dispatch_pipe (pipe_t *pipe_,
                                         zlink_msg_t *parts_,
                                         size_t part_count_)
{
    if (!pipe_ || !parts_ || part_count_ == 0) {
        errno = EINVAL;
        return -1;
    }

    pipe_t *out = pipe_->get_peer ();
    if (!out) {
        errno = EHOSTUNREACH;
        return -1;
    }

    for (size_t i = 0; i < part_count_; ++i) {
        msg_t *part = reinterpret_cast<msg_t *> (&parts_[i]);
        const bool has_more = i + 1 < part_count_;
        if (has_more)
            part->set_flags (msg_t::more);
        else
            part->reset_flags (msg_t::more);
        if (!out->write_no_hwm_check (part)) {
            if (i > 0)
                out->rollback ();
            errno = EAGAIN;
            return -1;
        }
        const int init_rc = part->init ();
        errno_assert (init_rc == 0);
    }

    out->flush ();
    return 0;
}

static uint64_t rid_handover_guard_ms ()
{
    return 50;
}

static std::string gateway_peer_key (const std::string &service_name_,
                                     const zlink_routing_id_t &rid_)
{
    if (service_name_.empty () || rid_.size == 0)
        return std::string ();

    std::string key = service_name_;
    key.push_back ('\0');
    key.append (reinterpret_cast<const char *> (rid_.data), rid_.size);
    return key;
}

}

gateway_service_pool_t::gateway_service_pool_t () :
    send_snapshot (std::shared_ptr<const send_snapshot_t> (new send_snapshot_t)),
    control_snapshot (std::shared_ptr<control_snapshot_t> (
      new control_snapshot_t)),
    rr_cursor (0),
    lb_strategy (ZLINK_GATEWAY_LB_STRATEGY_ROUND_ROBIN),
    dirty (true)
{
}

gateway_runtime_t::gateway_runtime_t (gateway_t *owner_) :
    owner (owner_),
    lifecycle (owner_ ? owner_->_ctx : NULL),
    monitor_socket (NULL),
    router_socket (NULL),
    stop (0),
    refresh_task_id (0),
    primary_pool (NULL),
    next_gateway_peer_report_ms (0),
    force_refresh_all (false)
{
}

gateway_t::gateway_t (ctx_t *ctx_,
                      const char *service_name_,
                      const char *routing_id_) :
    _ctx (ctx_),
    _discovery (NULL),
    _tag (gateway_tag_value),
    _runtime (NULL),
    _use_lock (true),
    _pollable_mode (false),
    _routing_id_locked (false),
    _service_ready_emitted (false),
    _refresh_interval_ms (
      static_cast<uint32_t> (resolve_gateway_refresh_sleep_ms ())),
    _server_weight (0),
    _last_summary_error (0),
    _summary_last_changed_ms (0),
    _tls_trust_system (0),
    _service_name (service_name_ ? service_name_ : ""),
    _routing_id_override (routing_id_ ? routing_id_ : ""),
    _handler (NULL),
    _handler_userdata (NULL),
    _send_ready_handler (NULL),
    _send_ready_handler_userdata (NULL),
    _monitor (ctx_)
{
    zlink_assert (_ctx);
    _runtime = new (std::nothrow) gateway_runtime_t (this);
    if (!_runtime) {
        errno = ENOMEM;
        _tag = 0xdeadbeef;
        return;
    }
    _routing_id.size = 0;
    if (_service_name.empty ()) {
        _tag = 0xdeadbeef;
        return;
    }
    if (init_router_socket () != 0)
        _tag = 0xdeadbeef;
    if (_tag != gateway_tag_value) {
        _tag = 0xdeadbeef;
    }
}

gateway_t::~gateway_t ()
{
    _tag = 0xdeadbeef;
    delete _runtime;
    _runtime = NULL;
}

bool gateway_t::check_tag () const
{
    return _tag == gateway_tag_value;
}

void gateway_t::refresh_task (void *arg_)
{
    gateway_t *self = static_cast<gateway_t *> (arg_);
    self->refresh_tick ();
}

void gateway_t::refresh_tick ()
{
    if (_runtime->stop.get () != 0)
        return;

    const uint64_t refresh_task_id = _runtime->refresh_task_id;
    std::vector<std::string> services_to_refresh;
    uint64_t now_ms = 0;
    {
        scoped_lock_t lock (_sync);
        process_monitor_events ();
        now_ms = _runtime->clock.now_ms ();
        for (std::map<std::string, uint64_t>::iterator it =
               _runtime->down_until_ms.begin ();
             it != _runtime->down_until_ms.end ();) {
            if (now_ms >= it->second) {
                _runtime->down_endpoints.erase (it->first);
                it = _runtime->down_until_ms.erase (it);
                _runtime->force_refresh_all = true;
            } else {
                ++it;
            }
        }
        if (_runtime->force_refresh_all) {
            for (std::map<std::string, gateway_service_pool_t>::iterator it =
                   _runtime->pools.begin ();
                 it != _runtime->pools.end (); ++it) {
                it->second.dirty = true;
                services_to_refresh.push_back (it->first);
            }
        } else {
            for (std::set<std::string>::iterator sit =
                   _runtime->pending_updates.begin ();
                 sit != _runtime->pending_updates.end (); ++sit) {
                std::map<std::string, gateway_service_pool_t>::iterator pit =
                  _runtime->pools.find (*sit);
                if (pit != _runtime->pools.end ()) {
                    pit->second.dirty = true;
                    services_to_refresh.push_back (*sit);
                }
            }
        }
        _runtime->pending_updates.clear ();
        _runtime->force_refresh_all = false;
    }
    if (!services_to_refresh.empty ()) {
        for (size_t i = 0; i < services_to_refresh.size (); ++i) {
            const std::string &service = services_to_refresh[i];
            std::vector<provider_info_t> providers;
            uint64_t seq = 0;
            if (_discovery) {
                _discovery->snapshot_providers (service, &providers);
                seq = _discovery->service_update_seq (service);
            }
            scoped_lock_t lock (_sync);
            std::map<std::string, gateway_service_pool_t>::iterator it =
              _runtime->pools.find (service);
            if (it == _runtime->pools.end ())
                continue;
            if (!it->second.dirty)
                continue;
            refresh_pool (&it->second, providers, seq);
        }
    }

    bool stop_refresh_task = false;
    {
        scoped_lock_t lock (_sync);
        sync_gateway_peer_reports (now_ms);
        stop_refresh_task =
          refresh_task_id != 0 && _runtime->refresh_task_id == refresh_task_id
          && can_suspend_refresh_task ();
        if (stop_refresh_task)
            _runtime->refresh_task_id = 0;
    }

    if (stop_refresh_task) {
        service_control_runtime_t *runtime = _ctx->service_control_runtime ();
        if (runtime)
            (void) runtime->remove_task (refresh_task_id);
    }
}

int gateway_t::ensure_refresh_task_running ()
{
    if (!_runtime) {
        errno = EFAULT;
        return -1;
    }
    if (_runtime->refresh_task_id != 0)
        return 0;

    service_control_runtime_t *runtime = _ctx->service_control_runtime ();
    if (!runtime) {
        errno = ETERM;
        return -1;
    }

    _runtime->refresh_task_id =
      runtime->add_periodic_task (refresh_task, this, _refresh_interval_ms,
                                  true);
    if (_runtime->refresh_task_id == 0)
        return -1;
    return 0;
}

bool gateway_t::can_suspend_refresh_task () const
{
    if (_discovery)
        return false;
    if (_monitor.has_watchers ())
        return false;
    if (_runtime->force_refresh_all || !_runtime->pending_updates.empty ())
        return false;
    if (!_runtime->down_until_ms.empty () || !_runtime->down_endpoints.empty ())
        return false;
    if (!_runtime->inflight_endpoints.empty ())
        return false;
    return true;
}

gateway_service_pool_t *gateway_t::get_or_create_pool (
  const std::string &service_name_)
{
    if (_runtime->primary_pool && service_name_ == _service_name)
        return _runtime->primary_pool;

    std::map<std::string, gateway_service_pool_t>::iterator it =
      _runtime->pools.find (service_name_);
    if (it != _runtime->pools.end ())
        return &it->second;

    gateway_service_pool_t pool;
    pool.service_name = service_name_;
    pool.rr_cursor = 0;
    pool.lb_strategy = ZLINK_GATEWAY_LB_STRATEGY_ROUND_ROBIN;
    pool.dirty = true;

    if (ensure_router_socket () != 0)
        return NULL;
    _runtime->pools.insert (std::make_pair (service_name_, pool));
    if (_discovery) {
        _runtime->pending_updates.insert (service_name_);
        service_control_runtime_t *runtime = _ctx->service_control_runtime ();
        if (runtime && _runtime->refresh_task_id != 0)
            runtime->wakeup_task (_runtime->refresh_task_id);
    }
    gateway_service_pool_t *created = &_runtime->pools.find (service_name_)->second;
    if (service_name_ == _service_name)
        _runtime->primary_pool = created;
    return created;
}

gateway_service_pool_t *gateway_t::get_or_create_pool_cached ()
{
    return _service_name.empty () ? NULL : get_or_create_pool (_service_name);
}

}
