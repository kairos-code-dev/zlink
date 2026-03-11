/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/gateway/gateway.hpp"

#include "core/msg.hpp"
#include "core/pipe.hpp"
#include "core/recv_internal.hpp"
#include "services/common/advertise_endpoint.hpp"
#include "services/common/monitor_decode.hpp"
#include "services/common/socket_monitor_bridge.hpp"
#include "services/gateway/routing_id_utils.hpp"
#include "services/control/service_control_runtime.hpp"
#include "services/discovery/discovery_protocol.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <cstdlib>

#include <zlink.h>

namespace zlink
{
namespace
{
static const uint32_t gateway_tag_value = 0x1e6700d7;

struct gateway_handler_registry_t
{
    mutex_t sync;
    std::map<socket_base_t *, gateway_t *> gateways;
};

static gateway_handler_registry_t &gateway_handler_registry ()
{
    static gateway_handler_registry_t registry;
    return registry;
}

static void register_gateway_handler_socket (socket_base_t *socket_,
                                             gateway_t *gateway_)
{
    if (!socket_ || !gateway_)
        return;

    gateway_handler_registry_t &registry = gateway_handler_registry ();
    scoped_lock_t lock (registry.sync);
    registry.gateways[socket_] = gateway_;
}

static void unregister_gateway_handler_socket (socket_base_t *socket_)
{
    if (!socket_)
        return;

    gateway_handler_registry_t &registry = gateway_handler_registry ();
    scoped_lock_t lock (registry.sync);
    registry.gateways.erase (socket_);
}

static gateway_t *find_gateway_for_dispatch (socket_base_t *socket_)
{
    if (!socket_)
        return NULL;

    gateway_handler_registry_t &registry = gateway_handler_registry ();
    scoped_lock_t lock (registry.sync);
    std::map<socket_base_t *, gateway_t *>::iterator it =
      registry.gateways.find (socket_);
    return it != registry.gateways.end () ? it->second : NULL;
}

static void gateway_router_msg_handler (const zlink_routing_id_t *source_rid_,
                                        zlink_msg_t *parts_,
                                        size_t part_count_)
{
    socket_base_t *socket = socket_base_t::current_socket_msg_dispatch_socket ();
    gateway_t *gateway = find_gateway_for_dispatch (socket);
    if (!gateway) {
        for (size_t i = 0; i < part_count_; ++i)
            zlink_msg_close (&parts_[i]);
        return;
    }

    gateway->dispatch_message (source_rid_, parts_, part_count_);
}

static void gateway_send_ready_handler (void *subject_)
{
    gateway_t *gateway = find_gateway_for_dispatch (
      static_cast<socket_base_t *> (subject_));
    if (!gateway)
        return;
    gateway->dispatch_send_ready ();
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
    if (socket_->getsockopt (ZLINK_ROUTING_ID, buf, &size) != 0)
        return;
    if (size > 0)
        return;
    zlink::discovery::set_socket_routing_id (socket_, override_id_, NULL);
}

static int allocate_router (ctx_t *ctx_, socket_base_t **socket_)
{
    *socket_ = ctx_->create_socket (ZLINK_ROUTER);
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
    if (socket_->setsockopt (ZLINK_TLS_CA, ca_cert_.data (),
                             ca_cert_.size ())
        != 0)
        return -1;
    if (socket_->setsockopt (ZLINK_TLS_HOSTNAME, hostname_.data (),
                             hostname_.size ())
        != 0)
        return -1;
    if (socket_->setsockopt (ZLINK_TLS_TRUST_SYSTEM, &trust_system_,
                             sizeof (trust_system_))
        != 0)
        return -1;
    return 0;
}

static int monitor_event_mask ()
{
    return ZLINK_EVENT_CONNECTION_READY | ZLINK_EVENT_DISCONNECTED
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

static void close_msg_parts (std::vector<zlink_msg_t> *parts_)
{
    if (!parts_)
        return;
    for (size_t i = 0; i < parts_->size (); ++i)
        zlink_msg_close (&(*parts_)[i]);
    parts_->clear ();
}

static uint32_t count_ready_for_service (
  const std::string &service_name_,
  const std::map<std::string, std::string> &endpoint_to_service_,
  const std::set<std::string> &ready_endpoints_)
{
    uint32_t count = 0;
    for (std::set<std::string>::const_iterator it = ready_endpoints_.begin ();
         it != ready_endpoints_.end (); ++it) {
        std::map<std::string, std::string>::const_iterator fit =
          endpoint_to_service_.find (*it);
        if (fit != endpoint_to_service_.end () && fit->second == service_name_)
            ++count;
    }
    return count;
}

}

gateway_t::gateway_t (ctx_t *ctx_,
                      const char *service_name_,
                      const char *routing_id_) :
    _ctx (ctx_),
    _discovery (NULL),
    _tag (gateway_tag_value),
    _last_pool (NULL),
    _force_refresh_all (false),
    _monitor_socket (NULL),
    _router_socket (NULL),
    _use_lock (true),
    _pollable_mode (false),
    _routing_id_locked (false),
    _stop (0),
    _refresh_task_id (0),
    _refresh_interval_ms (
      static_cast<uint32_t> (resolve_gateway_refresh_sleep_ms ())),
    _server_weight (1),
    _tls_trust_system (0),
    _service_name (service_name_ ? service_name_ : ""),
    _routing_id_override (routing_id_ ? routing_id_ : ""),
    _handler (NULL),
    _send_ready_handler (NULL),
    _monitor (ctx_)
{
    zlink_assert (_ctx);
    _routing_id.size = 0;
    if (_service_name.empty ()) {
        _tag = 0xdeadbeef;
        return;
    }
    if (init_router_socket () != 0)
        _tag = 0xdeadbeef;
    service_control_runtime_t *runtime = _ctx->service_control_runtime ();
    if (runtime && _tag == gateway_tag_value) {
        _refresh_task_id =
          runtime->add_periodic_task (refresh_task, this, _refresh_interval_ms,
                                      true);
        if (_refresh_task_id == 0)
            _tag = 0xdeadbeef;
    } else {
        _tag = 0xdeadbeef;
    }
}

gateway_t::~gateway_t ()
{
    _tag = 0xdeadbeef;
}

bool gateway_t::check_tag () const
{
    return _tag == gateway_tag_value;
}

int gateway_t::attach_discovery (discovery_t *discovery_)
{
    if (!discovery_
        || discovery_->service_type ()
             != discovery_protocol::service_type_gateway_receiver) {
        errno = EINVAL;
        return -1;
    }

    bool should_register = false;
    std::string bind_endpoint;
    uint32_t server_weight = 0;
    {
        scoped_optional_lock_t lock (_use_lock ? &_sync : NULL);
        if (ensure_facade_mode () != 0)
            return -1;
        if (_discovery == discovery_)
            return 0;
        if (_discovery) {
            errno = EBUSY;
            return -1;
        }

        _discovery = discovery_;
        _discovery->add_observer (this);
        _force_refresh_all = true;
        if (!_service_name.empty ())
            _pending_updates.insert (_service_name);
        if (!_bind_endpoint.empty ()) {
            should_register = true;
            bind_endpoint = _bind_endpoint;
            server_weight = _server_weight;
        }
    }

    if (should_register && register_service (bind_endpoint.c_str (), server_weight) != 0) {
        scoped_optional_lock_t lock (_use_lock ? &_sync : NULL);
        if (_discovery == discovery_) {
            _discovery->remove_observer (this);
            _discovery = NULL;
        }
        return -1;
    }

    service_control_runtime_t *runtime = _ctx->service_control_runtime ();
    if (runtime && _refresh_task_id != 0)
        runtime->wakeup_task (_refresh_task_id);
    return 0;
}

void gateway_t::refresh_task (void *arg_)
{
    gateway_t *self = static_cast<gateway_t *> (arg_);
    self->refresh_tick ();
}

void gateway_t::refresh_tick ()
{
    if (_stop.get () != 0)
        return;

    std::vector<std::string> services_to_refresh;
    {
        scoped_lock_t lock (_sync);
        process_monitor_events ();
        const uint64_t now_ms = _clock.now_ms ();
        for (std::map<std::string, uint64_t>::iterator it =
               _down_until_ms.begin ();
             it != _down_until_ms.end ();) {
            if (now_ms >= it->second) {
                _down_endpoints.erase (it->first);
                it = _down_until_ms.erase (it);
                _force_refresh_all = true;
            } else {
                ++it;
            }
        }
        if (_discovery) {
            if (_force_refresh_all) {
                for (std::map<std::string, service_pool_t>::iterator it =
                       _pools.begin ();
                     it != _pools.end (); ++it) {
                    it->second.dirty = true;
                    services_to_refresh.push_back (it->first);
                }
            } else {
                for (std::set<std::string>::iterator sit =
                       _pending_updates.begin ();
                     sit != _pending_updates.end (); ++sit) {
                    std::map<std::string, service_pool_t>::iterator pit =
                      _pools.find (*sit);
                    if (pit != _pools.end ()) {
                        pit->second.dirty = true;
                        services_to_refresh.push_back (*sit);
                    }
                }
            }
        }
        _pending_updates.clear ();
        _force_refresh_all = false;
    }
    if (_discovery && !services_to_refresh.empty ()) {
        for (size_t i = 0; i < services_to_refresh.size (); ++i) {
            const std::string &service = services_to_refresh[i];
            std::vector<provider_info_t> providers;
            _discovery->snapshot_providers (service, &providers);
            const uint64_t seq = _discovery->service_update_seq (service);
            scoped_lock_t lock (_sync);
            std::map<std::string, service_pool_t>::iterator it =
              _pools.find (service);
            if (it == _pools.end ())
                continue;
            if (!it->second.dirty)
                continue;
            refresh_pool (&it->second, providers, seq);
        }
    }
}

int gateway_t::init_router_socket ()
{
    if (_router_socket)
        return 0;
    if (allocate_router (_ctx, &_router_socket) != 0)
        return -1;
    ensure_gateway_routing_id (_router_socket, &_routing_id_override);
    if (_routing_id.size == 0) {
        size_t size = sizeof (_routing_id.data);
        if (_router_socket->getsockopt (ZLINK_ROUTING_ID, _routing_id.data,
                                        &size)
            == 0)
            _routing_id.size = static_cast<uint8_t> (size);
    }
    if (!_tls_server_cert.empty ()) {
        if (_router_socket->setsockopt (ZLINK_TLS_CERT, _tls_server_cert.data (),
                                        _tls_server_cert.size ())
              != 0
            || _router_socket->setsockopt (ZLINK_TLS_KEY, _tls_server_key.data (),
                                           _tls_server_key.size ())
                 != 0) {
            _router_socket->close ();
            _router_socket = NULL;
            return -1;
        }
    }
    // Enable socket monitor to receive connection-ready events.
    if (!_monitor_socket) {
        _monitor_socket =
          open_socket_monitor_bridge (_router_socket, monitor_event_mask ());
    }
    // Apply TLS settings before connecting to any providers.
    if (apply_tls_client (_router_socket, _tls_ca, _tls_hostname,
                          _tls_trust_system)
        != 0) {
        _router_socket->close ();
        _router_socket = NULL;
        return -1;
    }
    // Fail sends when routing id is unknown (no silent drops).
    int mandatory = 1;
    _router_socket->setsockopt (ZLINK_ROUTER_MANDATORY, &mandatory,
                                sizeof (mandatory));
    // Avoid long linger during teardown.
    int linger = 0;
    _router_socket->setsockopt (ZLINK_LINGER, &linger, sizeof (linger));
    // Allow a new connection with the same routing id to take over.
    int handover = 1;
    _router_socket->setsockopt (ZLINK_ROUTER_HANDOVER, &handover,
                                sizeof (handover));
    if (_handler.load (std::memory_order_acquire) != NULL) {
        register_gateway_handler_socket (_router_socket, this);
        if (_router_socket->socket_set_msg_handler (&gateway_router_msg_handler)
            != 0) {
            unregister_gateway_handler_socket (_router_socket);
            _router_socket->close ();
            _router_socket = NULL;
            return -1;
        }
    }
    if (_send_ready_handler.load (std::memory_order_acquire) != NULL) {
        register_gateway_handler_socket (_router_socket, this);
        if (_router_socket->socket_set_send_ready_handler (
              &gateway_send_ready_handler)
            != 0) {
            unregister_gateway_handler_socket (_router_socket);
            _router_socket->close ();
            _router_socket = NULL;
            return -1;
        }
    }
    return 0;
}

int gateway_t::ensure_router_socket ()
{
    return _router_socket ? 0 : -1;
}

gateway_t::service_pool_t *
  gateway_t::get_or_create_pool (const std::string &service_name_)
{
    std::map<std::string, service_pool_t>::iterator it =
      _pools.find (service_name_);
    if (it != _pools.end ())
        return &it->second;

    service_pool_t pool;
    pool.service_name = service_name_;
    pool.rr_index = 0;
    pool.lb_strategy = ZLINK_GATEWAY_LB_STRATEGY_ROUND_ROBIN;
    pool.last_seen_seq = 0;
    pool.dirty = true;

    if (ensure_router_socket () != 0)
        return NULL;
    _pools.insert (std::make_pair (service_name_, pool));
    if (_discovery) {
        _pending_updates.insert (service_name_);
        service_control_runtime_t *runtime = _ctx->service_control_runtime ();
        if (runtime && _refresh_task_id != 0)
            runtime->wakeup_task (_refresh_task_id);
    }
    return &_pools.find (service_name_)->second;
}

gateway_t::service_pool_t *gateway_t::get_or_create_pool_cached ()
{
    if (_service_name.empty ())
        return NULL;
    if (_last_pool && !_last_service_name.empty ()
        && _last_service_name == _service_name) {
        return _last_pool;
    }
    service_pool_t *pool = get_or_create_pool (_service_name);
    if (pool) {
        _last_service_name = _service_name;
        _last_pool = pool;
    }
    return pool;
}

void gateway_t::refresh_pool (service_pool_t *pool_,
                              const std::vector<provider_info_t> &providers,
                              uint64_t seq_)
{
    if (!pool_ || !_router_socket)
        return;

    if (!providers.empty ())
        lock_routing_id ();

    process_monitor_events ();
    bool keep_dirty = false;

    // 2) Build routing_id map by endpoint for this service.
    std::vector<std::string> next_endpoints;
    std::vector<zlink_routing_id_t> next_routing_ids;
    std::vector<uint32_t> next_weights;
    struct provider_route_t
    {
        zlink_routing_id_t rid;
        uint32_t weight;
    };
    std::map<std::string, provider_route_t> routing_map;

    for (size_t i = 0; i < providers.size (); ++i) {
        const provider_info_t &entry = providers[i];
        provider_route_t route = {entry.routing_id, entry.weight};
        routing_map[entry.endpoint] = route;
    }

    // 3) Connect and keep only peers that are actually ready (POLLOUT).
    for (std::map<std::string, provider_route_t>::const_iterator it =
           routing_map.begin ();
         it != routing_map.end (); ++it) {
        const std::string &endpoint = it->first;
        const zlink_routing_id_t &rid = it->second.rid;
        const std::string rid_key = routing_id_key (rid);
        const uint32_t weight = it->second.weight == 0 ? 1 : it->second.weight;
        if (rid.size == 0 || rid_key.empty ())
            continue;
        std::map<std::string, uint64_t>::iterator dit =
          _down_until_ms.find (endpoint);
        if (dit != _down_until_ms.end ()) {
            if (_clock.now_ms () < dit->second)
                continue;
            _down_until_ms.erase (dit);
            _down_endpoints.erase (endpoint);
        }
        // Only attempt a new connect when there is no active or inflight
        // connection for this endpoint. This prevents duplicate connect()
        // calls with the same CONNECT_ROUTING_ID while handshake/monitor
        // updates are still pending.
        const bool connected =
          std::find (pool_->endpoints.begin (), pool_->endpoints.end (),
                     endpoint)
          != pool_->endpoints.end ();
        bool inflight =
          _inflight_endpoints.find (endpoint) != _inflight_endpoints.end ();
        if (!connected && !inflight) {
            std::map<std::string, uint64_t>::iterator rit =
              _rid_connect_not_before_ms.find (rid_key);
            if (rit != _rid_connect_not_before_ms.end ()) {
                if (_clock.now_ms () < rit->second)
                    continue;
                _rid_connect_not_before_ms.erase (rit);
            }

            bool rid_conflict_connected = false;
            for (size_t pi = 0;
                 pi < pool_->routing_ids.size () && pi < pool_->endpoints.size ();
                 ++pi) {
                if (pool_->endpoints[pi] != endpoint
                    && routing_id_equals (pool_->routing_ids[pi], rid)) {
                    rid_conflict_connected = true;
                    break;
                }
            }
            if (rid_conflict_connected)
                continue;

            bool rid_conflict_inflight = false;
            for (std::map<std::string, std::string>::const_iterator fit =
                   _inflight_rid_by_endpoint.begin ();
                 fit != _inflight_rid_by_endpoint.end (); ++fit) {
                if (fit->first != endpoint && fit->second == rid_key) {
                    rid_conflict_inflight = true;
                    break;
                }
            }
            if (rid_conflict_inflight)
                continue;

            _router_socket->setsockopt (ZLINK_CONNECT_ROUTING_ID, rid.data,
                                        rid.size);
            if (_router_socket->connect (endpoint.c_str ()) == 0) {
                _inflight_endpoints.insert (endpoint);
                _inflight_rid_by_endpoint[endpoint] = rid_key;
                inflight = true;
            } else {
                continue;
            }
        }
        if (!connected && !inflight)
            continue;
        if (_ready_endpoints.find (endpoint) == _ready_endpoints.end ()) {
            const int state = _router_socket->get_peer_state (rid.data,
                                                             rid.size);
            if (state >= 0 && (state & ZLINK_POLLOUT)) {
                _ready_endpoints.insert (endpoint);
                _inflight_endpoints.erase (endpoint);
                _inflight_rid_by_endpoint.erase (endpoint);
                next_endpoints.push_back (endpoint);
                next_routing_ids.push_back (rid);
                next_weights.push_back (weight);
            } else {
                keep_dirty = true;
            }
            // Not ready yet: do not add to pool, but also do not term.
            continue;
        }
        _inflight_endpoints.erase (endpoint);
        _inflight_rid_by_endpoint.erase (endpoint);
        next_endpoints.push_back (endpoint);
        next_routing_ids.push_back (rid);
        next_weights.push_back (weight);
    }

    // 4) Disconnect endpoints that disappeared from discovery only.
    //    Readiness is transient; do not term on temporary not-ready.
    for (size_t i = 0; i < pool_->endpoints.size (); ++i) {
        const std::string &endpoint = pool_->endpoints[i];
        if (routing_map.find (endpoint) == routing_map.end ()) {
            if (i < pool_->routing_ids.size ()) {
                const std::string removed_rid_key =
                  routing_id_key (pool_->routing_ids[i]);
                if (!removed_rid_key.empty ()) {
                    _rid_connect_not_before_ms[removed_rid_key] =
                      _clock.now_ms () + rid_handover_guard_ms ();
                }
            }
            _router_socket->term_endpoint (endpoint.c_str ());
            _inflight_endpoints.erase (endpoint);
            _inflight_rid_by_endpoint.erase (endpoint);
            _ready_endpoints.erase (endpoint);
        }
    }

    // 5) Commit refreshed pool.
    for (size_t i = 0; i < pool_->endpoints.size (); ++i) {
        _endpoint_to_service.erase (pool_->endpoints[i]);
    }
    for (size_t i = 0; i < pool_->routing_ids.size (); ++i) {
        const std::string key = routing_id_key (pool_->routing_ids[i]);
        if (!key.empty ())
            _routing_id_to_service.erase (key);
    }
    pool_->endpoints.swap (next_endpoints);
    pool_->routing_ids.swap (next_routing_ids);
    pool_->weights.swap (next_weights);
    for (size_t i = 0; i < pool_->routing_ids.size (); ++i) {
        const std::string key = routing_id_key (pool_->routing_ids[i]);
        if (!key.empty ())
            _routing_id_to_service[key] = pool_->service_name;
    }
    // Track endpoint->service for monitor event routing.
    for (std::map<std::string, provider_route_t>::const_iterator it =
           routing_map.begin ();
         it != routing_map.end (); ++it) {
        _endpoint_to_service[it->first] = pool_->service_name;
    }
    for (size_t i = 0; i < pool_->endpoints.size (); ++i) {
        _endpoint_to_service[pool_->endpoints[i]] = pool_->service_name;
    }
    pool_->dirty = keep_dirty;
    pool_->last_seen_seq = seq_;
}

bool gateway_t::select_provider (service_pool_t *pool_, size_t *index_out_)
{
    if (!pool_ || pool_->routing_ids.empty () || !index_out_)
        return false;

    if (pool_->lb_strategy == ZLINK_GATEWAY_LB_STRATEGY_WEIGHTED
        && !pool_->weights.empty ()
        && pool_->weights.size () == pool_->routing_ids.size ()) {
        size_t total_weight = 0;
        for (size_t i = 0; i < pool_->weights.size (); ++i)
            total_weight += pool_->weights[i] == 0 ? 1 : pool_->weights[i];

        if (total_weight > 0) {
            size_t slot = pool_->rr_index % total_weight;
            pool_->rr_index++;
            for (size_t i = 0; i < pool_->weights.size (); ++i) {
                const size_t weight = pool_->weights[i] == 0 ? 1 : pool_->weights[i];
                if (slot < weight) {
                    *index_out_ = i;
                    return true;
                }
                slot -= weight;
            }
        }
    }

    const size_t index = pool_->rr_index % pool_->routing_ids.size ();
    pool_->rr_index++;
    *index_out_ = index;
    return true;
}

bool gateway_t::find_provider_index (service_pool_t *pool_,
                                     const zlink_routing_id_t *rid_,
                                     size_t *index_out_)
{
    if (!pool_ || !rid_ || !index_out_)
        return false;

    if (pool_->routing_ids.size () == 1) {
        if (routing_id_equals (pool_->routing_ids[0], *rid_)) {
            *index_out_ = 0;
            return true;
        }
        return false;
    }

    for (size_t i = 0; i < pool_->routing_ids.size (); ++i) {
        if (routing_id_equals (pool_->routing_ids[i], *rid_)) {
            *index_out_ = i;
            return true;
        }
    }
    return false;
}

int gateway_t::send_request_frames (service_pool_t *pool_,
                                    size_t provider_index_,
                                    zlink_msg_t *parts_,
                                    size_t part_count_,
                                    int flags_)
{
    if (!pool_ || !_router_socket) {
        errno = ENOTSUP;
        return -1;
    }
    if (flags_ != 0 && flags_ != ZLINK_DONTWAIT) {
        errno = ENOTSUP;
        return -1;
    }
    if (provider_index_ >= pool_->routing_ids.size ()) {
        errno = EINVAL;
        return -1;
    }

    const zlink_routing_id_t &rid = pool_->routing_ids[provider_index_];
    if ((flags_ & ZLINK_DONTWAIT) != 0) {
        const int peer_state =
          _router_socket->get_peer_state (rid.data, rid.size);
        if (peer_state < 0 || (peer_state & ZLINK_POLLOUT) == 0) {
            errno = EAGAIN;
            return -1;
        }
    }
    zlink_msg_t rid_msg;
    if (zlink_msg_init_data (
          &rid_msg,
          rid.size > 0 ? const_cast<uint8_t *> (rid.data) : NULL,
          rid.size,
          NULL,
          NULL)
        != 0)
        return -1;
    int send_flags =
      (part_count_ > 0 ? ZLINK_SNDMORE : 0) | (flags_ & ZLINK_DONTWAIT);
    if (zlink_msg_send (&rid_msg, _router_socket, send_flags) < 0) {
        zlink_msg_close (&rid_msg);
        return -1;
    }
    zlink_msg_close (&rid_msg);

    for (size_t i = 0; i < part_count_; ++i) {
        send_flags =
          (i + 1 < part_count_) ? ZLINK_SNDMORE : 0;
        send_flags |= (flags_ & ZLINK_DONTWAIT);
        if (zlink_msg_send (&parts_[i], _router_socket, send_flags) < 0) {
            return -1;
        }
        zlink_msg_close (&parts_[i]);
    }

    return 0;
}

int gateway_t::send (zlink_msg_t *parts_, size_t part_count_, int flags_)
{
    if (!parts_ || part_count_ == 0) {
        errno = EINVAL;
        return -1;
    }
    if (flags_ != 0 && flags_ != ZLINK_DONTWAIT) {
        errno = ENOTSUP;
        return -1;
    }

    scoped_optional_lock_t lock (_use_lock ? &_sync : NULL);
    if (ensure_facade_mode () != 0)
        return -1;
    lock_routing_id ();
    service_pool_t *pool = get_or_create_pool_cached ();
    if (!pool) {
        errno = ENOMEM;
        return -1;
    }

    // Fast-path for the common single-provider case.
    if (pool->routing_ids.size () == 1) {
        return send_request_frames (pool, 0, parts_, part_count_, flags_);
    }

    size_t provider_index = 0;
    if (!select_provider (pool, &provider_index)) {
        errno = EHOSTUNREACH;
        return -1;
    }

    return send_request_frames (pool, provider_index, parts_, part_count_,
                                flags_);
}

std::string gateway_t::resolve_advertise (const char *advertise_endpoint_) const
{
    return services::normalize_advertise_endpoint (advertise_endpoint_,
                                                   _bind_endpoint);
}

int gateway_t::bind (const char *endpoint_)
{
    if (!endpoint_ || endpoint_[0] == '\0') {
        errno = EINVAL;
        return -1;
    }

    scoped_optional_lock_t lock (_use_lock ? &_sync : NULL);
    if (ensure_facade_mode () != 0)
        return -1;
    lock_routing_id ();
    if (ensure_router_socket () != 0 || !_router_socket) {
        errno = ENOTSUP;
        return -1;
    }

    if (!_tls_server_cert.empty ()) {
        if (_router_socket->setsockopt (ZLINK_TLS_CERT, _tls_server_cert.data (),
                                        _tls_server_cert.size ())
              != 0
            || _router_socket->setsockopt (ZLINK_TLS_KEY, _tls_server_key.data (),
                                           _tls_server_key.size ())
                 != 0)
            return -1;
    }

    const bool already_bound_same_endpoint =
      !_bind_endpoint.empty () && _bind_endpoint == endpoint_;

    if (!already_bound_same_endpoint) {
        _bind_endpoint = endpoint_;
        if (_router_socket->bind (endpoint_) != 0)
            return -1;
    }

    if (!_discovery)
        return 0;

    return register_service (endpoint_, _server_weight);
}

int gateway_t::register_service (const char *advertise_endpoint_,
                                 uint32_t weight_)
{
    if (_service_name.empty ()) {
        errno = EINVAL;
        return -1;
    }

    scoped_optional_lock_t lock (_use_lock ? &_sync : NULL);
    if (ensure_facade_mode () != 0)
        return -1;
    if (!_discovery) {
        errno = ENOTSUP;
        return -1;
    }
    if (ensure_router_socket () != 0 || !_router_socket) {
        errno = ENOTSUP;
        return -1;
    }

    if (_routing_id.size == 0) {
        size_t size = sizeof (_routing_id.data);
        if (_router_socket->getsockopt (ZLINK_ROUTING_ID, _routing_id.data,
                                        &size)
            != 0)
            return -1;
        _routing_id.size = static_cast<uint8_t> (size);
    }

    _server_service_name = _service_name;
    _advertise_endpoint = resolve_advertise (advertise_endpoint_);
    if (_advertise_endpoint.empty ()) {
        errno = EINVAL;
        return -1;
    }
    _server_weight = weight_ == 0 ? 1 : weight_;

    std::string resolved;
    if (_discovery->register_service (
          discovery_protocol::service_type_gateway_receiver,
          _server_service_name.c_str (), _advertise_endpoint.c_str (),
          _server_weight, &resolved, &_routing_id)
        != 0) {
        _last_register_error = strerror (errno);
        return -1;
    }

    if (!resolved.empty ())
        _advertise_endpoint.swap (resolved);
    _last_register_error.clear ();
    emit_event (ZLINK_GATEWAY_SERVICE_READY, _server_service_name,
                _advertise_endpoint, NULL, 1, 0);
    return 0;
}

int gateway_t::update_weight (uint32_t weight_)
{
    if (_service_name.empty ()) {
        errno = EINVAL;
        return -1;
    }

    scoped_optional_lock_t lock (_use_lock ? &_sync : NULL);
    if (ensure_facade_mode () != 0)
        return -1;
    if (!_discovery) {
        errno = ENOTSUP;
        return -1;
    }
    if (_advertise_endpoint.empty () || _server_service_name != _service_name) {
        errno = EFSM;
        return -1;
    }

    const uint32_t value = weight_ == 0 ? 1 : weight_;
    if (_discovery->update_service_weight (
          discovery_protocol::service_type_gateway_receiver, _service_name.c_str (),
          _advertise_endpoint.c_str (), value)
        != 0)
        return -1;
    _server_weight = value;
    return 0;
}

int gateway_t::unregister_service ()
{
    if (_service_name.empty ()) {
        errno = EINVAL;
        return -1;
    }

    scoped_optional_lock_t lock (_use_lock ? &_sync : NULL);
    if (ensure_facade_mode () != 0)
        return -1;
    if (!_discovery) {
        errno = ENOTSUP;
        return -1;
    }
    if (_server_service_name.empty () || _advertise_endpoint.empty ()
        || _server_service_name != _service_name) {
        errno = EINVAL;
    }

    if (_server_service_name.empty () || _advertise_endpoint.empty ()
        || _server_service_name != _service_name
        || _discovery->unregister_service (
             discovery_protocol::service_type_gateway_receiver,
             _service_name.c_str (),
             _advertise_endpoint.c_str ())
             != 0) {
        return -1;
    }

    const std::string service_name (_server_service_name);
    const std::string endpoint (_advertise_endpoint);
    _server_service_name.clear ();
    _advertise_endpoint.clear ();
    _last_register_error.clear ();

    report_topology (service_name, endpoint, ZLINK_TOPOLOGY_STATE_STOPPED, 0, 0);

    emit_event (ZLINK_GATEWAY_SERVICE_LOST, service_name, endpoint, NULL, 0, 0);
    return 0;
}

int gateway_t::send_rid (const zlink_routing_id_t *routing_id_,
                         zlink_msg_t *parts_,
                         size_t part_count_,
                         int flags_)
{
    if (!routing_id_ || !parts_ || part_count_ == 0) {
        errno = EINVAL;
        return -1;
    }
    if (flags_ != 0 && flags_ != ZLINK_DONTWAIT) {
        errno = ENOTSUP;
        return -1;
    }

    scoped_optional_lock_t lock (_use_lock ? &_sync : NULL);
    if (ensure_facade_mode () != 0)
        return -1;
    if (_router_socket == socket_base_t::current_socket_msg_dispatch_socket ()) {
        pipe_t *dispatch_pipe =
          socket_base_t::current_socket_msg_dispatch_pipe ();
        if (pipe_routing_id_equals (dispatch_pipe, routing_id_))
            return send_parts_via_dispatch_pipe (dispatch_pipe, parts_,
                                                 part_count_);
    }
    service_pool_t *pool = get_or_create_pool_cached ();
    if (!pool) {
        errno = ENOMEM;
        return -1;
    }

    size_t provider_index = 0;
    if (find_provider_index (pool, routing_id_, &provider_index)) {
        return send_request_frames (pool, provider_index, parts_, part_count_,
                                    flags_);
    }
    if (!_router_socket) {
        errno = ENOTSUP;
        return -1;
    }
    if (routing_id_->size == 0) {
        errno = EINVAL;
        return -1;
    }

    zlink_msg_t rid_msg;
    if (zlink_msg_init_data (&rid_msg, const_cast<uint8_t *> (routing_id_->data),
                             routing_id_->size, NULL, NULL)
        != 0)
        return -1;
    int send_flags =
      (part_count_ > 0 ? ZLINK_SNDMORE : 0) | (flags_ & ZLINK_DONTWAIT);
    if (zlink_msg_send (&rid_msg, _router_socket, send_flags) < 0) {
        zlink_msg_close (&rid_msg);
        return -1;
    }
    zlink_msg_close (&rid_msg);

    for (size_t i = 0; i < part_count_; ++i) {
        send_flags = (i + 1 < part_count_) ? ZLINK_SNDMORE : 0;
        send_flags |= (flags_ & ZLINK_DONTWAIT);
        if (zlink_msg_send (&parts_[i], _router_socket, send_flags) < 0)
            return -1;
        zlink_msg_close (&parts_[i]);
    }
    return 0;
}

int gateway_t::set_lb_strategy (int strategy_)
{
    if (_service_name.empty ()) {
        errno = EINVAL;
        return -1;
    }
    if (strategy_ != ZLINK_GATEWAY_LB_STRATEGY_ROUND_ROBIN
        && strategy_ != ZLINK_GATEWAY_LB_STRATEGY_WEIGHTED) {
        errno = EINVAL;
        return -1;
    }

    scoped_optional_lock_t lock (_use_lock ? &_sync : NULL);
    if (ensure_facade_mode () != 0)
        return -1;
    service_pool_t *pool = get_or_create_pool (_service_name);
    if (!pool)
        return -1;
    pool->lb_strategy = strategy_;
    return 0;
}

int gateway_t::set_routing_id (const void *data_, size_t size_)
{
    if (!data_ || size_ == 0 || size_ > sizeof (_routing_id.data)) {
        errno = EINVAL;
        return -1;
    }

    scoped_optional_lock_t lock (_use_lock ? &_sync : NULL);
    if (!_pools.empty () || _routing_id_locked) {
        errno = EFSM;
        return -1;
    }

    _routing_id_override.assign (static_cast<const char *> (data_), size_);
    memcpy (_routing_id.data, data_, size_);
    _routing_id.size = static_cast<uint8_t> (size_);
    if (_router_socket
        && _router_socket->setsockopt (ZLINK_ROUTING_ID, data_, size_) != 0)
        return -1;
    return 0;
}

int gateway_t::routing_id (zlink_routing_id_t *out_)
{
    if (!out_) {
        errno = EINVAL;
        return -1;
    }

    scoped_optional_lock_t lock (_use_lock ? &_sync : NULL);
    if (_routing_id.size == 0) {
        if (ensure_router_socket () != 0)
            return -1;
        size_t size = sizeof (_routing_id.data);
        if (_router_socket->getsockopt (ZLINK_ROUTING_ID, _routing_id.data,
                                        &size)
            != 0)
            return -1;
        _routing_id.size = static_cast<uint8_t> (size);
    }
    *out_ = _routing_id;
    return 0;
}

int gateway_t::last_endpoint (char *endpoint_out_, size_t *size_out_) const
{
    if (!endpoint_out_ || !size_out_) {
        errno = EINVAL;
        return -1;
    }

    scoped_optional_lock_t lock (_use_lock ? const_cast<mutex_t *> (&_sync) : NULL);
    if (!_router_socket) {
        errno = ENOTSUP;
        return -1;
    }
    if (zlink_getsockopt (static_cast<void *> (_router_socket),
                          ZLINK_SOCKOPT_LAST_ENDPOINT, endpoint_out_,
                          size_out_)
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

int gateway_t::peer_info (const zlink_routing_id_t *routing_id_,
                          zlink_gateway_peer_info_t *info_out_) const
{
    if (!routing_id_ || !info_out_) {
        errno = EINVAL;
        return -1;
    }

    scoped_optional_lock_t lock (_use_lock ? const_cast<mutex_t *> (&_sync) : NULL);
    if (!_router_socket) {
        errno = ENOTSUP;
        return -1;
    }
    zlink_peer_info_t base_info;
    if (zlink_socket_peer_info (static_cast<void *> (_router_socket), routing_id_,
                                &base_info)
        != 0)
        return -1;

    memset (info_out_, 0, sizeof (*info_out_));
    info_out_->routing_id = base_info.routing_id;
    memcpy (info_out_->remote_addr, base_info.remote_addr,
            sizeof (info_out_->remote_addr));
    info_out_->connected_time = base_info.connected_time;
    info_out_->msgs_sent = base_info.msgs_sent;
    info_out_->msgs_received = base_info.msgs_received;
    info_out_->snd_pending_msgs = base_info.snd_pending_msgs;
    info_out_->rcv_pending_msgs = base_info.rcv_pending_msgs;
    info_out_->weight = 1;

    if (_discovery) {
        std::vector<provider_info_t> providers;
        _discovery->snapshot_providers (_service_name, &providers);
        for (size_t i = 0; i < providers.size (); ++i) {
            if (routing_id_equals (providers[i].routing_id, *routing_id_)) {
                info_out_->weight =
                  providers[i].weight == 0 ? 1 : providers[i].weight;
                break;
            }
        }
    }
    return 0;
}

int gateway_t::router_peers (zlink_gateway_peer_info_t *peers_,
                             size_t *count_) const
{
    if (!count_) {
        errno = EINVAL;
        return -1;
    }

    scoped_optional_lock_t lock (_use_lock ? const_cast<mutex_t *> (&_sync) : NULL);
    if (!_router_socket) {
        errno = ENOTSUP;
        return -1;
    }

    size_t available = 0;
    if (zlink_socket_peers (static_cast<void *> (_router_socket), NULL, &available)
        != 0)
        return -1;

    if (!peers_) {
        *count_ = available;
        return 0;
    }

    size_t to_copy = *count_ < available ? *count_ : available;
    std::vector<zlink_peer_info_t> base_infos (to_copy);
    if (to_copy > 0
        && zlink_socket_peers (static_cast<void *> (_router_socket), &base_infos[0],
                               &to_copy)
             != 0)
        return -1;

    std::vector<provider_info_t> providers;
    if (_discovery)
        _discovery->snapshot_providers (_service_name, &providers);

    for (size_t i = 0; i < to_copy; ++i) {
        memset (&peers_[i], 0, sizeof (peers_[i]));
        peers_[i].routing_id = base_infos[i].routing_id;
        memcpy (peers_[i].remote_addr, base_infos[i].remote_addr,
                sizeof (peers_[i].remote_addr));
        peers_[i].connected_time = base_infos[i].connected_time;
        peers_[i].msgs_sent = base_infos[i].msgs_sent;
        peers_[i].msgs_received = base_infos[i].msgs_received;
        peers_[i].snd_pending_msgs = base_infos[i].snd_pending_msgs;
        peers_[i].rcv_pending_msgs = base_infos[i].rcv_pending_msgs;
        peers_[i].weight = 1;
        for (size_t pi = 0; pi < providers.size (); ++pi) {
            if (routing_id_equals (providers[pi].routing_id,
                                   base_infos[i].routing_id)) {
                peers_[i].weight =
                  providers[pi].weight == 0 ? 1 : providers[pi].weight;
                break;
            }
        }
    }

    *count_ = to_copy;
    return 0;
}

int gateway_t::update_peer_weight (const zlink_routing_id_t *routing_id_,
                                   uint32_t weight_)
{
    if (!routing_id_ || routing_id_->size == 0) {
        errno = EINVAL;
        return -1;
    }
    if (_service_name.empty ()) {
        errno = EINVAL;
        return -1;
    }

    scoped_optional_lock_t lock (_use_lock ? &_sync : NULL);
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

    const uint32_t value = weight_ == 0 ? 1 : weight_;
    if (_discovery->update_service_weight (
          discovery_protocol::service_type_gateway_receiver, _service_name.c_str (),
          endpoint.c_str (), value)
        != 0)
        return -1;

    service_pool_t *pool = get_or_create_pool (_service_name);
    if (pool) {
        for (size_t i = 0; i < pool->routing_ids.size ()
                           && i < pool->weights.size ();
             ++i) {
            if (routing_id_equals (pool->routing_ids[i], *routing_id_)) {
                pool->weights[i] = value;
                break;
            }
        }
    }

    if (_routing_id.size > 0 && routing_id_equals (_routing_id, *routing_id_))
        _server_weight = value;

    return 0;
}

int gateway_t::set_option (int option_,
                           const void *optval_,
                           size_t optvallen_)
{
    switch (option_) {
        case ZLINK_GATEWAY_OPT_SNDHWM:
            return set_socket_option (ZLINK_SNDHWM, optval_, optvallen_);
        case ZLINK_GATEWAY_OPT_RCVHWM:
            return set_socket_option (ZLINK_RCVHWM, optval_, optvallen_);
        case ZLINK_GATEWAY_OPT_SNDTIMEO:
            return set_socket_option (ZLINK_SNDTIMEO, optval_, optvallen_);
        case ZLINK_GATEWAY_OPT_LINGER:
            return set_socket_option (ZLINK_LINGER, optval_, optvallen_);
        case ZLINK_GATEWAY_OPT_SNDBUF:
            return set_socket_option (ZLINK_SNDBUF, optval_, optvallen_);
        case ZLINK_GATEWAY_OPT_RCVBUF:
            return set_socket_option (ZLINK_RCVBUF, optval_, optvallen_);
        default:
            errno = EINVAL;
            return -1;
    }
}

int gateway_t::set_tls_server (const char *cert_, const char *key_)
{
    if (!cert_ || !key_) {
        errno = EINVAL;
        return -1;
    }

    scoped_optional_lock_t lock (_use_lock ? &_sync : NULL);
    if (ensure_facade_mode () != 0)
        return -1;
    if (cert_[0] == '\0' || key_[0] == '\0') {
        _tls_server_cert.clear ();
        _tls_server_key.clear ();
        return 0;
    }

    _tls_server_cert = cert_;
    _tls_server_key = key_;
    if (_router_socket) {
        if (_router_socket->setsockopt (ZLINK_TLS_CERT, _tls_server_cert.data (),
                                        _tls_server_cert.size ())
              != 0
            || _router_socket->setsockopt (ZLINK_TLS_KEY, _tls_server_key.data (),
                                           _tls_server_key.size ())
                 != 0)
            return -1;
    }
    return 0;
}

void *gateway_t::monitor_open (int events_)
{
    return _monitor.open (events_);
}

int gateway_t::set_socket_option (int option_,
                                  const void *optval_,
                                  size_t optvallen_)
{
    if (!optval_ || optvallen_ == 0) {
        errno = EINVAL;
        return -1;
    }

    scoped_optional_lock_t lock (_use_lock ? &_sync : NULL);
    if (ensure_facade_mode () != 0)
        return -1;
    if (!_router_socket) {
        errno = ENOTSUP;
        return -1;
    }
    return _router_socket->setsockopt (option_, optval_, optvallen_);
}

void *gateway_t::router ()
{
    scoped_optional_lock_t lock (_use_lock ? &_sync : NULL);
    lock_routing_id ();
    if (!enter_pollable_mode ())
        return NULL;
    if (ensure_router_socket () != 0)
        return NULL;
    return static_cast<void *> (_router_socket);
}

void *gateway_t::poller_socket ()
{
    scoped_optional_lock_t lock (_use_lock ? &_sync : NULL);
    lock_routing_id ();
    if (ensure_router_socket () != 0)
        return NULL;
    return static_cast<void *> (_router_socket);
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
    if (_pollable_mode) {
        errno = EFSM;
        return -1;
    }
    return 0;
}

void gateway_t::emit_event (uint32_t event_type_,
                            const std::string &service_name_,
                            const std::string &endpoint_,
                            const zlink_routing_id_t *routing_id_,
                            uint32_t value_,
                            int32_t error_code_)
{
    zlink_service_event_t ev;
    memset (&ev, 0, sizeof (ev));
    ev.service_kind = ZLINK_SERVICE_KIND_GATEWAY;
    ev.event_type = event_type_;
    ev.error_code = error_code_;
    ev.value = value_;
    if (!service_name_.empty ()) {
        ev.detail_flags |= ZLINK_EVENT_DETAIL_SERVICE_NAME;
        strncpy (ev.service_name, service_name_.c_str (),
                 sizeof (ev.service_name) - 1);
    }
    if (!endpoint_.empty ()) {
        ev.detail_flags |= ZLINK_EVENT_DETAIL_ENDPOINT;
        strncpy (ev.endpoint, endpoint_.c_str (), sizeof (ev.endpoint) - 1);
    }
    if (routing_id_ && routing_id_->size > 0) {
        ev.detail_flags |= ZLINK_EVENT_DETAIL_PEER_RID;
        ev.routing_id = *routing_id_;
    } else if (_routing_id.size > 0) {
        ev.detail_flags |= ZLINK_EVENT_DETAIL_SUBJECT_RID;
        ev.routing_id = _routing_id;
    }
    _monitor.emit (ev);

    uint16_t state = 0;
    switch (event_type_) {
        case ZLINK_GATEWAY_SERVICE_READY:
            state = ZLINK_TOPOLOGY_STATE_READY;
            break;
        case ZLINK_GATEWAY_SERVICE_LOST:
            state = ZLINK_TOPOLOGY_STATE_LOST;
            break;
        case ZLINK_GATEWAY_CONNECTION_COUNT_CHANGED:
            state = value_ > 0 ? ZLINK_TOPOLOGY_STATE_READY
                               : ZLINK_TOPOLOGY_STATE_CONNECTING;
            break;
        default:
            break;
    }
    if (state != 0)
        report_topology (service_name_, endpoint_, state, value_, error_code_);

}

void gateway_t::report_topology (const std::string &service_name_,
                                 const std::string &endpoint_,
                                 uint16_t state_,
                                 uint32_t ready_count_,
                                 int32_t error_code_)
{
    if (!_discovery || service_name_.empty () || _routing_id.size == 0)
        return;

    std::vector<provider_info_t> providers;
    _discovery->snapshot_providers (service_name_, &providers);

    zlink_registry_topology_entry_t entry;
    memset (&entry, 0, sizeof (entry));
    entry.routing_id = _routing_id;
    entry.service_kind = ZLINK_SERVICE_KIND_GATEWAY;
    strncpy (entry.service_name, service_name_.c_str (),
             sizeof (entry.service_name) - 1);
    if (!endpoint_.empty ())
        strncpy (entry.endpoint, endpoint_.c_str (),
                 sizeof (entry.endpoint) - 1);
    entry.source = ZLINK_TOPOLOGY_SOURCE_DISCOVERY;
    entry.state = static_cast<zlink_topology_state_t> (state_);
    entry.desired_count = static_cast<uint32_t> (providers.size ());
    entry.ready_count = ready_count_;
    entry.error_code = static_cast<uint32_t> (error_code_);
    entry.last_reported_ms = _clock.now_ms ();
    _discovery->upsert_service_summary (entry);
}

void gateway_t::on_service_update (const std::string &service_name_)
{
    if (_stop.get () != 0)
        return;
    if (!_service_name.empty () && service_name_ != _service_name)
        return;
    scoped_lock_t lock (_sync);
    if (!service_name_.empty ()) {
        _pending_updates.insert (service_name_);
        std::map<std::string, service_pool_t>::iterator pit =
          _pools.find (service_name_);
        if (pit != _pools.end ())
            pit->second.dirty = true;
    }
    service_control_runtime_t *runtime = _ctx->service_control_runtime ();
    if (runtime && _refresh_task_id != 0)
        runtime->wakeup_task (_refresh_task_id);
}

int gateway_t::connection_count ()
{
    if (_service_name.empty ()) {
        errno = EINVAL;
        return -1;
    }

    scoped_optional_lock_t lock (_use_lock ? &_sync : NULL);
    if (ensure_facade_mode () != 0)
        return -1;
    lock_routing_id ();
    process_monitor_events ();
    service_pool_t *pool = get_or_create_pool (_service_name);
    if (!pool)
        return 0;
    return static_cast<int> (pool->endpoints.size ());
}

int gateway_t::set_handler (zlink_socket_msg_handler_fn handler_)
{
    if (!handler_) {
        errno = EINVAL;
        return -1;
    }

    scoped_optional_lock_t lock (_use_lock ? &_sync : NULL);
    if (ensure_facade_mode () != 0)
        return -1;
    _handler.store (handler_, std::memory_order_release);
    if (_router_socket) {
        register_gateway_handler_socket (_router_socket, this);
        if (_router_socket->socket_set_msg_handler (&gateway_router_msg_handler)
            != 0) {
            unregister_gateway_handler_socket (_router_socket);
            return -1;
        }
    }
    return 0;
}

int gateway_t::set_send_ready_handler (zlink_send_ready_handler_fn handler_)
{
    if (!handler_) {
        errno = EINVAL;
        return -1;
    }

    _send_ready_handler.store (handler_, std::memory_order_release);
    if (_router_socket) {
        register_gateway_handler_socket (_router_socket, this);
        if (_router_socket->socket_set_send_ready_handler (
              &gateway_send_ready_handler)
            != 0) {
            return -1;
        }
    }
    return 0;
}

int gateway_t::set_tls_client (const char *ca_cert_,
                               const char *hostname_,
                               int trust_system_)
{
    if (!ca_cert_ || !hostname_) {
        errno = EINVAL;
        return -1;
    }

    scoped_optional_lock_t lock (_use_lock ? &_sync : NULL);
    if (ensure_facade_mode () != 0)
        return -1;
    _tls_ca.assign (ca_cert_);
    _tls_hostname.assign (hostname_);
    _tls_trust_system = trust_system_;

    if (ensure_router_socket () != 0)
        return -1;
    if (_router_socket
        && apply_tls_client (_router_socket, _tls_ca, _tls_hostname,
                             _tls_trust_system)
             != 0)
        return -1;
    return 0;
}

int gateway_t::destroy ()
{
    _stop.set (1);
    service_control_runtime_t *runtime = _ctx->service_control_runtime ();
    if (runtime && _refresh_task_id != 0)
        runtime->remove_task (_refresh_task_id);
    _refresh_task_id = 0;
    if (_discovery)
        _discovery->remove_observer (this);
    if (_discovery && !_server_service_name.empty () && !_advertise_endpoint.empty ()) {
        const std::string service_name (_server_service_name);
        const std::string endpoint (_advertise_endpoint);
        (void) _discovery->unregister_service (
          discovery_protocol::service_type_gateway_receiver,
          _server_service_name.c_str (), _advertise_endpoint.c_str ());
        report_topology (service_name, endpoint, ZLINK_TOPOLOGY_STATE_STOPPED, 0,
                         0);
    }
    _pools.clear ();
    _last_service_name.clear ();
    _last_pool = NULL;
    _endpoint_to_service.clear ();
    _routing_id_to_service.clear ();
    _ready_endpoints.clear ();
    _inflight_endpoints.clear ();
    _inflight_rid_by_endpoint.clear ();
    _rid_connect_not_before_ms.clear ();
    _down_endpoints.clear ();
    _down_until_ms.clear ();
    _force_refresh_all = false;
    _pending_updates.clear ();
    zlink_service_event_t terminal;
    memset (&terminal, 0, sizeof (terminal));
    terminal.service_kind = ZLINK_SERVICE_KIND_GATEWAY;
    terminal.event_type = ZLINK_MONITOR_EVENT_CLOSED;
    terminal.detail_flags = ZLINK_EVENT_DETAIL_SUBJECT_RID;
    terminal.routing_id = _routing_id;
    _monitor.close_all (&terminal);
    if (_monitor_socket) {
        zlink_close (_monitor_socket);
        _monitor_socket = NULL;
    }
    if (_router_socket) {
        unregister_gateway_handler_socket (_router_socket);
        _router_socket->close ();
        _router_socket = NULL;
    }
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

    handler (source_rid_, parts_, part_count_);
}

void gateway_t::dispatch_send_ready ()
{
    zlink_send_ready_handler_fn handler =
      _send_ready_handler.load (std::memory_order_acquire);
    if (handler)
        handler (this);
}

void gateway_t::process_monitor_events ()
{
    if (!_monitor_socket)
        return;
    while (true) {
        zlink_monitor_event_t event;
        const int rc =
          recv_socket_monitor_event (_monitor_socket, &event, ZLINK_DONTWAIT);
        if (rc != 0) {
            if (errno == EAGAIN)
                return;
            return;
        }
        const std::string endpoint = event.remote_addr;
        if (endpoint.empty ())
            continue;
        std::string service_name;
        if (event.event == ZLINK_EVENT_CONNECTION_READY) {
            _down_endpoints.erase (endpoint);
            _down_until_ms.erase (endpoint);
            _ready_endpoints.insert (endpoint);
            _inflight_endpoints.erase (endpoint);
            _inflight_rid_by_endpoint.erase (endpoint);
        } else if (event.event == ZLINK_EVENT_DISCONNECTED
                   || event.event == ZLINK_EVENT_HANDSHAKE_FAILED_NO_DETAIL
                   || event.event == ZLINK_EVENT_HANDSHAKE_FAILED_PROTOCOL
                   || event.event == ZLINK_EVENT_HANDSHAKE_FAILED_AUTH) {
            _inflight_endpoints.erase (endpoint);
            _inflight_rid_by_endpoint.erase (endpoint);
            _ready_endpoints.erase (endpoint);
            _down_endpoints.insert (endpoint);
            _down_until_ms[endpoint] = _clock.now_ms () + 500;
        }
        std::map<std::string, std::string>::iterator it =
          _endpoint_to_service.find (endpoint);
        if (it != _endpoint_to_service.end ()) {
            service_name = it->second;
            std::map<std::string, service_pool_t>::iterator pit =
              _pools.find (it->second);
            if (pit != _pools.end ()) {
                pit->second.dirty = true;
                _pending_updates.insert (it->second);
            }
        } else {
            _force_refresh_all = true;
        }

        if (!service_name.empty ()) {
            const uint32_t ready_count =
              count_ready_for_service (service_name, _endpoint_to_service,
                                       _ready_endpoints);
            if (event.event == ZLINK_EVENT_CONNECTION_READY) {
                emit_event (ZLINK_GATEWAY_ROUTE_UP, service_name, endpoint,
                            &event.routing_id, ready_count, 0);
                emit_event (ZLINK_GATEWAY_CONNECTION_COUNT_CHANGED,
                            service_name, endpoint, NULL, ready_count, 0);
                if (ready_count == 1)
                    emit_event (ZLINK_GATEWAY_SERVICE_READY, service_name,
                                endpoint, NULL, ready_count, 0);
            } else if (event.event == ZLINK_EVENT_DISCONNECTED
                       || event.event == ZLINK_EVENT_HANDSHAKE_FAILED_NO_DETAIL
                       || event.event == ZLINK_EVENT_HANDSHAKE_FAILED_PROTOCOL
                       || event.event == ZLINK_EVENT_HANDSHAKE_FAILED_AUTH) {
                emit_event (ZLINK_GATEWAY_ROUTE_DOWN, service_name, endpoint,
                            &event.routing_id, ready_count,
                            static_cast<int32_t> (event.event));
                emit_event (ZLINK_GATEWAY_CONNECTION_COUNT_CHANGED,
                            service_name, endpoint, NULL, ready_count, 0);
                if (ready_count == 0)
                    emit_event (ZLINK_GATEWAY_SERVICE_LOST, service_name,
                                endpoint, NULL, 0,
                                static_cast<int32_t> (event.event));
            }
        }
    }
}
}
