/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include <array>

#include "runtime/locations/location_key_codec.hpp"
#include "runtime/locations/live_location_reader.hpp"
#include "runtime/locations/location_runtime.hpp"
#include "runtime/locations/location_value_codec.hpp"
#include "runtime/locations/spot_address_resolvers.hpp"

#include <zlink/framework/contracts/locations/resolvers.hpp>
#include <zlink/framework/contracts/locations/runtime_query.hpp>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <map>
#include <memory>
#include <mutex>
#include <string_view>
#include <utility>

namespace zlink::framework::runtime
{

class actor_location_observer_t
{
  public:
    bool accepts (const actor_location_t &row)
    {
        std::lock_guard lock (_gate);
        const auto key = location_key_codec_t::encode_actor_key (
          actor_location_key_t{row.mesh_name, row.actor_id});
        const auto version = std::pair{row.membership_epoch, row.actor_ref.generation ()};
        auto &observed = _generations[key];
        if (version < observed) {
            return false;
        }
        observed = version;
        return !row.actor_ref.empty ();
    }

  private:
    std::mutex _gate;
    std::map<std::string, std::pair<std::uint64_t, std::uint64_t>> _generations;
};

class store_location_resolvers_t final : public peer_location_resolver_t,
                                         public spot_address_resolver_t,
                                         public actor_address_resolver_t,
                                         public location_readiness_t
{
  public:
    explicit store_location_resolvers_t (
      location_store_t &store,
      location_options_t options = {},
      std::shared_ptr<actor_location_observer_t> actor_locations =
        std::make_shared<actor_location_observer_t> (),
      std::string actor_mesh_name = {}) :
        _owned_reader (std::make_unique<live_location_reader_t> (store, options)),
        _store (_owned_reader.get ()), _options (std::move (options)),
        _actor_locations (std::move (actor_locations)),
        _actor_mesh_name (std::move (actor_mesh_name))
    {
    }

    void set_actor_mesh_name (std::string mesh_name)
    {
        _actor_mesh_name = std::move (mesh_name);
    }

    explicit store_location_resolvers_t (
      live_location_reader_t &store,
      location_options_t options = {},
      std::shared_ptr<actor_location_observer_t> actor_locations =
        std::make_shared<actor_location_observer_t> (),
      std::string actor_mesh_name = {}) :
        _store (&store), _options (std::move (options)),
        _actor_locations (std::move (actor_locations)),
        _actor_mesh_name (std::move (actor_mesh_name))
    {
    }

    task_t<std::vector<peer_location_t>> list_live_peers (peer_location_filter_t filter) override
    {
        return completed (_store->list_peers (std::move (filter)).result ().value ());
    }

    task_t<std::optional<spot_address_t>>
    resolve_spot_address (std::string, std::string spot_id) override
    {
        if (auto cached = cached_route (_spot_routes, spot_id)) {
            return completed (std::optional<spot_address_t>{std::move (*cached)});
        }
        auto row =
          _store->resolve_spot (spot_location_key_t{std::move (spot_id)})
            .result ()
            .value ();
        if (!row) {
            return completed (std::optional<spot_address_t>{});
        }
        auto address = spot_address_t{
          row->mesh_name, row->node_rid, row->spot_id, row->spot_generation};
        if (const auto authority = read_ready_authority (false, row->spot_id)) {
            if (authority_matches (*authority, row->owner_id, row->mesh_name, row->node_rid)) {
                apply_authority (address, *authority);
            }
        }
        cache_ready_route (_spot_routes, row->spot_id, address);
        return completed (std::optional<spot_address_t>{std::move (address)});
    }

    void invalidate_spot_address (std::string_view spot_id) override
    {
        std::lock_guard lock (_route_cache_gate);
        _spot_routes.erase (std::string (spot_id));
    }

    void invalidate_all_routes_after_store_recovery () override
    {
        std::lock_guard lock (_route_cache_gate);
        _spot_routes.clear ();
        _actor_routes.clear ();
        ++_store_recovery_generation;
    }

    void observe_spot_authority_version (std::string_view spot_id,
                                         std::string_view store_version,
                                         std::uint64_t object_generation,
                                         std::uint64_t authority_owner_generation)
    {
        invalidate_on_newer_authority (_spot_routes, spot_id, store_version,
                                       object_generation, authority_owner_generation);
    }

    void observe_actor_authority_version (std::string_view actor_id,
                                          std::string_view store_version,
                                          std::uint64_t object_generation,
                                          std::uint64_t authority_owner_generation)
    {
        invalidate_on_newer_authority (_actor_routes, actor_id, store_version,
                                       object_generation, authority_owner_generation);
    }

    task_t<std::optional<spot_address_t>>
    resolve_actor_address (std::string actor_id) override
    {
        if (auto cached = cached_route (_actor_routes, actor_id)) {
            return completed (std::optional<spot_address_t>{std::move (*cached)});
        }
        const auto row = _store->resolve_actor (
          actor_location_key_t{_actor_mesh_name, actor_id}).result ().value ();
        if (!row || !_actor_locations->accepts (*row)) {
            return completed (std::optional<spot_address_t>{});
        }
        auto address = spot_address_t{row->mesh_name, row->owner_node_rid,
                                      row->spot_id, row->spot_generation};
        if (const auto authority = read_ready_authority (true, actor_id)) {
            if (authority_matches (*authority, row->owner_id, row->mesh_name,
                                   row->owner_node_rid)) {
                apply_authority (address, *authority);
            }
        }
        cache_ready_route (_actor_routes, actor_id, address);
        return completed (std::optional<spot_address_t>{std::move (address)});
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
    struct cached_address_t
    {
        spot_address_t address;
        std::chrono::steady_clock::time_point expires_at;
        std::uint64_t store_recovery_generation = 0;
    };

    std::optional<spot_address_t>
    cached_route (std::map<std::string, cached_address_t> &routes,
                  std::string_view key)
    {
        if (_options.route_cache_max_age <= std::chrono::milliseconds::zero ()) {
            return std::nullopt;
        }
        std::lock_guard lock (_route_cache_gate);
        const auto found = routes.find (std::string (key));
        if (found == routes.end ()) {
            return std::nullopt;
        }
        if (std::chrono::steady_clock::now () >= found->second.expires_at
            || found->second.store_recovery_generation != _store_recovery_generation) {
            routes.erase (found);
            return std::nullopt;
        }
        return found->second.address;
    }

    void cache_ready_route (std::map<std::string, cached_address_t> &routes,
                            std::string key,
                            const spot_address_t &address)
    {
        const auto max_age = _options.route_cache_max_age;
        if (max_age <= std::chrono::milliseconds::zero ()) {
            return;
        }
        const auto measured_at = std::chrono::steady_clock::now ();
        if (address.store_version.empty () || address.object_generation == 0
            || address.authority_owner_generation == 0
            || address.owner.owner_id.empty () || address.owner.lease_generation <= 0) {
            return;
        }
        const auto lease_lifetime = _store->owner_admission_lifetime (address.owner);
        if (!lease_lifetime) {
            return;
        }
        const auto lifetime = std::min (
          std::chrono::duration_cast<std::chrono::steady_clock::duration> (max_age),
          *lease_lifetime);
        if (lifetime <= std::chrono::steady_clock::duration::zero ()) {
            return;
        }
        std::lock_guard lock (_route_cache_gate);
        routes.insert_or_assign (
          std::move (key), cached_address_t{address, measured_at + lifetime,
                                            _store_recovery_generation});
    }

    void invalidate_on_newer_authority (
      std::map<std::string, cached_address_t> &routes,
      std::string_view key,
      std::string_view store_version,
      std::uint64_t object_generation,
      std::uint64_t authority_owner_generation)
    {
        std::lock_guard lock (_route_cache_gate);
        const auto found = routes.find (std::string (key));
        if (found == routes.end ()) {
            return;
        }
        const auto &cached = found->second.address;
        const auto newer_fence = object_generation > cached.object_generation
                                 || (object_generation == cached.object_generation
                                     && authority_owner_generation
                                          > cached.authority_owner_generation);
        if (newer_fence
            || (store_version != cached.store_version
                && object_generation >= cached.object_generation
                && authority_owner_generation >= cached.authority_owner_generation)) {
            routes.erase (found);
        }
    }

    struct authority_projection_t
    {
        std::string store_version;
        std::uint64_t object_generation = 0;
        std::uint64_t authority_owner_generation = 0;
        location_owner_token_t owner;
        placement_allocation_t allocation;
    };

    static std::string authority_key (char kind, std::string_view object_id)
    {
        std::string encoded;
        encoded.reserve (object_id.size ());
        constexpr char hex[] = "0123456789ABCDEF";
        for (const auto character : object_id) {
            const auto value = static_cast<unsigned char> (character);
            if (std::isalnum (value) || value == '-' || value == '.' || value == '_'
                || value == '~') {
                encoded.push_back (static_cast<char> (value));
            }
            else {
                encoded.push_back ('%');
                encoded.push_back (hex[value >> 4]);
                encoded.push_back (hex[value & 0x0f]);
            }
        }
        return "zla1:" + std::string (1, kind) + ":"
               + std::to_string (object_id.size ()) + ":" + encoded;
    }

    std::optional<authority_projection_t>
    read_ready_authority (bool actor, std::string_view object_id)
    {
        const std::array<std::string, 3> keys = actor
          ? std::array<std::string, 3>{authority_key ('a', object_id),
                                       "actor:" + std::string (object_id),
                                       "1:" + std::string (object_id)}
          : std::array<std::string, 3>{authority_key ('s', object_id),
                                       "spot:" + std::string (object_id),
                                       "2:" + std::string (object_id)};
        for (const auto &key : keys) {
            const auto read = _store->read_authority (authority_key_t{key}).result ().value ();
            const auto *snapshot = std::get_if<authority_snapshot_t> (&read);
            if (snapshot == nullptr) {
                continue;
            }
            const auto expected_kind = actor ? placement_object_kind_t::actor
                                             : snapshot->allocation.object_kind;
            if (snapshot->allocation.state != placement_allocation_state_t::active
                || (actor && expected_kind != placement_object_kind_t::actor)
                || (!actor && expected_kind != placement_object_kind_t::user_spot
                    && expected_kind != placement_object_kind_t::instance_spot)) {
                return std::nullopt;
            }
            return authority_projection_t{snapshot->store_version,
                                          snapshot->object_generation,
                                          snapshot->authority_owner_generation,
                                          snapshot->owner,
                                          snapshot->allocation};
        }
        return std::nullopt;
    }

    static bool authority_matches (const authority_projection_t &authority,
                                   const std::string &owner_id,
                                   const std::string &mesh_name,
                                   const zlink::routing_id_t &node_rid)
    {
        return authority.owner.owner_id == owner_id
               && authority.allocation.target.mesh_name == mesh_name
               && authority.allocation.target.node_rid.value () == node_rid.to_string ();
    }

    static void apply_authority (spot_address_t &address,
                                 const authority_projection_t &authority)
    {
        address.store_version = authority.store_version;
        address.object_generation = authority.object_generation;
        address.authority_owner_generation = authority.authority_owner_generation;
        address.owner = authority.owner;
    }

    template <typename T> static task_t<T> completed (T value)
    {
        return task_t<T> (result_t<T>::success (std::move (value)));
    }

    static std::optional<spot_address_t>
    resolve_actor_row (live_location_reader_t &store,
                       const std::string &mesh_name,
                       const std::string &actor_id,
                       const std::shared_ptr<actor_location_observer_t> &actor_locations)
    {
        auto row =
          store.resolve_actor (actor_location_key_t{mesh_name, actor_id}).result ().value ();
        if (!row || !actor_locations->accepts (*row)) {
            return std::nullopt;
        }
        return spot_address_t{row->mesh_name, row->owner_node_rid, row->spot_id,
                              row->spot_generation};
    }

    static std::optional<spot_address_t>
    resolve_spot_row (live_location_reader_t &store, const std::string &spot_id)
    {
        auto row =
          store.resolve_spot (spot_location_key_t{spot_id}).result ().value ();
        if (!row) {
            return std::nullopt;
        }
        return spot_address_t{row->mesh_name, row->node_rid, row->spot_id,
                              row->spot_generation};
    }

    static std::string router_channel_for (const location_options_t &options,
                                            const std::string &mesh_name)
    {
        const auto found = options.spot_router_channels.find (mesh_name);
        return found == options.spot_router_channels.end () ? mesh_name : found->second;
    }

    std::string router_channel_for (const std::string &mesh_name) const
    {
        return router_channel_for (_options, mesh_name);
    }

    std::unique_ptr<live_location_reader_t> _owned_reader;
    live_location_reader_t *_store;
    location_options_t _options;
    std::shared_ptr<actor_location_observer_t> _actor_locations;
    std::string _actor_mesh_name;
    std::mutex _route_cache_gate;
    std::map<std::string, cached_address_t> _spot_routes;
    std::map<std::string, cached_address_t> _actor_routes;
    std::uint64_t _store_recovery_generation = 0;
};

class store_location_runtime_query_t final : public location_runtime_query_t
{
  public:
    store_location_runtime_query_t (
      location_store_t &store,
      location_runtime_t &runtime,
      const location_options_t &options,
      std::shared_ptr<actor_location_observer_t> actor_locations =
        std::make_shared<actor_location_observer_t> ()) :
        _owned_reader (std::make_unique<live_location_reader_t> (store, options)),
        _store (_owned_reader.get ()), _runtime (&runtime), _options (options),
        _actor_locations (std::move (actor_locations))
    {
    }

    store_location_runtime_query_t (live_location_reader_t &store,
                                    location_runtime_t &runtime,
                                    const location_options_t &options,
                                    std::shared_ptr<actor_location_observer_t> actor_locations =
                                      std::make_shared<actor_location_observer_t> ()) :
        _store (&store), _runtime (&runtime), _options (options),
        _actor_locations (std::move (actor_locations))
    {
    }

    task_t<location_runtime_status_t> get_status () override
    {
        location_runtime_status_t value;
        value.watch_enabled = false;
        value.polling_interval = _options.polling_interval;
        value.owner_lease_healthy = _runtime->owner_lease_healthy ();
        value.owner_lease_renewed_at = _runtime->owner_lease_renewed_at ();
        value.last_refresh_at = value.owner_lease_renewed_at;
        value.last_error = _runtime->last_error ();
        value.store_healthy = !value.last_error.has_value ();
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
        auto result = _store->list_actors (std::move (filter), page).result ().value ();
        std::erase_if (result.items,
                       [actor_locations = _actor_locations] (const actor_location_t &row) {
                           return !actor_locations->accepts (row);
                       });
        return completed (std::move (result));
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
        const auto kind = filter.kind.value_or (location_kind_t::peer);
        if (kind == location_kind_t::peer) {
            auto rows = _store
                          ->list_raw_peers (peer_location_filter_t{
                            .mesh_name = filter.mesh_name,
                            .role = filter.role,
                            .node_rid = filter.node_rid})
                          .result ()
                          .value ();
            std::vector<location_topology_entry_t> entries;
            entries.reserve (rows.size ());
            const auto live_owners = _store->live_owner_ids ();
            for (const auto &row : rows) {
                const auto live = live_owners.contains (row.owner_id);
                location_topology_entry_t entry{
                  .kind = location_kind_t::peer,
                  .mesh_name = row.mesh_name,
                  .role = row.role,
                  .node_rid = row.node_rid,
                  .endpoint = row.endpoint,
                  .state = live ? location_topology_state_t::ready
                                : location_topology_state_t::lost,
                  .desired_count = 1,
                  .ready_count = live ? 1u : 0u,
                  .updated_at = row.updated_at};
                if (matches (entry, filter)) {
                    entries.push_back (std::move (entry));
                }
            }
            return completed (page_in_memory (std::move (entries), page));
        }
        if (kind == location_kind_t::spot) {
            auto rows = _store
                          ->list_raw_spots (spot_location_filter_t{
                                             .mesh_name = filter.mesh_name,
                                             .node_rid = filter.node_rid},
                                           page)
                          .result ()
                          .value ();
            const auto live_owners = _store->live_owner_ids ();
            return completed (project_page (
              std::move (rows), filter, [&live_owners] (const spot_location_t &row) {
                  const auto live = live_owners.contains (row.owner_id);
                  return location_topology_entry_t{
                    .kind = location_kind_t::spot,
                    .mesh_name = row.mesh_name,
                    .node_rid = row.node_rid,
                    .spot_id = row.spot_id,
                    .endpoint = row.route_endpoint,
                    .state = live ? location_topology_state_t::ready
                                  : location_topology_state_t::lost,
                    .desired_count = 1,
                    .ready_count = live ? 1u : 0u,
                    .updated_at = row.updated_at};
              }));
        }
        if (kind == location_kind_t::actor) {
            auto rows = _store
                          ->list_raw_actors (
                            actor_location_filter_t{.mesh_name = filter.mesh_name,
                                                    .owner_node_rid = filter.node_rid},
                                            page)
                          .result ()
                          .value ();
            std::erase_if (rows.items, [this] (const actor_location_t &row) {
                return row.actor_ref.empty () || !_actor_locations->accepts (row);
            });
            const auto live_owners = _store->live_owner_ids ();
            return completed (project_page (
              std::move (rows), filter, [&live_owners] (const actor_location_t &row) {
                  const auto live = live_owners.contains (row.owner_id);
                  return location_topology_entry_t{
                    .kind = location_kind_t::actor,
                    .mesh_name = row.mesh_name,
                    .node_rid = row.owner_node_rid,
                    .spot_id = row.spot_id,
                    .actor_id = row.actor_id,
                    .state = live ? location_topology_state_t::ready
                                  : location_topology_state_t::lost,
                    .desired_count = 1,
                    .ready_count = live ? 1u : 0u,
                    .updated_at = row.updated_at};
              }));
        }
        auto rows = _store
                      ->list_raw_routes (route_location_filter_t{.owner_node_rid = filter.node_rid},
                                        page)
                      .result ()
                      .value ();
        const auto live_owners = _store->live_owner_ids ();
        return completed (project_page (
          std::move (rows), filter, [&live_owners] (const route_location_t &row) {
              const auto live = live_owners.contains (row.owner_id);
              return location_topology_entry_t{
                .kind = location_kind_t::route,
                .node_rid = row.owner_node_rid,
                .state = live ? location_topology_state_t::ready
                              : location_topology_state_t::lost,
                .desired_count = 1,
                .ready_count = live ? 1u : 0u,
                .updated_at = row.updated_at};
          }));
    }

    task_t<std::vector<location_service_summary_t>>
    list_service_summaries (location_service_summary_filter_t filter) override
    {
        auto peers = _store->list_raw_peers (peer_location_filter_t{}).result ().value ();
        const auto live_owners = _store->live_owner_ids ();
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
            if (live_owners.contains (peer.owner_id)) {
                ++summary.ready_count;
            } else {
                ++summary.lost_count;
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

    std::unique_ptr<live_location_reader_t> _owned_reader;
    live_location_reader_t *_store;
    location_runtime_t *_runtime;
    location_options_t _options;
    std::shared_ptr<actor_location_observer_t> _actor_locations;

    static bool matches (const location_topology_entry_t &entry,
                         const location_topology_filter_t &filter)
    {
        return (!filter.kind || entry.kind == *filter.kind)
               && (!filter.mesh_name || (entry.mesh_name && *entry.mesh_name == *filter.mesh_name))
               && (!filter.role || (entry.role && *entry.role == *filter.role))
               && (!filter.node_rid || (entry.node_rid && *entry.node_rid == *filter.node_rid))
               && (!filter.state || entry.state == *filter.state);
    }

    static location_page_t<location_topology_entry_t>
    page_in_memory (std::vector<location_topology_entry_t> entries,
                    const location_page_request_t &page)
    {
        location_page_t<location_topology_entry_t> result;
        const auto offset = page.continuation_token ? parse_offset (*page.continuation_token) : 0;
        const auto page_size =
          page.page_size > 0 ? static_cast<std::size_t> (page.page_size) : entries.size ();
        for (std::size_t i = offset; i < entries.size () && result.items.size () < page_size; ++i) {
            result.items.push_back (std::move (entries[i]));
        }
        const auto next = offset + result.items.size ();
        if (next < entries.size ()) {
            result.continuation_token = std::to_string (next);
        }
        return result;
    }

    template <typename TRow, typename Project>
    static location_page_t<location_topology_entry_t>
    project_page (location_page_t<TRow> rows,
                  const location_topology_filter_t &filter,
                  Project project)
    {
        location_page_t<location_topology_entry_t> result;
        result.continuation_token = std::move (rows.continuation_token);
        result.items.reserve (rows.items.size ());
        for (const auto &row : rows.items) {
            auto entry = project (row);
            if (matches (entry, filter)) {
                result.items.push_back (std::move (entry));
            }
        }
        return result;
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
