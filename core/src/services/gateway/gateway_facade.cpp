/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/gateway/gateway.hpp"

#include "core/multipart_send_txn.hpp"
#include "core/pipe.hpp"
#include "services/common/advertise_endpoint.hpp"
#include "services/common/socket_monitor_bridge.hpp"
#include "services/control/service_control_runtime.hpp"
#include "services/discovery/discovery_protocol.hpp"
#include "services/gateway/gateway_access.hpp"
#include "services/gateway/gateway_runtime.hpp"
#include "services/gateway/routing_id_utils.hpp"

#include <cerrno>
#include <cstring>

namespace zlink
{
namespace
{
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

static void ensure_gateway_routing_id (socket_base_t *socket_,
                                       const std::string *override_id_)
{
    if (!socket_)
        return;
    if (override_id_ && !override_id_->empty ()) {
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

int gateway_t::connect (const char *endpoint_,
                        const zlink_routing_id_t *routing_id_)
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return -1;

    if (!endpoint_ || endpoint_[0] == '\0' || !routing_id_
        || routing_id_->size == 0) {
        errno = EINVAL;
        return -1;
    }

    bool changed = false;
    {
        scoped_lock_t lock (_sync);
        if (ensure_facade_mode () != 0)
            return -1;
        if (_discovery) {
            errno = EBUSY;
            return -1;
        }
        lock_routing_id ();
        gateway_manual_route_t route;
        memset (&route, 0, sizeof (route));
        route.routing_id = *routing_id_;
        route.weight = 1;

        std::map<std::string, gateway_manual_route_t>::iterator it =
          _runtime->manual_routes.find (endpoint_);
        if (it != _runtime->manual_routes.end ()) {
            if (routing_id_equals (it->second.routing_id, *routing_id_))
                return 0;
            errno = EBUSY;
            return -1;
        }

        _runtime->manual_routes[endpoint_] = route;
        _summary_last_changed_ms = _runtime->clock.now_ms ();
        gateway_service_pool_t *pool = get_or_create_pool_cached ();
        if (!pool)
            return -1;
        pool->dirty = true;
        _runtime->pending_updates.insert (_service_name);
        if (ensure_refresh_task_running () != 0)
            return -1;
        changed = true;
    }

    if (changed) {
        service_control_runtime_t *runtime = _ctx->service_control_runtime ();
        if (runtime && _runtime->refresh_task_id != 0)
            runtime->wakeup_task (_runtime->refresh_task_id);
    }
    return 0;
}

int gateway_t::disconnect (const char *endpoint_)
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return -1;

    if (!endpoint_ || endpoint_[0] == '\0') {
        errno = EINVAL;
        return -1;
    }

    bool changed = false;
    {
        scoped_lock_t lock (_sync);
        if (ensure_facade_mode () != 0)
            return -1;
        if (_discovery) {
            errno = EBUSY;
            return -1;
        }

        std::map<std::string, gateway_manual_route_t>::iterator it =
          _runtime->manual_routes.find (endpoint_);
        if (it == _runtime->manual_routes.end ())
            return 0;

        _runtime->manual_routes.erase (it);
        _summary_last_changed_ms = _runtime->clock.now_ms ();
        gateway_service_pool_t *pool = get_or_create_pool_cached ();
        if (!pool)
            return -1;
        pool->dirty = true;
        _runtime->pending_updates.insert (_service_name);
        if (ensure_refresh_task_running () != 0)
            return -1;
        changed = true;
    }

    if (changed) {
        service_control_runtime_t *runtime = _ctx->service_control_runtime ();
        if (runtime && _runtime->refresh_task_id != 0)
            runtime->wakeup_task (_runtime->refresh_task_id);
    }
    return 0;
}

int gateway_t::init_router_socket ()
{
    if (_runtime->router_socket)
        return 0;
    if (allocate_router (_ctx, &_runtime->router_socket) != 0)
        return -1;
    _runtime->lifecycle.register_socket (_runtime->router_socket);
    ensure_gateway_routing_id (_runtime->router_socket, &_routing_id_override);
    if (_routing_id.size == 0) {
        size_t size = sizeof (_routing_id.data);
        if (_runtime->router_socket->getsockopt (ZLINK_INTERNAL_OPT_ROUTING_ID,
                                                 _routing_id.data, &size)
            == 0)
            _routing_id.size = static_cast<uint8_t> (size);
    }
    if (!_tls_server_cert.empty ()) {
        if (_runtime->router_socket->setsockopt (ZLINK_INTERNAL_OPT_TLS_CERT,
                                                 _tls_server_cert.data (),
                                                 _tls_server_cert.size ())
              != 0
            || _runtime->router_socket->setsockopt (ZLINK_INTERNAL_OPT_TLS_KEY,
                                                    _tls_server_key.data (),
                                                    _tls_server_key.size ())
                 != 0) {
            rollback_gateway_runtime_socket_init (_runtime);
            return -1;
        }
    }
    if (!_runtime->monitor_socket) {
        _runtime->monitor_socket =
          open_socket_monitor_bridge (_runtime->router_socket, monitor_event_mask ());
        _runtime->lifecycle.register_socket (
          static_cast<socket_base_t *> (_runtime->monitor_socket));
    }
    if (apply_tls_client (_runtime->router_socket, _tls_ca, _tls_hostname,
                          _tls_trust_system)
        != 0) {
        rollback_gateway_runtime_socket_init (_runtime);
        return -1;
    }
    int mandatory = 1;
    _runtime->router_socket->setsockopt (ZLINK_INTERNAL_OPT_ROUTER_MANDATORY,
                                         &mandatory, sizeof (mandatory));
    int linger = 0;
    _runtime->router_socket->setsockopt (ZLINK_INTERNAL_OPT_LINGER, &linger,
                                         sizeof (linger));
    int handover = 1;
    _runtime->router_socket->setsockopt (ZLINK_INTERNAL_OPT_ROUTER_HANDOVER,
                                         &handover, sizeof (handover));
    if (_handler.load (std::memory_order_acquire) != NULL) {
        if (_runtime->router_socket->socket_set_msg_handler_ex (
              &gateway_router_msg_handler, this)
            != 0) {
            rollback_gateway_runtime_socket_init (_runtime);
            return -1;
        }
    }
    if (_send_ready_handler.load (std::memory_order_acquire) != NULL) {
        if (_runtime->router_socket->socket_set_send_ready_handler_ex (
              &gateway_send_ready_handler, this)
            != 0) {
            rollback_gateway_runtime_socket_init (_runtime);
            return -1;
        }
    }
    return 0;
}

int gateway_t::ensure_router_socket ()
{
    return _runtime->router_socket ? 0 : -1;
}

int gateway_t::send (zlink_msg_t *parts_, size_t part_count_, int flags_)
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return -1;

    if (!parts_ || part_count_ == 0) {
        errno = EINVAL;
        return -1;
    }
    if (flags_ != 0 && flags_ != ZLINK_DONTWAIT) {
        errno = ENOTSUP;
        return -1;
    }

    if (ensure_facade_mode () != 0)
        return -1;

    gateway_service_pool_t *pool = _runtime->primary_pool;
    if (!pool) {
        scoped_lock_t lock (_sync);
        if (ensure_facade_mode () != 0)
            return -1;
        pool = get_or_create_pool_cached ();
        if (!pool) {
            errno = ENOMEM;
            return -1;
        }
    }

    scoped_lock_t send_lock (_send_sync);
    const gateway_service_pool_t::send_snapshot_t *snapshot =
      pool->send_snapshot.get ();
    if (!snapshot || snapshot->routing_ids.empty () || !snapshot->router_socket) {
        errno = EHOSTUNREACH;
        return -1;
    }

    zlink_routing_id_t rid;
    memset (&rid, 0, sizeof (rid));
    size_t provider_index = 0;
    if (snapshot->routing_ids.size () > 1
        && !select_provider (pool, snapshot, &provider_index)) {
        errno = EHOSTUNREACH;
        return -1;
    }
    if (provider_index >= snapshot->routing_ids.size ()) {
        errno = ENOTSUP;
        return -1;
    }

    rid = snapshot->routing_ids[provider_index];
    socket_base_t *router_socket = snapshot->router_socket;

    return zlink::logical_multipart_send_prefixed (router_socket, rid.data,
                                                   rid.size, parts_, part_count_,
                                                   flags_, 1000);
}

std::string gateway_t::resolve_advertise (const char *advertise_endpoint_) const
{
    return services::normalize_advertise_endpoint (advertise_endpoint_,
                                                   _bind_endpoint);
}

int gateway_t::send_rid (const zlink_routing_id_t *routing_id_,
                         zlink_msg_t *parts_,
                         size_t part_count_,
                         int flags_)
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return -1;

    if (!routing_id_ || !parts_ || part_count_ == 0) {
        errno = EINVAL;
        return -1;
    }
    if (flags_ != 0 && flags_ != ZLINK_DONTWAIT) {
        errno = ENOTSUP;
        return -1;
    }

    if (_runtime->stop.get () == 0 && _runtime->router_socket
        == socket_base_t::current_socket_msg_dispatch_socket ()) {
        pipe_t *dispatch_pipe =
          socket_base_t::current_socket_msg_dispatch_pipe ();
        zlink_routing_id_t dispatch_source_rid;
        if (dispatch_pipe
            && socket_base_t::current_socket_msg_dispatch_source_rid (
              &dispatch_source_rid)
            && routing_id_equals (dispatch_source_rid, *routing_id_))
            return send_parts_via_dispatch_pipe (dispatch_pipe, parts_,
                                                 part_count_);
        if (pipe_routing_id_equals (dispatch_pipe, routing_id_))
            return send_parts_via_dispatch_pipe (dispatch_pipe, parts_,
                                                 part_count_);
    }

    if (ensure_facade_mode () != 0)
        return -1;
    if (routing_id_->size == 0) {
        errno = EINVAL;
        return -1;
    }

    socket_base_t *router_socket = _runtime->router_socket;
    if (router_socket == socket_base_t::current_socket_msg_dispatch_socket ()) {
        pipe_t *dispatch_pipe =
          socket_base_t::current_socket_msg_dispatch_pipe ();
        zlink_routing_id_t dispatch_source_rid;
        if (dispatch_pipe
            && socket_base_t::current_socket_msg_dispatch_source_rid (
              &dispatch_source_rid)
            && routing_id_equals (dispatch_source_rid, *routing_id_))
            return send_parts_via_dispatch_pipe (dispatch_pipe, parts_,
                                                 part_count_);
        if (pipe_routing_id_equals (dispatch_pipe, routing_id_))
            return send_parts_via_dispatch_pipe (dispatch_pipe, parts_,
                                                 part_count_);
    }
    if (!router_socket) {
        errno = ENOTSUP;
        return -1;
    }

    scoped_lock_t send_lock (_send_sync);

    return zlink::logical_multipart_send_prefixed (
      router_socket, routing_id_->data, routing_id_->size, parts_, part_count_,
      flags_);
}

int gateway_t::set_lb_strategy (int strategy_)
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return -1;

    if (_service_name.empty ()) {
        errno = EINVAL;
        return -1;
    }
    if (strategy_ != ZLINK_GATEWAY_LB_STRATEGY_ROUND_ROBIN
        && strategy_ != ZLINK_GATEWAY_LB_STRATEGY_WEIGHTED) {
        errno = EINVAL;
        return -1;
    }

    scoped_lock_t lock (_sync);
    if (ensure_facade_mode () != 0)
        return -1;
    gateway_service_pool_t *pool = get_or_create_pool (_service_name);
    if (!pool)
        return -1;
    pool->lb_strategy = strategy_;
    return 0;
}

int gateway_t::set_routing_id (const void *data_, size_t size_)
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return -1;

    if (!data_ || size_ == 0 || size_ > sizeof (_routing_id.data)) {
        errno = EINVAL;
        return -1;
    }

    scoped_lock_t lock (_sync);
    if (!_runtime->pools.empty () || _routing_id_locked) {
        errno = EFSM;
        return -1;
    }

    _routing_id_override.assign (static_cast<const char *> (data_), size_);
    memcpy (_routing_id.data, data_, size_);
    _routing_id.size = static_cast<uint8_t> (size_);
    if (_runtime->router_socket
        && _runtime->router_socket->setsockopt (ZLINK_INTERNAL_OPT_ROUTING_ID,
                                                data_, size_)
             != 0)
        return -1;
    return 0;
}

int gateway_t::routing_id (zlink_routing_id_t *out_)
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return -1;

    if (!out_) {
        errno = EINVAL;
        return -1;
    }

    scoped_lock_t lock (_sync);
    if (_routing_id.size == 0) {
        if (ensure_router_socket () != 0)
            return -1;
        size_t size = sizeof (_routing_id.data);
        if (_runtime->router_socket->getsockopt (ZLINK_INTERNAL_OPT_ROUTING_ID,
                                                 _routing_id.data, &size)
            != 0)
            return -1;
        _routing_id.size = static_cast<uint8_t> (size);
    }
    *out_ = _routing_id;
    return 0;
}

int gateway_t::last_endpoint (char *endpoint_out_, size_t *size_out_) const
{
    service_public_api_scope_t admission (
      const_cast<service_public_api_guard_t &> (_public_api));
    if (!admission.acquired ())
        return -1;

    if (!endpoint_out_ || !size_out_) {
        errno = EINVAL;
        return -1;
    }

    scoped_lock_t lock (const_cast<mutex_t &> (_sync));
    if (!_runtime->router_socket) {
        errno = ENOTSUP;
        return -1;
    }
    if (_runtime->router_socket->getsockopt (ZLINK_INTERNAL_OPT_LAST_ENDPOINT,
                                             endpoint_out_, size_out_)
        == 0) {
        return 0;
    }

    if (_bind_endpoint.empty ())
        return -1;

    const size_t required = _bind_endpoint.size () + 1;
    if (*size_out_ < required) {
        *size_out_ = required;
        errno = EINVAL;
        return -1;
    }

    memcpy (endpoint_out_, _bind_endpoint.c_str (), required);
    *size_out_ = required;
    return 0;
}

int gateway_t::update_peer_weight (const zlink_routing_id_t *routing_id_,
                                   uint32_t weight_)
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return -1;

    if (!routing_id_ || routing_id_->size == 0) {
        errno = EINVAL;
        return -1;
    }
    if (_service_name.empty ()) {
        errno = EINVAL;
        return -1;
    }

    scoped_lock_t lock (_sync);
    if (ensure_facade_mode () != 0)
        return -1;
    if (!_discovery) {
        errno = ENOTSUP;
        return -1;
    }

    std::vector<provider_info_t> providers;
    _discovery->snapshot_providers (_service_name, &providers);

    std::string endpoint;
    bool found = false;
    for (size_t i = 0; i < providers.size (); ++i) {
        if (routing_id_equals (providers[i].routing_id, *routing_id_)) {
            endpoint = providers[i].endpoint;
            found = true;
            break;
        }
    }

    if (!found) {
        errno = ENOENT;
        return -1;
    }

    const uint32_t value = weight_;
    if (_discovery->update_service_weight (
          discovery_protocol::service_type_gateway_receiver, _service_name.c_str (),
          endpoint.c_str (), value)
        != 0)
        return -1;

    gateway_service_pool_t *pool = get_or_create_pool (_service_name);
    if (pool) {
        scoped_lock_t send_lock (_send_sync);
        std::shared_ptr<const gateway_service_pool_t::send_snapshot_t> published =
          pool->send_snapshot;
        if (published) {
            std::shared_ptr<gateway_service_pool_t::send_snapshot_t> updated (
              new gateway_service_pool_t::send_snapshot_t (*published));
            for (size_t i = 0; i < updated->routing_ids.size ()
                               && i < updated->weights.size ();
                 ++i) {
                if (routing_id_equals (updated->routing_ids[i], *routing_id_)) {
                    updated->weights[i] = value;
                    break;
                }
            }
            updated->router_socket = _runtime->router_socket;
            pool->send_snapshot =
              std::shared_ptr<const gateway_service_pool_t::send_snapshot_t> (
                updated);
        }
        if (pool->control_snapshot) {
            for (std::map<std::string,
                          gateway_service_pool_t::control_route_t>::iterator it =
                   pool->control_snapshot->routes_by_endpoint.begin ();
                 it != pool->control_snapshot->routes_by_endpoint.end ();
                 ++it) {
                if (routing_id_equals (it->second.routing_id, *routing_id_)) {
                    it->second.weight = value;
                    break;
                }
            }
        }
    }

    const std::string peer_key = gateway_peer_key (_service_name, *routing_id_);
    std::map<std::string, gateway_runtime_t::gateway_peer_report_t>::iterator it =
      _runtime->ready_peer_reports.find (peer_key);
    if (it != _runtime->ready_peer_reports.end ()) {
        it->second.weight = value;
        report_gateway_peer (it->second.service_name, it->second.peer_endpoint,
                             it->second.peer_routing_id, it->second.weight,
                             ZLINK_TOPOLOGY_STATE_READY,
                             it->second.connected_since_ms);
        _runtime->next_gateway_peer_report_ms = 0;
    }

    if (_routing_id.size > 0 && routing_id_equals (_routing_id, *routing_id_))
        _server_weight = value;

    return 0;
}

int gateway_t::set_socket_option (int option_,
                                  const void *optval_,
                                  size_t optvallen_)
{
    if (!optval_ || optvallen_ == 0) {
        errno = EINVAL;
        return -1;
    }

    scoped_lock_t lock (_sync);
    if (ensure_facade_mode () != 0)
        return -1;
    if (!_runtime->router_socket) {
        errno = ENOTSUP;
        return -1;
    }
    return _runtime->router_socket->setsockopt (option_, optval_, optvallen_);
}

int gateway_t::get_socket_option (int option_,
                                  void *optval_,
                                  size_t *optvallen_)
{
    if (!optval_ || !optvallen_) {
        errno = EINVAL;
        return -1;
    }

    scoped_lock_t lock (_sync);
    if (ensure_facade_mode () != 0)
        return -1;
    if (!_runtime->router_socket) {
        errno = ENOTSUP;
        return -1;
    }
    return _runtime->router_socket->getsockopt (option_, optval_, optvallen_);
}

void *gateway_t::router ()
{
    scoped_lock_t lock (_sync);
    lock_routing_id ();
    if (ensure_router_socket () != 0)
        return NULL;
    return static_cast<void *> (_runtime->router_socket);
}

bool gateway_t::enter_pollable_mode ()
{
    _pollable_mode = true;
    return true;
}

void gateway_t::lock_routing_id ()
{
    _routing_id_locked = true;
}

int gateway_t::ensure_facade_mode () const
{
    return 0;
}

int gateway_t::set_handler (zlink_socket_msg_handler_fn handler_,
                            void *userdata_)
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return -1;

    if (!handler_) {
        errno = EINVAL;
        return -1;
    }

    scoped_lock_t lock (_sync);
    if (ensure_facade_mode () != 0)
        return -1;
    _handler_userdata.store (userdata_, std::memory_order_release);
    _handler.store (handler_, std::memory_order_release);
    if (_runtime->router_socket) {
        if (_runtime->router_socket->socket_set_msg_handler_ex (
              &gateway_router_msg_handler, this)
            != 0) {
            return -1;
        }
    }
    return 0;
}

int gateway_t::set_send_ready_handler (zlink_send_ready_handler_fn handler_,
                                       void *userdata_)
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return -1;

    if (!handler_) {
        errno = EINVAL;
        return -1;
    }

    if (_runtime->router_socket) {
        if (_runtime->router_socket->socket_set_send_ready_handler_ex (
              &gateway_send_ready_handler, this)
            != 0) {
            return -1;
        }
    }
    _send_ready_handler_userdata.store (userdata_, std::memory_order_release);
    _send_ready_handler.store (handler_, std::memory_order_release);
    return 0;
}

int gateway_t::set_tls_client (const char *ca_cert_,
                               const char *hostname_,
                               int trust_system_)
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return -1;

    if (!ca_cert_ || !hostname_) {
        errno = EINVAL;
        return -1;
    }

    scoped_lock_t lock (_sync);
    if (ensure_facade_mode () != 0)
        return -1;
    _tls_ca.assign (ca_cert_);
    _tls_hostname.assign (hostname_);
    _tls_trust_system = trust_system_;

    if (ensure_router_socket () != 0)
        return -1;
    if (_runtime->router_socket
        && apply_tls_client (_runtime->router_socket, _tls_ca, _tls_hostname,
                             _tls_trust_system)
             != 0)
        return -1;
    return 0;
}

void gateway_t::dispatch_message (const zlink_routing_id_t *source_rid_,
                                  zlink_msg_t *parts_,
                                  size_t part_count_)
{
    zlink_socket_msg_handler_fn handler =
      _handler.load (std::memory_order_acquire);
    if (!handler) {
        for (size_t i = 0; i < part_count_; ++i)
            zlink_msg_close (&parts_[i]);
        return;
    }

    handler (source_rid_, parts_, part_count_,
             _handler_userdata.load (std::memory_order_acquire));
}

void gateway_t::dispatch_send_ready ()
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return;
    zlink_send_ready_handler_fn handler =
      _send_ready_handler.load (std::memory_order_acquire);
    if (handler)
        handler (this,
                 _send_ready_handler_userdata.load (std::memory_order_acquire));
}
}
