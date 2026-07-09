/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "runtime/locations/location_key_codec.hpp"
#include "runtime/locations/location_runtime.hpp"
#include "runtime/locations/location_value_codec.hpp"

#include <zlink/framework/contracts/locations/runtime_query.hpp>

#include <algorithm>
#include <chrono>
#include <map>

namespace zlink::framework::runtime
{

class store_location_resolvers_t final : public peer_location_resolver_t,
                                         public spot_location_resolver_t,
                                         public actor_location_resolver_t,
                                         public location_readiness_t
{
  public:
    explicit store_location_resolvers_t (location_store_t &store) : _store (&store) {}

    task_t<std::vector<peer_location_t>> list_live_peers (peer_location_filter_t filter) override
    {
        return completed (_store->list_peers (std::move (filter)).result ().value ());
    }

    task_t<std::optional<spot_ref_t>>
    resolve_spot_ref (std::string mesh_name, zlink::routing_id_t spot_rid) override
    {
        auto row =
          _store->resolve_spot (spot_location_key_t{std::move (mesh_name), spot_rid})
            .result ()
            .value ();
        if (!row) {
            return completed (std::optional<spot_ref_t>{});
        }
        return completed (std::optional<spot_ref_t>{spot_ref_t{
          row->mesh_name, row->node_rid, row->spot_rid}});
    }

    task_t<std::optional<spot_ref_t>>
    resolve_actor_spot_ref (std::string actor_id) override
    {
        auto row = _store
                     ->resolve_actor (actor_location_key_t{std::move (actor_id)})
                     .result ()
                     .value ();
        if (!row) {
            return completed (std::optional<spot_ref_t>{});
        }
        const auto target_spot =
          row->location_kind == zlink::spot_kind::entry || !row->spot_rid
            ? row->node_rid
            : *row->spot_rid;
        return completed (std::optional<spot_ref_t>{spot_ref_t{
          row->spot_mesh_name, row->node_rid, target_spot}});
    }

    task_t<bool> is_peer_ready (std::string mesh_name,
                                location_role_t role,
                                std::optional<zlink::routing_id_t> node_rid = std::nullopt) override
    {
        try {
            auto peers = _store
                           ->list_peers (peer_location_filter_t{
                             .mesh_name = std::move (mesh_name), .role = role, .node_rid = node_rid})
                           .result ()
                           .value ();
            return completed (std::any_of (peers.begin (), peers.end (),
                                           [] (const peer_location_t &peer) {
                                               return peer.weight != 0;
                                           }));
        }
        catch (...) {
            return completed (false);
        }
    }

    task_t<std::optional<route_location_t>> resolve_route (route_location_key_t key)
    {
        return completed (_store->resolve_route (std::move (key)).result ().value ());
    }

  private:
    template <typename T> static task_t<T> completed (T value)
    {
        return task_t<T> (result_t<T>::success (std::move (value)));
    }

    location_store_t *_store;
};

class store_location_runtime_query_t final : public location_runtime_query_t
{
  public:
    store_location_runtime_query_t (location_store_t &store,
                                    location_runtime_t &runtime,
                                    const location_options_t &options) :
        _store (&store), _runtime (&runtime), _options (options)
    {
    }

    task_t<location_runtime_status_t> get_status () override
    {
        location_runtime_status_t value;
        value.watch_enabled = false;
        value.polling_interval = _options.polling_interval;
        value.owner_lease_healthy = _runtime->owner_lease_healthy ();
        value.owner_lease_renewed_at = _runtime->owner_lease_renewed_at ();
        value.last_error = _runtime->last_error ();
        try {
            (void) _store->list_owner_leases ().result ().value ();
            value.store_healthy = true;
            value.last_refresh_at = std::chrono::system_clock::now ();
            value.last_error.reset ();
        }
        catch (const std::exception &error) {
            value.store_healthy = false;
            value.owner_lease_healthy = false;
            value.last_error = error.what ();
        }
        return completed (std::move (value));
    }

    task_t<std::vector<peer_location_t>>
    list_peer_locations (peer_location_filter_t filter) override
    {
        return completed (_store->list_peers (std::move (filter)).result ().value ());
    }

    task_t<location_page_t<spot_location_t>>
    list_spot_locations (spot_location_filter_t filter, location_page_request_t page = {}) override
    {
        return completed (_store->list_spots (std::move (filter), page).result ().value ());
    }

    task_t<location_page_t<actor_location_t>>
    list_actor_locations (actor_location_filter_t filter,
                          location_page_request_t page = {}) override
    {
        return completed (_store->list_actors (std::move (filter), page).result ().value ());
    }

    task_t<location_page_t<route_location_t>>
    list_route_locations (route_location_filter_t filter,
                          location_page_request_t page = {}) override
    {
        return completed (_store->list_routes (std::move (filter), page).result ().value ());
    }

    task_t<location_page_t<location_topology_entry_t>>
    list_topology (location_topology_filter_t filter, location_page_request_t page = {}) override
    {
        auto peers = _store->list_peers (peer_location_filter_t{}).result ().value ();
        std::vector<location_topology_entry_t> matched;
        matched.reserve (peers.size ());
        for (const auto &peer : peers) {
            location_topology_entry_t entry;
            entry.kind = location_kind_t::peer;
            entry.mesh_name = peer.mesh_name;
            entry.role = peer.role;
            entry.node_rid = peer.node_rid;
            entry.endpoint = peer.endpoint;
            entry.state = peer.weight == 0 ? location_topology_state_t::lost
                                           : location_topology_state_t::ready;
            entry.desired_count = 1;
            entry.ready_count = peer.weight == 0 ? 0 : 1;
            entry.updated_at = peer.updated_at;
            if (matches (entry, filter)) {
                matched.push_back (std::move (entry));
            }
        }
        location_page_t<location_topology_entry_t> result;
        const auto offset = page.continuation_token ? parse_offset (*page.continuation_token) : 0;
        const auto page_size =
          page.page_size > 0 ? static_cast<std::size_t> (page.page_size) : matched.size ();
        for (std::size_t i = offset; i < matched.size () && result.items.size () < page_size; ++i) {
            result.items.push_back (matched[i]);
        }
        const auto next = offset + result.items.size ();
        if (next < matched.size ()) {
            result.continuation_token = std::to_string (next);
        }
        return completed (std::move (result));
    }

    task_t<std::vector<location_service_summary_t>>
    list_service_summaries (location_service_summary_filter_t filter) override
    {
        auto peers = _store->list_peers (peer_location_filter_t{}).result ().value ();
        std::map<std::string, location_service_summary_t> grouped;
        for (const auto &peer : peers) {
            if (filter.mesh_name && peer.mesh_name != *filter.mesh_name) {
                continue;
            }
            if (filter.auto_connect_type && peer.auto_connect_type != *filter.auto_connect_type) {
                continue;
            }
            if (filter.role && peer.role != *filter.role) {
                continue;
            }
            const auto key = peer.mesh_name + "|"
                             + location_value_codec_t::to_canonical_string (
                               peer.auto_connect_type)
                             + "|" + location_value_codec_t::to_canonical_string (peer.role);
            auto &summary = grouped[key];
            summary.mesh_name = peer.mesh_name;
            summary.auto_connect_type = peer.auto_connect_type;
            summary.role = peer.role;
            ++summary.total_count;
            if (peer.weight == 0) {
                ++summary.lost_count;
            } else {
                ++summary.ready_count;
            }
        }
        std::vector<location_service_summary_t> result;
        result.reserve (grouped.size ());
        for (auto &[_, summary] : grouped) {
            result.push_back (std::move (summary));
        }
        return completed (std::move (result));
    }

  private:
    template <typename T> static task_t<T> completed (T value)
    {
        return task_t<T> (result_t<T>::success (std::move (value)));
    }

    location_store_t *_store;
    location_runtime_t *_runtime;
    location_options_t _options;

    static bool matches (const location_topology_entry_t &entry,
                         const location_topology_filter_t &filter)
    {
        return (!filter.kind || entry.kind == *filter.kind)
               && (!filter.mesh_name || (entry.mesh_name && *entry.mesh_name == *filter.mesh_name))
               && (!filter.role || (entry.role && *entry.role == *filter.role))
               && (!filter.node_rid || (entry.node_rid && *entry.node_rid == *filter.node_rid))
               && (!filter.state || entry.state == *filter.state);
    }

    static std::size_t parse_offset (const std::string &value)
    {
        try {
            return static_cast<std::size_t> (std::stoull (value));
        }
        catch (...) {
            return 0;
        }
    }
};

} // namespace zlink::framework::runtime
