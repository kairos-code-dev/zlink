/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include "runtime/locations/location_key_codec.hpp"

#include <zlink/framework/contracts/locations/stores.hpp>

#include <algorithm>
#include <map>
#include <mutex>

namespace zlink::framework::runtime
{

class in_memory_location_store_t final : public location_store_t,
                                         public location_change_stamp_store_t
{
  public:
    task_t<location_write_result_t> update_peer (peer_location_t peer,
                                                 location_write_intent_t intent) override
    {
        const auto mesh_name = peer.mesh_name;
        const auto key = location_key_codec_t::encode_peer_key (peer_location_key_t{
          peer.auto_connect_type, peer.mesh_name, peer.role, peer.node_rid, peer.endpoint});
        return completed (
          write (_peers, key, std::move (peer), intent, location_kind_t::peer, mesh_name));
    }

    task_t<location_write_result_t> remove_peer (peer_location_key_t key,
                                                 location_owner_token_t owner) override
    {
        return completed (remove (_peers, location_key_codec_t::encode_peer_key (key),
                                  std::move (owner), location_kind_t::peer, key.mesh_name));
    }

    task_t<std::vector<peer_location_t>> list_peers (peer_location_filter_t filter) override
    {
        std::lock_guard lock (_gate);
        std::vector<peer_location_t> rows;
        for (const auto &[_, row] : _peers.rows) {
            if (matches (row, filter)) {
                rows.push_back (row);
            }
        }
        return completed (std::move (rows));
    }

    task_t<location_write_result_t> update_spot (spot_location_t spot,
                                                 location_write_intent_t intent) override
    {
        const auto mesh_name = spot.mesh_name;
        const auto key = location_key_codec_t::encode_spot_key (
          spot_location_key_t{spot.mesh_name, spot.spot_rid});
        return completed (
          write (_spots, key, std::move (spot), intent, location_kind_t::spot, mesh_name));
    }

    task_t<location_write_result_t> remove_spot (spot_location_key_t key,
                                                 location_owner_token_t owner) override
    {
        return completed (remove (_spots, location_key_codec_t::encode_spot_key (key),
                                  std::move (owner), location_kind_t::spot, key.mesh_name));
    }

    task_t<std::optional<spot_location_t>> resolve_spot (spot_location_key_t key) override
    {
        std::lock_guard lock (_gate);
        const auto found = _spots.rows.find (location_key_codec_t::encode_spot_key (key));
        return completed (found == _spots.rows.end () ? std::optional<spot_location_t>{}
                                                       : std::optional<spot_location_t>{found->second});
    }

    task_t<location_page_t<spot_location_t>> list_spots (spot_location_filter_t filter,
                                                         location_page_request_t page = {}) override
    {
        return completed (page_rows (
          _spots,
          [&] (const spot_location_t &row) { return matches (row, filter); },
          page));
    }

    task_t<location_write_result_t> update_actor (actor_location_t actor,
                                                  location_write_intent_t intent) override
    {
        const auto key = location_key_codec_t::encode_actor_key (
          actor_location_key_t{actor.actor_id});
        return completed (
          write (_actors, key, std::move (actor), intent, location_kind_t::actor, std::nullopt));
    }

    task_t<location_write_result_t> remove_actor (actor_location_key_t key,
                                                  location_owner_token_t owner) override
    {
        return completed (remove (_actors, location_key_codec_t::encode_actor_key (key),
                                  std::move (owner), location_kind_t::actor, std::nullopt));
    }

    task_t<std::optional<actor_location_t>> resolve_actor (actor_location_key_t key) override
    {
        std::lock_guard lock (_gate);
        const auto found = _actors.rows.find (location_key_codec_t::encode_actor_key (key));
        return completed (found == _actors.rows.end () ? std::optional<actor_location_t>{}
                                                        : std::optional<actor_location_t>{found->second});
    }

    task_t<location_page_t<actor_location_t>>
    list_actors (actor_location_filter_t filter, location_page_request_t page = {}) override
    {
        return completed (page_rows (
          _actors,
          [&] (const actor_location_t &row) { return matches (row, filter); },
          page));
    }

    task_t<location_write_result_t> update_route (route_location_t route,
                                                  location_write_intent_t intent) override
    {
        const auto key = location_key_codec_t::encode_route_key (
          route_location_key_t{route.route_kind, route.route_key});
        return completed (
          write (_routes, key, std::move (route), intent, location_kind_t::route, std::nullopt));
    }

    task_t<location_write_result_t> remove_route (route_location_key_t key,
                                                  location_owner_token_t owner) override
    {
        return completed (remove (_routes, location_key_codec_t::encode_route_key (key),
                                  std::move (owner), location_kind_t::route, std::nullopt));
    }

    task_t<std::optional<route_location_t>> resolve_route (route_location_key_t key) override
    {
        std::lock_guard lock (_gate);
        const auto found = _routes.rows.find (location_key_codec_t::encode_route_key (key));
        return completed (found == _routes.rows.end () ? std::optional<route_location_t>{}
                                                        : std::optional<route_location_t>{found->second});
    }

    task_t<location_page_t<route_location_t>>
    list_routes (route_location_filter_t filter, location_page_request_t page = {}) override
    {
        return completed (page_rows (
          _routes,
          [&] (const route_location_t &row) { return matches (row, filter); },
          page));
    }

    task_t<owner_lease_renewal_t> renew_owner_lease (std::string owner_id,
                                                     zlink::routing_id_t node_rid,
                                                     std::chrono::milliseconds lease_ttl) override
    {
        std::lock_guard lock (_gate);
        const auto now = clock_t::now ();
        const auto expires_at = now + lease_ttl;
        _leases[owner_id] = owner_lease_t{owner_id, std::move (node_rid), expires_at, now};
        return completed (owner_lease_renewal_t{expires_at, now});
    }

    task_t<bool> remove_owner_lease (std::string owner_id) override
    {
        std::lock_guard lock (_gate);
        return completed (_leases.erase (owner_id) > 0);
    }

    task_t<owner_lease_snapshot_t> list_owner_leases () override
    {
        std::lock_guard lock (_gate);
        owner_lease_snapshot_t snapshot;
        snapshot.store_now = clock_t::now ();
        for (const auto &[_, lease] : _leases) {
            snapshot.leases.push_back (lease);
        }
        return completed (std::move (snapshot));
    }

    task_t<std::int64_t> get_change_stamp (location_change_stamp_scope_t scope) override
    {
        std::lock_guard lock (_gate);
        const auto found = _stamps.find (stamp_key (scope));
        return completed (found == _stamps.end () ? 0 : found->second);
    }

    task_t<std::int64_t> remove_all_by_owner (std::string owner_id) override
    {
        std::lock_guard lock (_gate);
        std::int64_t removed = 0;
        removed += remove_by_owner_locked (
          _peers, owner_id, location_kind_t::peer,
          [] (const peer_location_t &row) { return std::optional<std::string> (row.mesh_name); });
        removed += remove_by_owner_locked (
          _spots, owner_id, location_kind_t::spot,
          [] (const spot_location_t &row) { return std::optional<std::string> (row.mesh_name); });
        removed += remove_by_owner_locked (
          _actors, owner_id, location_kind_t::actor,
          [] (const actor_location_t &) { return std::optional<std::string>{}; });
        removed += remove_by_owner_locked (
          _routes, owner_id, location_kind_t::route,
          [] (const route_location_t &) { return std::optional<std::string>{}; });
        return completed (removed);
    }

  private:
    using clock_t = std::chrono::system_clock;

    template <typename T> struct row_table_t
    {
        std::map<std::string, T> rows;
        std::map<std::string, std::int64_t> generations;
    };

    template <typename T> static task_t<T> completed (T value)
    {
        return task_t<T> (result_t<T>::success (std::move (value)));
    }

    template <typename T>
    location_write_result_t write (row_table_t<T> &table,
                                   const std::string &key,
                                   T row,
                                   location_write_intent_t intent,
                                   location_kind_t kind,
                                   std::optional<std::string> mesh_name)
    {
        std::lock_guard lock (_gate);
        const auto now = clock_t::now ();
        const auto found = table.rows.find (key);
        const auto exists = found != table.rows.end ();
        if (intent == location_write_intent_t::new_claim && exists
            && owner_is_live (found->second.owner_id, now)) {
            return {location_write_status_t::rejected_conflict, 0, {}};
        }
        if (intent == location_write_intent_t::renew) {
            if (!exists || found->second.owner_id != row.owner_id
                || found->second.generation != row.generation) {
                return {location_write_status_t::ignored_stale, 0, {}};
            }
            row.updated_at = now;
            table.rows[key] = row;
            bump (kind, std::move (mesh_name));
            return location_write_result_t::stored (row.generation, now);
        }

        const auto next_generation = table.generations[key] + 1;
        table.generations[key] = next_generation;
        row.generation = next_generation;
        row.updated_at = now;
        table.rows[key] = std::move (row);
        bump (kind, std::move (mesh_name));
        return location_write_result_t::stored (next_generation, now);
    }

    template <typename T>
    location_write_result_t remove (row_table_t<T> &table,
                                    const std::string &key,
                                    location_owner_token_t owner,
                                    location_kind_t kind,
                                    std::optional<std::string> mesh_name)
    {
        std::lock_guard lock (_gate);
        const auto found = table.rows.find (key);
        if (found == table.rows.end () || found->second.owner_id != owner.owner_id
            || found->second.generation != owner.generation) {
            return {location_write_status_t::ignored_stale, 0, {}};
        }
        table.rows.erase (found);
        bump (kind, std::move (mesh_name));
        return location_write_result_t::stored (owner.generation, clock_t::now ());
    }

    template <typename T, typename MeshOf>
    std::int64_t remove_by_owner_locked (row_table_t<T> &table,
                                         const std::string &owner_id,
                                         location_kind_t kind,
                                         MeshOf mesh_of)
    {
        std::vector<std::string> removed_keys;
        for (const auto &[key, row] : table.rows) {
            if (row.owner_id == owner_id) {
                removed_keys.push_back (key);
            }
        }
        for (const auto &key : removed_keys) {
            auto mesh_name = mesh_of (table.rows[key]);
            table.rows.erase (key);
            bump (kind, std::move (mesh_name));
        }
        return static_cast<std::int64_t> (removed_keys.size ());
    }

    template <typename T, typename Matches>
    location_page_t<T>
    page_rows (const row_table_t<T> &table, Matches matches, location_page_request_t page)
    {
        std::lock_guard lock (_gate);
        std::vector<T> matched;
        for (const auto &[_, row] : table.rows) {
            if (matches (row)) {
                matched.push_back (row);
            }
        }
        const auto offset = page.continuation_token ? parse_offset (*page.continuation_token) : 0;
        const auto page_size =
          page.page_size > 0 ? static_cast<std::size_t> (page.page_size) : matched.size ();

        location_page_t<T> result;
        for (std::size_t i = offset; i < matched.size () && result.items.size () < page_size; ++i) {
            result.items.push_back (matched[i]);
        }
        const auto next = offset + result.items.size ();
        if (next < matched.size ()) {
            result.continuation_token = std::to_string (next);
        }
        return result;
    }

    bool owner_is_live (const std::string &owner_id, clock_t::time_point now) const
    {
        const auto found = _leases.find (owner_id);
        return found != _leases.end () && found->second.lease_expires_at > now;
    }

    void bump (location_kind_t kind, std::optional<std::string> mesh_name)
    {
        ++_stamps[stamp_key ({kind, mesh_name})];
        if (mesh_name) {
            ++_stamps[stamp_key ({kind, std::nullopt})];
        }
    }

    static std::string stamp_key (const location_change_stamp_scope_t &scope)
    {
        return std::to_string (static_cast<int> (scope.kind)) + "|"
               + scope.mesh_name.value_or (std::string{});
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

    static bool matches (const peer_location_t &row, const peer_location_filter_t &filter)
    {
        return (!filter.auto_connect_type || row.auto_connect_type == *filter.auto_connect_type)
               && (!filter.mesh_name || row.mesh_name == *filter.mesh_name)
               && (!filter.role || row.role == *filter.role)
               && (!filter.node_rid || (row.node_rid && *row.node_rid == *filter.node_rid))
               && (!filter.endpoint || row.endpoint == *filter.endpoint);
    }

    static bool matches (const spot_location_t &row, const spot_location_filter_t &filter)
    {
        return (!filter.mesh_name || row.mesh_name == *filter.mesh_name)
               && (!filter.spot_type || (row.spot_type && *row.spot_type == *filter.spot_type))
               && (!filter.node_rid || row.node_rid == *filter.node_rid)
               && (!filter.spot_kind || row.spot_kind == *filter.spot_kind);
    }

    static bool matches (const actor_location_t &row, const actor_location_filter_t &filter)
    {
        return (!filter.actor_type || (row.actor_type && *row.actor_type == *filter.actor_type))
               && (!filter.node_rid || row.node_rid == *filter.node_rid)
               && (!filter.spot_rid || (row.spot_rid && *row.spot_rid == *filter.spot_rid))
               && (!filter.location_kind || row.location_kind == *filter.location_kind);
    }

    static bool matches (const route_location_t &row, const route_location_filter_t &filter)
    {
        return (!filter.route_kind || row.route_kind == *filter.route_kind)
               && (!filter.owner_node_rid || row.owner_node_rid == *filter.owner_node_rid)
               && (!filter.owner_id || row.owner_id == *filter.owner_id);
    }

    mutable std::mutex _gate;
    std::map<std::string, owner_lease_t> _leases;
    row_table_t<peer_location_t> _peers;
    row_table_t<spot_location_t> _spots;
    row_table_t<actor_location_t> _actors;
    row_table_t<route_location_t> _routes;
    std::map<std::string, std::int64_t> _stamps;
};

} // namespace zlink::framework::runtime
