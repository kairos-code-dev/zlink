/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/gateway/gateway.hpp"

#include "core/multipart_send_txn.hpp"
#include "core/pipe.hpp"
#include "services/common/advertise_endpoint.hpp"
#include "services/control/service_control_runtime.hpp"
#include "services/discovery/discovery_protocol.hpp"
#include "services/gateway/gateway_runtime.hpp"
#include "services/gateway/routing_id_utils.hpp"

#include <cerrno>
#include <cstring>

namespace zlink
{
namespace
{
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

bool gateway_t::enter_pollable_mode ()
{
    _pollable_mode = true;
    return true;
}

int gateway_t::ensure_facade_mode () const
{
    return 0;
}

}
