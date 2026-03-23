/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/gateway/gateway.hpp"

#include "core/multipart_send_txn.hpp"
#include "services/gateway/gateway_runtime.hpp"

#include <cstring>

namespace zlink
{
namespace
{
static std::string routing_id_key_local (const zlink_routing_id_t &rid_)
{
    if (rid_.size == 0)
        return std::string ();
    return std::string (reinterpret_cast<const char *> (rid_.data), rid_.size);
}

static bool routing_id_equals_local (const zlink_routing_id_t &a_,
                                     const zlink_routing_id_t &b_)
{
    if (a_.size != b_.size || a_.size == 0)
        return false;
    return memcmp (a_.data, b_.data, a_.size) == 0;
}

static uint64_t rid_handover_guard_ms_local ()
{
    return 50;
}

static std::string gateway_peer_key_local (const std::string &service_name_,
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

void gateway_t::refresh_pool (gateway_service_pool_t *pool_,
                              const std::vector<provider_info_t> &providers,
                              uint64_t seq_)
{
    if (!pool_ || !_runtime->router_socket)
        return;

    if (!providers.empty () || !_runtime->manual_routes.empty ())
        lock_routing_id ();

    process_monitor_events ();
    bool keep_dirty = false;
    std::shared_ptr<gateway_service_pool_t::send_snapshot_t> next_send_snapshot (
      new gateway_service_pool_t::send_snapshot_t);
    std::shared_ptr<gateway_service_pool_t::control_snapshot_t>
      next_control_snapshot (new gateway_service_pool_t::control_snapshot_t);
    next_send_snapshot->router_socket = _runtime->router_socket;

    const auto resolve_provider_sources = [&]() {
        if (_discovery) {
            for (size_t i = 0; i < providers.size (); ++i) {
                const provider_info_t &entry = providers[i];
                gateway_service_pool_t::control_route_t &route =
                  next_control_snapshot->routes_by_endpoint[entry.endpoint];
                route.routing_id = entry.routing_id;
                route.weight = entry.weight;
            }
            return;
        }

        if (pool_->service_name != _service_name)
            return;

        for (std::map<std::string, gateway_manual_route_t>::const_iterator it =
               _runtime->manual_routes.begin ();
             it != _runtime->manual_routes.end (); ++it) {
            gateway_service_pool_t::control_route_t &route =
              next_control_snapshot->routes_by_endpoint[it->first];
            route.routing_id = it->second.routing_id;
            route.weight = it->second.weight;
        }
    };

    const auto build_send_snapshot = [&]() {
        const gateway_service_pool_t::send_snapshot_t *published =
          pool_->send_snapshot.get ();
        for (std::map<std::string,
                      gateway_service_pool_t::control_route_t>::const_iterator it =
               next_control_snapshot->routes_by_endpoint.begin ();
             it != next_control_snapshot->routes_by_endpoint.end (); ++it) {
            const std::string &endpoint = it->first;
            const zlink_routing_id_t &rid = it->second.routing_id;
            const std::string rid_key = routing_id_key_local (rid);
            const uint32_t weight = it->second.weight;
            if (rid.size == 0 || rid_key.empty ())
                continue;
            std::map<std::string, uint64_t>::iterator dit =
              _runtime->down_until_ms.find (endpoint);
            if (dit != _runtime->down_until_ms.end ()) {
                if (_runtime->clock.now_ms () < dit->second)
                    continue;
                _runtime->down_until_ms.erase (dit);
                _runtime->down_endpoints.erase (endpoint);
            }
            const bool connected =
              published
                && std::find (published->endpoints.begin (),
                              published->endpoints.end (), endpoint)
                     != published->endpoints.end ();
            bool inflight =
              _runtime->inflight_endpoints.find (endpoint)
              != _runtime->inflight_endpoints.end ();
            if (!connected && !inflight) {
                std::map<std::string, uint64_t>::iterator rit =
                  _runtime->rid_connect_not_before_ms.find (rid_key);
                if (rit != _runtime->rid_connect_not_before_ms.end ()) {
                    if (_runtime->clock.now_ms () < rit->second)
                        continue;
                    _runtime->rid_connect_not_before_ms.erase (rit);
                }

                bool rid_conflict_connected = false;
                for (size_t pi = 0;
                     published && pi < published->routing_ids.size ()
                          && pi < published->endpoints.size ();
                     ++pi) {
                    if (published->endpoints[pi] != endpoint
                        && routing_id_equals_local (published->routing_ids[pi],
                                                    rid)) {
                        rid_conflict_connected = true;
                        break;
                    }
                }
                if (rid_conflict_connected)
                    continue;

                bool rid_conflict_inflight = false;
                for (std::map<std::string, std::string>::const_iterator fit =
                       _runtime->inflight_rid_by_endpoint.begin ();
                     fit != _runtime->inflight_rid_by_endpoint.end (); ++fit) {
                    if (fit->first != endpoint && fit->second == rid_key) {
                        rid_conflict_inflight = true;
                        break;
                    }
                }
                if (rid_conflict_inflight)
                    continue;

                _runtime->router_socket->setsockopt (
                  ZLINK_INTERNAL_OPT_CONNECT_ROUTING_ID, rid.data, rid.size);
                if (_runtime->router_socket->connect (endpoint.c_str ()) == 0) {
                    _runtime->inflight_endpoints.insert (endpoint);
                    _runtime->inflight_rid_by_endpoint[endpoint] = rid_key;
                    inflight = true;
                } else {
                    continue;
                }
            }
            if (!connected && !inflight)
                continue;
            if (_runtime->ready_endpoints.find (endpoint)
                == _runtime->ready_endpoints.end ()) {
                const int state =
                  _runtime->router_socket->get_peer_state (rid.data, rid.size);
                if (state >= 0 && (state & ZLINK_POLLOUT)) {
                    _runtime->ready_endpoints.insert (endpoint);
                    _runtime->inflight_endpoints.erase (endpoint);
                    _runtime->inflight_rid_by_endpoint.erase (endpoint);
                    next_send_snapshot->endpoints.push_back (endpoint);
                    next_send_snapshot->routing_ids.push_back (rid);
                    next_send_snapshot->weights.push_back (weight);
                } else {
                    keep_dirty = true;
                }
                continue;
            }
            _runtime->inflight_endpoints.erase (endpoint);
            _runtime->inflight_rid_by_endpoint.erase (endpoint);
            next_send_snapshot->endpoints.push_back (endpoint);
            next_send_snapshot->routing_ids.push_back (rid);
            next_send_snapshot->weights.push_back (weight);
        }
    };

    const auto detach_stale_routes = [&]() {
        const gateway_service_pool_t::send_snapshot_t *published =
          pool_->send_snapshot.get ();
        if (!published)
            return;
        for (size_t i = 0; i < published->endpoints.size (); ++i) {
            const std::string &endpoint = published->endpoints[i];
            if (next_control_snapshot->routes_by_endpoint.find (endpoint)
                != next_control_snapshot->routes_by_endpoint.end ())
                continue;

            if (i < published->routing_ids.size ()) {
                const zlink_routing_id_t &removed_rid =
                  published->routing_ids[i];
                const std::string peer_key =
                  gateway_peer_key_local (pool_->service_name, removed_rid);
                std::map<std::string,
                         gateway_runtime_t::gateway_peer_report_t>::iterator rit =
                  _runtime->ready_peer_reports.find (peer_key);
                if (rit != _runtime->ready_peer_reports.end ()) {
                    report_gateway_peer (
                      rit->second.service_name, rit->second.peer_endpoint,
                      rit->second.peer_routing_id, rit->second.weight,
                      ZLINK_TOPOLOGY_STATE_LOST,
                      rit->second.connected_since_ms);
                    _runtime->ready_peer_reports.erase (rit);
                }
                const std::string removed_rid_key =
                  routing_id_key_local (removed_rid);
                if (!removed_rid_key.empty ()) {
                    _runtime->rid_connect_not_before_ms[removed_rid_key] =
                      _runtime->clock.now_ms () + rid_handover_guard_ms_local ();
                }
            }
            _runtime->router_socket->term_endpoint (endpoint.c_str ());
            _runtime->inflight_endpoints.erase (endpoint);
            _runtime->inflight_rid_by_endpoint.erase (endpoint);
            _runtime->ready_endpoints.erase (endpoint);
        }
    };

    const auto commit_refresh = [&]() {
        const gateway_service_pool_t::send_snapshot_t *published =
          pool_->send_snapshot.get ();
        const bool was_send_ready =
          published != NULL && !published->routing_ids.empty ();
        if (published) {
            for (size_t i = 0; i < published->endpoints.size (); ++i)
                _runtime->endpoint_to_service.erase (published->endpoints[i]);
            for (size_t i = 0; i < published->routing_ids.size (); ++i) {
                const std::string key =
                  routing_id_key_local (published->routing_ids[i]);
                if (!key.empty ())
                    _runtime->routing_id_to_service.erase (key);
            }
        }

        next_control_snapshot->last_seen_seq = seq_;
        pool_->control_snapshot = next_control_snapshot;
        {
            scoped_lock_t send_lock (_send_sync);
            pool_->send_snapshot =
              std::shared_ptr<const gateway_service_pool_t::send_snapshot_t> (
                next_send_snapshot);
        }
        const gateway_service_pool_t::send_snapshot_t *current =
          pool_->send_snapshot.get ();
        const bool is_send_ready =
          current != NULL && !current->routing_ids.empty ();
        for (size_t i = 0; current && i < current->routing_ids.size (); ++i) {
            const std::string key =
              routing_id_key_local (current->routing_ids[i]);
            if (!key.empty ())
                _runtime->routing_id_to_service[key] = pool_->service_name;
        }

        for (std::map<std::string,
                      gateway_service_pool_t::control_route_t>::const_iterator
               it = pool_->control_snapshot->routes_by_endpoint.begin ();
             it != pool_->control_snapshot->routes_by_endpoint.end (); ++it) {
            _runtime->endpoint_to_service[it->first] = pool_->service_name;
        }
        for (size_t i = 0; current && i < current->endpoints.size (); ++i)
            _runtime->endpoint_to_service[current->endpoints[i]] =
              pool_->service_name;

        if (pool_ == _runtime->primary_pool) {
            if (is_send_ready && !was_send_ready)
                _runtime->send_ready_callback_pending = true;
            _runtime->send_ready_available = is_send_ready;
        }

        for (size_t i = 0;
             current && i < current->routing_ids.size ()
               && i < current->endpoints.size ();
             ++i) {
            const std::string peer_key =
              gateway_peer_key_local (pool_->service_name,
                                      current->routing_ids[i]);
            if (peer_key.empty ())
                continue;
            gateway_runtime_t::gateway_peer_report_t &report =
              _runtime->ready_peer_reports[peer_key];
            if (report.connected_since_ms == 0)
                report.connected_since_ms = _runtime->clock.now_ms ();
            report.service_name = pool_->service_name;
            report.peer_endpoint = current->endpoints[i];
            report.peer_routing_id = current->routing_ids[i];
            report.weight =
              i < current->weights.size () ? current->weights[i] : 0;
        }
    };

    resolve_provider_sources ();
    build_send_snapshot ();
    detach_stale_routes ();
    commit_refresh ();
    pool_->dirty = keep_dirty;
}

bool gateway_t::select_provider (
  gateway_service_pool_t *pool_,
  const gateway_service_pool_t::send_snapshot_t *snapshot_,
  size_t *index_out_)
{
    if (!pool_ || !snapshot_ || snapshot_->routing_ids.empty () || !index_out_)
        return false;

    const size_t cursor = pool_->rr_cursor++;
    const int lb_strategy = pool_->lb_strategy;

    if (lb_strategy == ZLINK_GATEWAY_LB_STRATEGY_WEIGHTED
        && !snapshot_->weights.empty ()
        && snapshot_->weights.size () == snapshot_->routing_ids.size ()) {
        size_t total_weight = 0;
        for (size_t i = 0; i < snapshot_->weights.size (); ++i)
            total_weight += snapshot_->weights[i];

        if (total_weight > 0) {
            size_t slot = static_cast<size_t> (cursor % total_weight);
            for (size_t i = 0; i < snapshot_->weights.size (); ++i) {
                const size_t weight = snapshot_->weights[i];
                if (slot < weight) {
                    *index_out_ = i;
                    return true;
                }
                slot -= weight;
            }
        }
    }

    const size_t index =
      static_cast<size_t> (cursor % snapshot_->routing_ids.size ());
    *index_out_ = index;
    return true;
}

bool gateway_t::find_provider_index (gateway_service_pool_t *pool_,
                                     const zlink_routing_id_t *rid_,
                                     size_t *index_out_)
{
    if (!pool_ || !rid_ || !index_out_)
        return false;

    const gateway_service_pool_t::send_snapshot_t *snapshot =
      pool_->send_snapshot.get ();
    if (!snapshot)
        return false;

    if (snapshot->routing_ids.size () == 1) {
        if (routing_id_equals_local (snapshot->routing_ids[0], *rid_)) {
            *index_out_ = 0;
            return true;
        }
        return false;
    }

    for (size_t i = 0; i < snapshot->routing_ids.size (); ++i) {
        if (routing_id_equals_local (snapshot->routing_ids[i], *rid_)) {
            *index_out_ = i;
            return true;
        }
    }
    return false;
}

int gateway_t::send_request_frames (gateway_service_pool_t *pool_,
                                    size_t provider_index_,
                                    zlink_msg_t *parts_,
                                    size_t part_count_,
                                    int flags_)
{
    if (!pool_ || !_runtime->router_socket) {
        errno = ENOTSUP;
        return -1;
    }
    if (flags_ != 0 && flags_ != ZLINK_DONTWAIT) {
        errno = ENOTSUP;
        return -1;
    }
    const gateway_service_pool_t::send_snapshot_t *snapshot =
      pool_->send_snapshot.get ();
    if (!snapshot || provider_index_ >= snapshot->routing_ids.size ()) {
        errno = EINVAL;
        return -1;
    }

    const zlink_routing_id_t &rid = snapshot->routing_ids[provider_index_];
    return zlink::logical_multipart_send_prefixed (
      _runtime->router_socket, rid.data, rid.size, parts_, part_count_, flags_,
      1000);
}
}
