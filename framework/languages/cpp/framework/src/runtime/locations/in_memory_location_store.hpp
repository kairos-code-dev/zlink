/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include "runtime/locations/location_key_codec.hpp"

#include <zlink/framework/contracts/locations/stores.hpp>

#include <algorithm>
#include <limits>
#include <map>
#include <mutex>
#include <set>

namespace zlink::framework::runtime
{

class in_memory_location_store_t final : public location_store_t,
                                         public location_change_stamp_store_t
{
  public:
    in_memory_location_store_t () = default;

    explicit in_memory_location_store_t (
      std::uint64_t initial_store_revision) :
        _store_revision (initial_store_revision)
    {
    }

    task_t<location_write_result_t> update_mesh_node (
      mesh_node_descriptor_t descriptor,
      location_write_intent_t intent) override
    {
        if (!valid_mesh_node_descriptor (descriptor))
            throw std::invalid_argument (
              "mesh node descriptor is incomplete");
        std::lock_guard lock (_gate);
        const auto now = clock_t::now ();
        const auto key = mesh_node_key (
          descriptor.mesh_name, descriptor.rid);
        const auto found = _mesh_nodes.find (key);
        const auto token = location_owner_token_t{
          descriptor.owner_id, descriptor.lease_generation};
        if (!owner_token_is_live (token, now))
            return completed (location_write_result_t{
              location_write_status_t::ignored_stale, 0, {}});

        if (intent == location_write_intent_t::new_claim) {
            if (found != _mesh_nodes.end ())
                return completed (location_write_result_t{
                  location_write_status_t::rejected_conflict, 0, {}});
        } else if (intent == location_write_intent_t::renew) {
            if (found == _mesh_nodes.end ()
                || found->second.owner_id != descriptor.owner_id
                || found->second.lease_generation
                     != descriptor.lease_generation
                || descriptor.descriptor_revision
                     < found->second.descriptor_revision)
                return completed (location_write_result_t{
                  location_write_status_t::ignored_stale, 0, {}});
            if (!same_mesh_node_identity (
                  found->second, descriptor))
                return completed (location_write_result_t{
                  location_write_status_t::rejected_conflict,
                  0,
                  {}});
            if (descriptor.descriptor_revision
                  == found->second.descriptor_revision) {
                if (!same_mesh_node_descriptor (
                      found->second, descriptor))
                    return completed (location_write_result_t{
                      location_write_status_t::rejected_conflict,
                      0,
                      {}});
                return completed (
                  location_write_result_t::stored (
                    static_cast<std::int64_t> (
                      descriptor.descriptor_revision),
                    found->second.updated_at));
            }
        } else {
            return completed (location_write_result_t{
              location_write_status_t::rejected_conflict, 0, {}});
        }

        descriptor.updated_at = now;
        _mesh_nodes[key] = descriptor;
        return completed (
          location_write_result_t::stored (
            static_cast<std::int64_t> (
              descriptor.descriptor_revision),
            now));
    }

    task_t<location_write_status_t> remove_mesh_node (
      mesh_node_descriptor_key_t key,
      location_owner_token_t owner) override
    {
        std::lock_guard lock (_gate);
        const auto found = _mesh_nodes.find (
          mesh_node_key (key.mesh_name, key.rid));
        if (found == _mesh_nodes.end ()
            || found->second.owner_id != owner.owner_id
            || found->second.lease_generation
                 != owner.lease_generation)
            return completed (
              location_write_status_t::ignored_stale);
        _mesh_nodes.erase (found);
        return completed (location_write_status_t::stored);
    }

    task_t<location_page_t<mesh_node_descriptor_t>>
    list_mesh_nodes (std::string mesh_name,
                     location_page_request_t page = {}) override
    {
        std::lock_guard lock (_gate);
        std::vector<mesh_node_descriptor_t> matched;
        for (const auto &[_, descriptor] : _mesh_nodes) {
            if (descriptor.mesh_name == mesh_name)
                matched.push_back (descriptor);
        }
        const auto offset =
          page.continuation_token
            ? parse_offset (*page.continuation_token)
            : 0;
        const auto page_size =
          page.page_size > 0
            ? static_cast<std::size_t> (page.page_size)
            : matched.size ();
        location_page_t<mesh_node_descriptor_t> result;
        for (std::size_t index = offset;
             index < matched.size ()
             && result.items.size () < page_size;
             ++index)
            result.items.push_back (matched[index]);
        const auto next = offset + result.items.size ();
        if (next < matched.size ())
            result.continuation_token = std::to_string (next);
        return completed (std::move (result));
    }

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
        const auto mesh_name = actor.mesh_name;
        const auto key = location_key_codec_t::encode_actor_key (
          actor_location_key_t{actor.mesh_name, actor.actor_id});
        return completed (
          write_actor (key, std::move (actor), intent, mesh_name));
    }

    task_t<location_write_result_t> remove_actor (actor_location_key_t key,
                                                  location_owner_token_t owner) override
    {
        return completed (remove_actor_row (location_key_codec_t::encode_actor_key (key),
                                            std::move (owner), key.mesh_name));
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

    task_t<owner_lease_claim_result_t> claim_owner_lease (
      std::string owner_id,
      std::chrono::milliseconds lease_ttl) override
    {
        if (owner_id.empty () || lease_ttl.count () <= 0)
            throw std::invalid_argument (
              "owner lease claim is incomplete");
        std::lock_guard lock (_gate);
        const auto now = clock_t::now ();
        const auto existing = _leases.find (owner_id);
        if (existing != _leases.end ()
            && existing->second.lease_expires_at > now)
            return completed (
              owner_lease_claim_result_t{
                owner_lease_conflict_t{}});
        if (_lease_generation >= max_generation)
            return completed (
              owner_lease_claim_result_t{
                owner_lease_generation_exhausted_t{}});
        if (existing != _leases.end ())
            _leases.erase (existing);
        ++_lease_generation;
        const auto generation =
          static_cast<std::int64_t> (_lease_generation);
        const auto expires_at = now + lease_ttl;
        _active_lease_generations[owner_id] = generation;
        _leases[owner_id] = owner_lease_t{
          owner_id,
          zlink::routing_id_t::from (std::uint32_t{0}),
          expires_at,
          now};
        return completed (
          owner_lease_claim_result_t{
            owner_lease_claimed_t{
              {std::move (owner_id), generation},
              expires_at,
              now}});
    }

    task_t<owner_lease_read_result_t> read_owner_lease (
      std::string owner_id) override
    {
        std::lock_guard lock (_gate);
        const auto now = clock_t::now ();
        const auto lease = _leases.find (owner_id);
        const auto generation =
          _active_lease_generations.find (owner_id);
        if (lease == _leases.end ()
            || generation == _active_lease_generations.end ()
            || lease->second.lease_expires_at <= now) {
            if (lease != _leases.end ())
                _leases.erase (lease);
            _active_lease_generations.erase (owner_id);
            return completed (
              owner_lease_read_result_t{
                owner_lease_missing_t{}});
        }
        return completed (
          owner_lease_read_result_t{
            owner_lease_found_t{
              {std::move (owner_id), generation->second},
              lease->second.lease_expires_at,
              now}});
    }

    task_t<owner_lease_renew_result_t> renew_owner_lease (
      location_owner_token_t token,
      std::chrono::milliseconds lease_ttl) override
    {
        if (lease_ttl.count () <= 0)
            throw std::invalid_argument (
              "owner lease TTL must be positive");
        std::lock_guard lock (_gate);
        const auto now = clock_t::now ();
        const auto lease = _leases.find (token.owner_id);
        if (!owner_token_is_live (token, now))
            return completed (
              owner_lease_renew_result_t{
                owner_lease_stale_t{}});
        const auto expires_at = now + lease_ttl;
        lease->second.lease_expires_at = expires_at;
        lease->second.updated_at = now;
        return completed (
          owner_lease_renew_result_t{
            owner_lease_renewed_t{expires_at, now}});
    }

    task_t<owner_lease_release_result_t> release_owner_lease (
      location_owner_token_t token) override
    {
        std::lock_guard lock (_gate);
        const auto now = clock_t::now ();
        if (!owner_token_is_live (token, now))
            return completed (
              owner_lease_release_result_t{
                owner_lease_stale_t{}});
        _leases.erase (token.owner_id);
        _active_lease_generations.erase (token.owner_id);
        return completed (
          owner_lease_release_result_t{
            owner_lease_released_t{}});
    }

    task_t<owner_lease_renewal_t> renew_owner_lease (std::string owner_id,
                                                     zlink::routing_id_t node_rid,
                                                     std::chrono::milliseconds lease_ttl) override
    {
        std::lock_guard lock (_gate);
        const auto now = clock_t::now ();
        const auto expires_at = now + lease_ttl;
        const auto existing = _leases.find (owner_id);
        if (existing == _leases.end ()
            || existing->second.lease_expires_at <= now) {
            if (_lease_generation
                < static_cast<std::uint64_t> (
                  std::numeric_limits<std::int64_t>::max ()))
                ++_lease_generation;
            _active_lease_generations[owner_id] =
              static_cast<std::int64_t> (_lease_generation);
        }
        _leases[owner_id] = owner_lease_t{owner_id, std::move (node_rid), expires_at, now};
        return completed (owner_lease_renewal_t{expires_at, now});
    }

    task_t<bool> remove_owner_lease (std::string owner_id) override
    {
        std::lock_guard lock (_gate);
        _active_lease_generations.erase (owner_id);
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

    task_t<authority_read_result_t> read_authority (
      authority_key_t key,
      std::stop_token cancellation = {}) override
    {
        if (cancellation.stop_requested ())
            return cancelled<authority_read_result_t> ();
        std::lock_guard lock (_gate);
        const auto now = clock_t::now ();
        const auto found = _authorities.find (key.value);
        if (found == _authorities.end ())
            return completed (
              authority_read_result_t{authority_missing_t{now}});
        auto snapshot = found->second;
        snapshot.store_now = now;
        return completed (
          authority_read_result_t{std::move (snapshot)});
    }

    task_t<authority_compare_exchange_result_t>
    compare_exchange_authority (
      authority_key_t key,
      std::string expected_store_version,
      authority_mutation_t mutation,
      std::stop_token cancellation = {}) override
    {
        if (cancellation.stop_requested ())
            return cancelled<authority_compare_exchange_result_t> ();
        std::lock_guard lock (_gate);
        const auto now = clock_t::now ();
        auto found = _authorities.find (key.value);
        if (found == _authorities.end ()
            || found->second.store_version != expected_store_version) {
            authority_read_result_t current =
              found == _authorities.end ()
                ? authority_read_result_t{authority_missing_t{now}}
                : authority_read_result_t{found->second};
            return completed (
              authority_compare_exchange_result_t{
                authority_conflict_t{std::move (current)}});
        }

        if (std::holds_alternative<authority_delete_t> (mutation)) {
            if (found == _authorities.end ())
                return completed (
                  authority_compare_exchange_result_t{
                    authority_conflict_t{
                      authority_missing_t{now}}});
            const auto capacity_key =
              allocation_capacity_key (found->second.allocation);
            const auto active_count =
              _active_by_placement.find (capacity_key);
            if (found->second.allocation.capacity_state
                  != placement_capacity_state_t::active
                || !owner_token_is_live (found->second.owner, now)
                || active_count == _active_by_placement.end ()
                || active_count->second
                     < found->second.allocation.capacity_delta)
                return completed (
                  authority_compare_exchange_result_t{
                    authority_conflict_t{found->second}});
            if (!store_revisions_available ())
                return completed (
                  authority_compare_exchange_result_t{
                    authority_generation_exhausted_t{}});
            const auto store_version = next_store_version ();
            auto &active =
              _active_by_placement[capacity_key];
            active -= found->second.allocation.capacity_delta;
            _authorities.erase (found);
            return completed (
              authority_compare_exchange_result_t{
                authority_deleted_t{store_version, now}});
        }

        auto put = std::get<authority_put_t> (std::move (mutation));
        const auto transition = put.generation_transition;
        if ((transition == authority_generation_transition_t::preserve
             && (put.target_owner
                 || put.relocation_capacity_fence))
            || (transition == authority_generation_transition_t::new_owner
                && (!put.target_owner
                    || !put.relocation_capacity_fence)))
            throw std::invalid_argument (
              "authority owner or relocation capacity fence does not match generation transition");

        if (found == _authorities.end ()
            || found->second.allocation.capacity_state
                 != placement_capacity_state_t::active)
            return completed (
              authority_compare_exchange_result_t{
                authority_conflict_t{
                  found == _authorities.end ()
                    ? authority_read_result_t{
                        authority_missing_t{now}}
                    : authority_read_result_t{found->second}}});
        auto owner = found->second.owner;
        const auto object_generation =
          found->second.object_generation;
        auto owner_generation =
          found->second.authority_owner_generation;
        auto allocation = found->second.allocation;
        if (transition
            == authority_generation_transition_t::new_owner) {
                const auto capacity =
                  _relocation_capacity_reservations.find (
                    put.relocation_capacity_fence->value);
                if (capacity
                      == _relocation_capacity_reservations.end ()
                    || capacity->second.status
                         != relocation_reservation_status_t::reserved
                    || capacity->second.request.key.value != key.value
                    || capacity->second.request.expected_store_version
                         != found->second.store_version
                    || !same_owner (
                      capacity->second.request.source.owner,
                      found->second.owner)
                    || !same_owner (
                      capacity->second.request.target.owner,
                      *put.target_owner)
                    || !allocation_matches_source (
                      found->second.allocation,
                      capacity->second.request)
                    || !live_target_descriptor (
                      capacity->second.request.target,
                      capacity->second.request.object_kind,
                      capacity->second.request.stable_type,
                      std::nullopt,
                      now)
                    || !relocation_capacity_counters_available (
                      capacity->second))
                    return completed (
                      authority_compare_exchange_result_t{
                        authority_conflict_t{found->second}});
                if (!store_revisions_available ())
                    return completed (
                      authority_compare_exchange_result_t{
                        authority_generation_exhausted_t{}});
                if (!next_generation (_authority_owner_generation))
                    return completed (
                      authority_compare_exchange_result_t{
                        authority_generation_exhausted_t{}});
                owner_generation = _authority_owner_generation;
                owner = *put.target_owner;
                allocation = allocation_from_relocation (
                  capacity->second.request);
                consume_relocation_capacity (
                  capacity->second);
        } else {
                if (!owner_token_is_live (found->second.owner, now))
                    return completed (
                      authority_compare_exchange_result_t{
                        authority_conflict_t{found->second}});
                if (!store_revisions_available ())
                    return completed (
                      authority_compare_exchange_result_t{
                        authority_generation_exhausted_t{}});
        }

        authority_snapshot_t snapshot{
          next_store_version (),
          std::move (put.payload),
          object_generation,
          owner_generation,
          std::move (owner),
          now,
          std::move (allocation)};
        _authorities[key.value] = snapshot;
        return completed (
          authority_compare_exchange_result_t{
            authority_stored_t{std::move (snapshot)}});
    }

    task_t<authority_scan_result_t> list_authorities (
      std::string prefix,
      std::optional<authority_scan_cursor_t> cursor,
      std::size_t limit,
      std::stop_token cancellation = {}) override
    {
        if (cancellation.stop_requested ())
            return cancelled<authority_scan_result_t> ();
        if (limit == 0 || limit > 1000)
            throw std::invalid_argument (
              "authority scan limit must be between 1 and 1000");
        std::lock_guard lock (_gate);
        const auto now = clock_t::now ();
        cleanup_scans (now);

        std::string scan_id;
        std::size_t offset = 0;
        if (cursor) {
            const auto encoded = std::string (cursor->encoded ());
            const auto separator = encoded.find (':');
            if (separator == std::string::npos)
                return completed (
                  authority_scan_result_t{
                    authority_scan_expired_t{}});
            scan_id = encoded.substr (0, separator);
            try {
                offset = static_cast<std::size_t> (
                  std::stoull (encoded.substr (separator + 1)));
            }
            catch (...) {
                return completed (
                  authority_scan_result_t{
                    authority_scan_expired_t{}});
            }
        } else {
            scan_id = std::to_string (++_next_scan_id);
            authority_scan_state_t state;
            state.created_at = now;
            for (const auto &[authority_key, snapshot] : _authorities) {
                if (authority_key.starts_with (prefix))
                    state.entries.push_back (
                      {{authority_key}, snapshot});
            }
            _authority_scans.emplace (scan_id, std::move (state));
        }

        const auto scan = _authority_scans.find (scan_id);
        if (scan == _authority_scans.end ()
            || offset > scan->second.entries.size ())
            return completed (
              authority_scan_result_t{
                authority_scan_expired_t{}});

        authority_page_t page;
        std::size_t encoded_size = 0;
        while (offset < scan->second.entries.size ()
               && page.items.size () < limit) {
            const auto &entry = scan->second.entries[offset];
            const auto item_size =
              entry.key.value.size () + entry.snapshot.payload.size ();
            if (!page.items.empty ()
                && encoded_size + item_size > 4u * 1024u * 1024u)
                break;
            page.items.push_back (entry);
            encoded_size += item_size;
            ++offset;
        }
        if (offset < scan->second.entries.size ()) {
            page.next_cursor = authority_scan_cursor_t{
              scan_id + ":" + std::to_string (offset)};
        } else {
            _authority_scans.erase (scan);
        }
        return completed (
          authority_scan_result_t{std::move (page)});
    }

    task_t<object_reserve_result_t> reserve (
      object_reserve_request_t request,
      std::stop_token cancellation = {}) override
    {
        if (cancellation.stop_requested ())
            return cancelled<object_reserve_result_t> ();
        if (request.creating_payload.size () > 1024u * 1024u
            || request.intent.request_encoded_size > 1024u * 1024u)
            throw std::invalid_argument (
              "object reservation payload exceeds 1 MiB");
        std::lock_guard lock (_gate);
        const auto now = clock_t::now ();
        const auto key = object_key (request.key);
        const auto authority = _authorities.find (key);
        if (authority != _authorities.end ()) {
            const auto type = _object_types.find (key);
            if (type != _object_types.end ()
                && type->second != request.intent.stable_type)
                return completed (
                  object_reserve_result_t{
                    object_type_mismatch_t{authority->second}});
            return completed (
              object_reserve_result_t{
                object_already_exists_t{authority->second}});
        }
        const auto target_descriptor =
          live_target_descriptor (
            request.target,
            request.key.kind,
            request.intent.stable_type,
            request.intent.placement_profile,
            now);
        if (!target_descriptor)
            return completed (
              object_reserve_result_t{
                object_reserve_conflict_t{
                  authority_missing_t{now}}});
        const auto limit = pending_limit (
          *target_descriptor,
          request.key.kind,
          request.intent.stable_type);
        const auto &pending =
          _pending_by_placement[
            target_capacity_key (
              request.target,
              request.key.kind,
              request.intent.stable_type)];
        const auto active_limit_value = active_limit (
          *target_descriptor,
          request.key.kind,
          request.intent.stable_type);
        const auto &active =
          _active_by_placement[
            target_capacity_key (
              request.target,
              request.key.kind,
              request.intent.stable_type)];
        if (request.pending_capacity_delta == 0
            || request.pending_capacity_delta
                 > static_cast<std::uint32_t> (
                   std::numeric_limits<std::int32_t>::max ())
            || request.pending_capacity_delta > limit
            || pending
                 > limit - request.pending_capacity_delta
            || request.pending_capacity_delta > active_limit_value
            || active
                 > active_limit_value
                     - request.pending_capacity_delta)
            return completed (
              object_reserve_result_t{
                object_placement_capacity_exhausted_t{}});
        if (!store_revisions_available ()
            || _object_generation >= max_generation
            || _authority_owner_generation >= max_generation)
            return completed (
              object_reserve_result_t{
                authority_generation_exhausted_t{}});
        ++_object_generation;
        ++_authority_owner_generation;

        const auto store_version = next_store_version ();
        object_reservation_fence_t fence{
          "reservation-" + store_version,
          store_version,
          _object_generation,
          _authority_owner_generation,
          request.target,
          request.pending_capacity_delta};
        authority_snapshot_t creating{
          store_version,
          request.creating_payload,
          _object_generation,
          _authority_owner_generation,
          request.target.owner,
          now,
          {placement_capacity_state_t::pending,
           request.key.kind,
           request.intent.stable_type,
           request.target.mesh_name,
           request.target.node_rid,
           request.target.node_lifecycle_generation,
           request.pending_capacity_delta}};
        reservation_state_t reservation{
          request, fence, creating, reservation_status_t::prepared};
        _authorities[key] = creating;
        _object_types[key] = request.intent.stable_type;
        _reservations[key] = std::move (reservation);
        _pending_by_placement[
          target_capacity_key (
            request.target,
            request.key.kind,
            request.intent.stable_type)] +=
          request.pending_capacity_delta;
        return completed (
          object_reserve_result_t{
            object_reserved_t{std::move (fence),
                              std::move (creating)}});
    }

    task_t<object_commit_result_t> commit (
      object_commit_request_t request,
      std::stop_token cancellation = {}) override
    {
        if (cancellation.stop_requested ())
            return cancelled<object_commit_result_t> ();
        if (request.ready_payload.size () > 1024u * 1024u)
            throw std::invalid_argument (
              "object commit payload exceeds 1 MiB");
        std::lock_guard lock (_gate);
        const auto key = object_key (request.key);
        const auto reservation = _reservations.find (key);
        if (reservation == _reservations.end ())
            return completed (
              object_commit_result_t{object_commit_stale_t{}});
        if (!same_fence (reservation->second.fence, request.fence))
            return completed (
              object_commit_result_t{object_commit_stale_t{}});
        if (reservation->second.status
            == reservation_status_t::committed)
            return completed (
              object_commit_result_t{
                object_already_committed_t{
                  reservation->second.snapshot}});
        if (reservation->second.status
            == reservation_status_t::aborted)
            return completed (
              object_commit_result_t{object_commit_stale_t{}});

        auto authority = _authorities.find (key);
        if (authority == _authorities.end ()
            || authority->second.store_version
                 != request.fence.expected_store_version)
            return completed (
              object_commit_result_t{
                object_commit_conflict_t{
                  authority == _authorities.end ()
                    ? authority_read_result_t{
                        authority_missing_t{clock_t::now ()}}
                    : authority_read_result_t{authority->second}}});
        const auto now = clock_t::now ();
        const auto descriptor = live_target_descriptor (
              request.fence.target,
              reservation->second.request.key.kind,
              reservation->second.request.intent.stable_type,
              reservation->second.request.intent.placement_profile,
              now);
        const auto capacity_key = target_capacity_key (
          request.fence.target,
          reservation->second.request.key.kind,
          reservation->second.request.intent.stable_type);
        const auto pending =
          _pending_by_placement.find (capacity_key);
        const auto active =
          _active_by_placement.find (capacity_key);
        if (!descriptor
            || pending == _pending_by_placement.end ()
            || pending->second
                 < request.fence.pending_capacity_delta
            || request.fence.pending_capacity_delta
                 > active_limit (
                   *descriptor,
                   reservation->second.request.key.kind,
                   reservation->second.request.intent.stable_type)
            || (active != _active_by_placement.end ()
                && active->second
                     > active_limit (
                         *descriptor,
                         reservation->second.request.key.kind,
                         reservation->second.request.intent.stable_type)
                         - request.fence.pending_capacity_delta))
            return completed (
              object_commit_result_t{
                  object_commit_conflict_t{authority->second}});
        if (!store_revisions_available ())
            return completed (
              object_commit_result_t{
                authority_generation_exhausted_t{}});

        authority->second.store_version = next_store_version ();
        authority->second.payload = std::move (request.ready_payload);
        authority->second.store_now = now;
        authority->second.allocation.capacity_state =
          placement_capacity_state_t::active;
        reservation->second.snapshot = authority->second;
        reservation->second.status = reservation_status_t::committed;
        release_pending (reservation->second);
        _active_by_placement[
          target_capacity_key (
            request.fence.target,
            reservation->second.request.key.kind,
            reservation->second.request.intent.stable_type)] +=
          request.fence.pending_capacity_delta;
        return completed (
          object_commit_result_t{
            object_committed_t{authority->second}});
    }

    task_t<object_abort_result_t> abort (
      object_abort_request_t request,
      std::stop_token cancellation = {}) override
    {
        if (cancellation.stop_requested ())
            return cancelled<object_abort_result_t> ();
        std::lock_guard lock (_gate);
        const auto key = object_key (request.key);
        const auto reservation = _reservations.find (key);
        if (reservation == _reservations.end ())
            return completed (
              object_abort_result_t{object_abort_stale_t{}});
        if (!same_fence (reservation->second.fence, request.fence))
            return completed (
              object_abort_result_t{object_abort_stale_t{}});
        if (reservation->second.status
            == reservation_status_t::aborted)
            return completed (
              object_abort_result_t{object_already_aborted_t{}});
        if (reservation->second.status
            == reservation_status_t::committed)
            return completed (
              object_abort_result_t{object_abort_stale_t{}});

        const auto authority = _authorities.find (key);
        if (authority == _authorities.end ()
            || authority->second.store_version
                 != request.fence.expected_store_version)
            return completed (
              object_abort_result_t{
                object_abort_conflict_t{
                  authority == _authorities.end ()
                    ? authority_read_result_t{
                        authority_missing_t{clock_t::now ()}}
                    : authority_read_result_t{authority->second}}});
        _authorities.erase (authority);
        _object_types.erase (key);
        release_pending (reservation->second);
        reservation->second.status = reservation_status_t::aborted;
        return completed (
          object_abort_result_t{object_aborted_t{}});
    }

    task_t<relocation_capacity_reserve_result_t>
    reserve_relocation_capacity (
      relocation_capacity_reserve_request_t request,
      std::stop_token cancellation = {}) override
    {
        if (cancellation.stop_requested ())
            return cancelled<relocation_capacity_reserve_result_t> ();
        if (all_zero (request.reservation_id)
            || request.key.value.empty ()
            || request.expected_store_version.empty ()
            || request.stable_type.empty ()
            || request.capacity_delta == 0
            || request.capacity_delta
                 > static_cast<std::uint32_t> (
                   std::numeric_limits<std::int32_t>::max ()))
            throw std::invalid_argument (
              "relocation capacity reservation is incomplete");

        std::lock_guard lock (_gate);
        const auto now = clock_t::now ();
        const auto reservation_key =
          reservation_id_key (request.reservation_id);
        const auto existing_id =
          _relocation_capacity_by_id.find (reservation_key);
        if (existing_id != _relocation_capacity_by_id.end ()) {
            const auto existing =
              _relocation_capacity_reservations.find (
                existing_id->second);
            if (existing
                  != _relocation_capacity_reservations.end ()
                && same_relocation_capacity_request (
                  existing->second.request, request)) {
                return completed (
                  relocation_capacity_reserve_result_t{
                    relocation_capacity_already_reserved_t{
                      existing->second.fence}});
            }
            const auto authority =
              _authorities.find (request.key.value);
            return completed (
              relocation_capacity_reserve_result_t{
                relocation_capacity_conflict_t{
                  authority == _authorities.end ()
                    ? authority_read_result_t{
                        authority_missing_t{now}}
                    : authority_read_result_t{
                        authority->second}}});
        }

        const auto authority =
          _authorities.find (request.key.value);
        if (authority == _authorities.end ()
            || authority->second.store_version
                 != request.expected_store_version
            || !same_owner (
              authority->second.owner, request.source.owner)
            || !allocation_matches_source (
              authority->second.allocation, request)) {
            return completed (
              relocation_capacity_reserve_result_t{
                relocation_capacity_conflict_t{
                  authority == _authorities.end ()
                    ? authority_read_result_t{
                        authority_missing_t{now}}
                    : authority_read_result_t{
                        authority->second}}});
        }
        const auto target_descriptor =
          live_target_descriptor (
            request.target,
            request.object_kind,
            request.stable_type,
            std::nullopt,
            now);
        if (!target_descriptor)
            return completed (
              relocation_capacity_reserve_result_t{
                relocation_capacity_target_unavailable_t{}});
        const auto limit = pending_limit (
          *target_descriptor,
          request.object_kind,
          request.stable_type);
        const auto target_pending =
          _pending_by_placement[
            target_capacity_key (
              request.target,
              request.object_kind,
              request.stable_type)];
        const auto active_limit_value = active_limit (
          *target_descriptor,
          request.object_kind,
          request.stable_type);
        const auto target_active =
          _active_by_placement[
            target_capacity_key (
              request.target,
              request.object_kind,
              request.stable_type)];
        if (request.capacity_delta > limit
            || target_pending
                 > limit - request.capacity_delta
            || request.capacity_delta > active_limit_value
            || target_active
                 > active_limit_value - request.capacity_delta)
            return completed (
              relocation_capacity_reserve_result_t{
                relocation_capacity_exhausted_t{}});

        relocation_capacity_fence_t fence{
          "relocation-" + reservation_key};
        relocation_capacity_state_t state{
          request, fence, relocation_reservation_status_t::reserved};
        _pending_by_placement[
          target_capacity_key (
            request.target,
            request.object_kind,
            request.stable_type)]
          += request.capacity_delta;
        _relocation_capacity_by_id.emplace (
          reservation_key, fence.value);
        _relocation_capacity_reservations.emplace (
          fence.value, std::move (state));
        return completed (
          relocation_capacity_reserve_result_t{
            relocation_capacity_reserved_t{
              std::move (fence)}});
    }

    task_t<relocation_capacity_abort_result_t>
    abort_relocation_capacity (
      relocation_capacity_fence_t fence,
      std::stop_token cancellation = {}) override
    {
        if (cancellation.stop_requested ())
            return cancelled<relocation_capacity_abort_result_t> ();
        std::lock_guard lock (_gate);
        const auto reservation =
          _relocation_capacity_reservations.find (fence.value);
        if (reservation
            == _relocation_capacity_reservations.end ())
            return completed (
              relocation_capacity_abort_result_t::stale);
        if (reservation->second.status
            == relocation_reservation_status_t::committed)
            return completed (
              relocation_capacity_abort_result_t::
                already_committed);
        if (reservation->second.status
            == relocation_reservation_status_t::aborted)
            return completed (
              relocation_capacity_abort_result_t::
                already_aborted);
        if (reservation->second.status
            != relocation_reservation_status_t::reserved)
            return completed (
              relocation_capacity_abort_result_t::stale);
        release_relocation_pending (reservation->second);
        reservation->second.status =
          relocation_reservation_status_t::aborted;
        return completed (
          relocation_capacity_abort_result_t::aborted);
    }

    task_t<aggregate_prepare_result_t> prepare_aggregate (
      aggregate_prepare_request_t request,
      std::stop_token cancellation = {}) override
    {
        if (cancellation.stop_requested ())
            return cancelled<aggregate_prepare_result_t> ();
        std::lock_guard lock (_gate);
        if (request.aggregate_generation == 0
            || request.aggregate_generation > max_generation
            || request.participants.empty ()
            || request.participants.size () > 1024
            || all_zero (request.aggregate_id.value)
            || request.target_owner.owner_id.empty ()
            || request.target_owner.lease_generation <= 0
            || aggregate_encoded_size (request)
                 > 1024u * 1024u)
            return completed (
              aggregate_prepare_result_t{
                aggregate_prepare_conflict_t{}});

        const auto aggregate_key =
          aggregate_id_key (request.aggregate_id);
        const auto existing = _aggregates.find (aggregate_key);
        if (existing != _aggregates.end ()) {
            if (same_aggregate_request (
                  existing->second.request, request))
                return completed (
                  aggregate_prepare_result_t{
                    aggregate_already_prepared_t{
                      {request.aggregate_id,
                       request.aggregate_generation}}});
            return completed (
              aggregate_prepare_result_t{
                aggregate_prepare_stale_t{}});
        }

        std::string previous;
        for (const auto &participant : request.participants) {
            if (!previous.empty ()
                && participant.key.value <= previous)
                return completed (
                  aggregate_prepare_result_t{
                    aggregate_prepare_conflict_t{}});
            previous = participant.key.value;
            const auto authority =
              _authorities.find (participant.key.value);
            if (authority == _authorities.end ()
                || authority->second.store_version
                     != participant.expected_store_version)
                return completed (
                  aggregate_prepare_result_t{
                    aggregate_prepare_conflict_t{}});
        }
        std::map<std::string, std::string>
          new_owner_participants;
        for (const auto &participant : request.participants) {
            if (participant.owner_transition
                == authority_generation_transition_t::new_owner)
                new_owner_participants.emplace (
                  participant.key.value,
                  participant.expected_store_version);
        }
        if (request.target_reservations.size ()
            != new_owner_participants.size ())
            return completed (
              aggregate_prepare_result_t{
                aggregate_prepare_conflict_t{}});
        std::set<std::string> observed_fences;
        std::set<std::string> observed_keys;
        for (const auto &fence : request.target_reservations) {
            if (!observed_fences.insert (fence.value).second)
                return completed (
                  aggregate_prepare_result_t{
                    aggregate_prepare_conflict_t{}});
            const auto reservation =
              _relocation_capacity_reservations.find (
                fence.value);
            if (reservation
                  == _relocation_capacity_reservations.end ()
                || reservation->second.status
                     != relocation_reservation_status_t::reserved
                || !same_owner (
                  reservation->second.request.target.owner,
                  request.target_owner)
                || !observed_keys
                      .insert (
                        reservation->second.request.key.value)
                      .second)
                return completed (
                  aggregate_prepare_result_t{
                    aggregate_prepare_conflict_t{}});
            const auto participant =
              new_owner_participants.find (
                reservation->second.request.key.value);
            const auto authority =
              _authorities.find (
                reservation->second.request.key.value);
            if (participant == new_owner_participants.end ()
                || participant->second
                     != reservation->second.request
                          .expected_store_version)
                return completed (
                  aggregate_prepare_result_t{
                    aggregate_prepare_conflict_t{}});
            if (authority == _authorities.end ()
                || !same_owner (
                  authority->second.owner,
                  reservation->second.request.source.owner)
                || !allocation_matches_source (
                  authority->second.allocation,
                  reservation->second.request)
                || !live_target_descriptor (
                  reservation->second.request.target,
                  reservation->second.request.object_kind,
                  reservation->second.request.stable_type,
                  std::nullopt,
                  clock_t::now ())
                || !relocation_capacity_counters_available (
                  reservation->second))
                return completed (
                  aggregate_prepare_result_t{
                    aggregate_prepare_conflict_t{}});
        }

        aggregate_state_t state;
        state.request = std::move (request);
        _aggregates.emplace (aggregate_key, std::move (state));
        const auto &stored = _aggregates.at (aggregate_key).request;
        for (const auto &fence : stored.target_reservations) {
            auto &reservation =
              _relocation_capacity_reservations.at (fence.value);
            reservation.status =
              relocation_reservation_status_t::prepared;
            reservation.aggregate_id = stored.aggregate_id;
            reservation.aggregate_generation =
              stored.aggregate_generation;
        }
        return completed (
          aggregate_prepare_result_t{
            aggregate_prepared_t{
              {stored.aggregate_id,
               stored.aggregate_generation}}});
    }

    task_t<aggregate_commit_result_t> commit_aggregate (
      aggregate_fence_t fence,
      std::stop_token cancellation = {}) override
    {
        if (cancellation.stop_requested ())
            return cancelled<aggregate_commit_result_t> ();
        std::lock_guard lock (_gate);
        const auto aggregate =
          _aggregates.find (aggregate_id_key (fence.aggregate_id));
        if (aggregate == _aggregates.end ()
            || aggregate->second.request.aggregate_generation
                 != fence.aggregate_generation)
            return completed (
              aggregate_commit_result_t::stale);
        if (aggregate->second.status
            == aggregate_status_t::committed)
            return completed (
              aggregate_commit_result_t::already_committed);
        if (aggregate->second.status
            == aggregate_status_t::aborted)
            return completed (aggregate_commit_result_t::stale);

        const auto participant_count =
          static_cast<std::size_t> (std::count_if (
            aggregate->second.request.participants.begin (),
            aggregate->second.request.participants.end (),
            [] (const aggregate_participant_t &participant) {
                return participant.owner_transition
                       == authority_generation_transition_t::
                            new_owner;
            }));
        if (!store_revisions_available (
              aggregate->second.request.participants.size ())
            || _authority_owner_generation
            > max_generation - participant_count)
            return completed (
              aggregate_commit_result_t::generation_exhausted);
        const auto now = clock_t::now ();
        for (const auto &participant :
             aggregate->second.request.participants) {
            const auto authority =
              _authorities.find (participant.key.value);
            if (authority == _authorities.end ()
                || authority->second.store_version
                     != participant.expected_store_version)
                return completed (
                  aggregate_commit_result_t::stale);
        }
        for (const auto &fence :
             aggregate->second.request.target_reservations) {
            const auto reservation =
              _relocation_capacity_reservations.find (
                fence.value);
            if (reservation
                  == _relocation_capacity_reservations.end ()
                || reservation->second.status
                     != relocation_reservation_status_t::prepared
                || !reservation_bound_to (
                  reservation->second,
                  aggregate->second.request)
                || !live_target_descriptor (
                  reservation->second.request.target,
                  reservation->second.request.object_kind,
                  reservation->second.request.stable_type,
                  std::nullopt,
                  now))
                return completed (
                  aggregate_commit_result_t::stale);
            const auto authority = _authorities.find (
              reservation->second.request.key.value);
            if (authority == _authorities.end ()
                || !same_owner (
                  authority->second.owner,
                  reservation->second.request.source.owner)
                || !allocation_matches_source (
                  authority->second.allocation,
                  reservation->second.request)
                || !relocation_capacity_counters_available (
                  reservation->second))
                return completed (
                  aggregate_commit_result_t::stale);
        }

        for (const auto &participant :
             aggregate->second.request.participants) {
            auto &snapshot = _authorities.at (
              participant.key.value);
            snapshot.store_version = next_store_version ();
            snapshot.payload = participant.authority_payload;
            snapshot.store_now = now;
            if (participant.owner_transition
                == authority_generation_transition_t::new_owner) {
                ++_authority_owner_generation;
                snapshot.authority_owner_generation =
                  _authority_owner_generation;
                snapshot.owner =
                  aggregate->second.request.target_owner;
                const auto reservation =
                  std::find_if (
                    aggregate->second.request
                      .target_reservations.begin (),
                    aggregate->second.request
                      .target_reservations.end (),
                    [this, &participant] (
                      const relocation_capacity_fence_t &item) {
                        const auto found =
                          _relocation_capacity_reservations.find (
                            item.value);
                        return found
                                 != _relocation_capacity_reservations.end ()
                               && found->second.request.key.value
                                    == participant.key.value;
                    });
                snapshot.allocation =
                  allocation_from_relocation (
                    _relocation_capacity_reservations.at (
                      reservation->value)
                      .request);
            }
        }
        for (const auto &fence_value :
             aggregate->second.request.target_reservations) {
            const auto reservation =
              _relocation_capacity_reservations.find (
                fence_value.value);
            if (reservation
                  != _relocation_capacity_reservations.end ()
                && reservation->second.status
                     == relocation_reservation_status_t::prepared
                && reservation_bound_to (
                  reservation->second,
                  aggregate->second.request)) {
                consume_relocation_capacity (
                  reservation->second);
            }
        }
        aggregate->second.status = aggregate_status_t::committed;
        return completed (
          aggregate_commit_result_t::committed);
    }

    task_t<aggregate_abort_result_t> abort_aggregate (
      aggregate_fence_t fence,
      std::stop_token cancellation = {}) override
    {
        if (cancellation.stop_requested ())
            return cancelled<aggregate_abort_result_t> ();
        std::lock_guard lock (_gate);
        const auto aggregate =
          _aggregates.find (aggregate_id_key (fence.aggregate_id));
        if (aggregate == _aggregates.end ()
            || aggregate->second.request.aggregate_generation
                 != fence.aggregate_generation)
            return completed (aggregate_abort_result_t::stale);
        if (aggregate->second.status
            == aggregate_status_t::aborted)
            return completed (
              aggregate_abort_result_t::already_aborted);
        if (aggregate->second.status
            == aggregate_status_t::committed)
            return completed (aggregate_abort_result_t::stale);
        for (const auto &fence_value :
             aggregate->second.request.target_reservations) {
            const auto reservation =
              _relocation_capacity_reservations.find (
                fence_value.value);
            if (reservation
                  != _relocation_capacity_reservations.end ()
                && reservation->second.status
                     == relocation_reservation_status_t::prepared
                && reservation_bound_to (
                  reservation->second,
                  aggregate->second.request)) {
                release_relocation_pending (
                  reservation->second);
                reservation->second.status =
                  relocation_reservation_status_t::aborted;
            }
        }
        aggregate->second.status = aggregate_status_t::aborted;
        return completed (aggregate_abort_result_t::aborted);
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
          [] (const actor_location_t &row) {
              return std::optional<std::string>{row.mesh_name};
          });
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

    enum class reservation_status_t
    {
        prepared,
        committed,
        aborted
    };

    struct reservation_state_t
    {
        object_reserve_request_t request;
        object_reservation_fence_t fence;
        authority_snapshot_t snapshot;
        reservation_status_t status = reservation_status_t::prepared;
    };

    enum class relocation_reservation_status_t
    {
        reserved,
        prepared,
        committed,
        aborted
    };

    struct relocation_capacity_state_t
    {
        relocation_capacity_reserve_request_t request;
        relocation_capacity_fence_t fence;
        relocation_reservation_status_t status =
          relocation_reservation_status_t::reserved;
        std::optional<aggregate_id_t> aggregate_id;
        std::uint64_t aggregate_generation = 0;
    };

    enum class aggregate_status_t
    {
        prepared,
        committed,
        aborted
    };

    struct aggregate_state_t
    {
        aggregate_prepare_request_t request;
        aggregate_status_t status = aggregate_status_t::prepared;
    };

    struct authority_scan_state_t
    {
        std::vector<authority_entry_t> entries;
        clock_t::time_point created_at;
    };

    static constexpr std::uint64_t max_generation =
      static_cast<std::uint64_t> (
        std::numeric_limits<std::int64_t>::max ());
    template <typename T> static task_t<T> completed (T value)
    {
        return task_t<T> (result_t<T>::success (std::move (value)));
    }

    template <typename T> static task_t<T> cancelled ()
    {
        return task_t<T> (
          detail::boundary_failure<T> (
            detail::boundary_error_t::cancelled,
            "location store operation was cancelled"));
    }

    bool owner_token_is_live (
      const location_owner_token_t &token,
      clock_t::time_point now) const
    {
        const auto lease = _leases.find (token.owner_id);
        const auto generation =
          _active_lease_generations.find (token.owner_id);
        return lease != _leases.end ()
               && generation != _active_lease_generations.end ()
               && lease->second.lease_expires_at > now
               && generation->second == token.lease_generation;
    }

    const mesh_node_descriptor_t *live_target_descriptor (
      const object_creation_target_t &target,
      placement_object_kind_t kind,
      const std::string &stable_type,
      const std::optional<placement_profile_t> &profile,
      clock_t::time_point now) const
    {
        const auto found = _mesh_nodes.find (
          mesh_node_key (
            target.mesh_name,
            std::string (target.node_rid.value ())));
        if (found == _mesh_nodes.end ())
            return nullptr;
        const auto &descriptor = found->second;
        if (descriptor.lifecycle_generation
              != target.node_lifecycle_generation
            || descriptor.owner_id != target.owner.owner_id
            || descriptor.lease_generation
                 != target.owner.lease_generation
            || descriptor.state
                 != framework_runtime_state_t::serving
            || descriptor.object_role != object_role_t::server
            || descriptor.placement_weight == 0
            || !owner_token_is_live (target.owner, now))
            return nullptr;
        const auto capability = find_capability (
          descriptor, kind, stable_type);
        if (!capability)
            return nullptr;
        if (profile
            && capability->placement_profiles.find (
                 std::string (profile->value ()))
                 == capability->placement_profiles.end ())
            return nullptr;
        return &descriptor;
    }

    static const object_capability_t *find_capability (
      const mesh_node_descriptor_t &descriptor,
      placement_object_kind_t kind,
      const std::string &stable_type)
    {
        const auto found = std::find_if (
          descriptor.object_capabilities.begin (),
          descriptor.object_capabilities.end (),
          [&] (const object_capability_t &capability) {
              return capability.object_kind == kind
                     && capability.stable_type == stable_type;
          });
        return found == descriptor.object_capabilities.end ()
                 ? nullptr
                 : &*found;
    }

    static std::uint32_t pending_limit (
      const mesh_node_descriptor_t &descriptor,
      placement_object_kind_t kind,
      const std::string &stable_type)
    {
        const auto capability =
          find_capability (descriptor, kind, stable_type);
        return capability && capability->pending_limit
                 ? *capability->pending_limit
                 : descriptor.object_capacity.pending_limit;
    }

    static std::uint32_t active_limit (
      const mesh_node_descriptor_t &descriptor,
      placement_object_kind_t kind,
      const std::string &stable_type)
    {
        const auto capability =
          find_capability (descriptor, kind, stable_type);
        return capability && capability->active_limit
                 ? *capability->active_limit
                 : descriptor.object_capacity.active_limit;
    }

    static bool valid_mesh_node_descriptor (
      const mesh_node_descriptor_t &descriptor)
    {
        if (descriptor.mesh_name.empty ()
            || descriptor.rid.size () == 0
            || descriptor.lifecycle_generation == 0
            || descriptor.descriptor_revision == 0
            || descriptor.descriptor_revision > max_generation
            || descriptor.endpoint.empty ()
            || descriptor.application_version < 0
            || descriptor.placement_weight > 100
            || descriptor.object_capacity.active
                 > descriptor.object_capacity.active_limit
            || descriptor.object_capacity.pending
                 > descriptor.object_capacity.pending_limit
            || descriptor.object_capacity.active_limit == 0
            || descriptor.object_capacity.active_limit
                 > std::numeric_limits<std::int32_t>::max ()
            || descriptor.object_capacity.pending_limit == 0
            || descriptor.object_capacity.pending_limit
                 > std::numeric_limits<std::int32_t>::max ()
            || descriptor.security_identity.empty ()
            || descriptor.owner_id.empty ()
            || descriptor.lease_generation <= 0
            || descriptor.object_capabilities.size () > 1024
            || (descriptor.object_role != object_role_t::server
                && !descriptor.object_capabilities.empty ()))
            return false;
        std::pair<int, std::string> previous;
        bool first = true;
        for (const auto &capability :
             descriptor.object_capabilities) {
            if (capability.stable_type.empty ()
                || capability.placement_profiles.size () > 1024
                || ((capability.policy
                       == maintenance_policy_kind_t::snapshot)
                    != capability.has_snapshot_adapter)
                || (capability.active_limit
                    && (*capability.active_limit == 0
                        || *capability.active_limit
                             > std::numeric_limits<
                                 std::int32_t>::max ()))
                || (capability.pending_limit
                    && (*capability.pending_limit == 0
                        || *capability.pending_limit
                             > std::numeric_limits<
                                 std::int32_t>::max ())))
                return false;
            const auto key = std::make_pair (
              static_cast<int> (capability.object_kind),
              capability.stable_type);
            if (!first && previous >= key)
                return false;
            previous = key;
            first = false;
        }
        return true;
    }

    static bool same_capability (
      const object_capability_t &left,
      const object_capability_t &right)
    {
        return left.object_kind == right.object_kind
               && left.stable_type == right.stable_type
               && left.policy == right.policy
               && left.has_snapshot_adapter
                    == right.has_snapshot_adapter
               && left.placement_profiles
                    == right.placement_profiles
               && left.active_limit == right.active_limit
               && left.pending_limit == right.pending_limit;
    }

    static bool same_capabilities (
      const std::vector<object_capability_t> &left,
      const std::vector<object_capability_t> &right)
    {
        return left.size () == right.size ()
               && std::equal (
                 left.begin (), left.end (), right.begin (),
                 same_capability);
    }

    static bool same_mesh_node_identity (
      const mesh_node_descriptor_t &left,
      const mesh_node_descriptor_t &right)
    {
        return left.mesh_name == right.mesh_name
               && left.rid == right.rid
               && left.lifecycle_generation
                    == right.lifecycle_generation
               && left.endpoint == right.endpoint
               && left.application_version
                    == right.application_version
               && same_channel_names (
                 left.channel_weights, right.channel_weights)
               && same_capabilities (
                 left.object_capabilities,
                 right.object_capabilities)
               && left.object_role == right.object_role
               && left.object_capacity.active_limit
                    == right.object_capacity.active_limit
               && left.object_capacity.pending_limit
                    == right.object_capacity.pending_limit
               && left.security_identity
                    == right.security_identity;
    }

    static bool same_mesh_node_descriptor (
      const mesh_node_descriptor_t &left,
      const mesh_node_descriptor_t &right)
    {
        return same_mesh_node_identity (left, right)
               && left.descriptor_revision
                    == right.descriptor_revision
               && left.channel_weights == right.channel_weights
               && left.placement_weight == right.placement_weight
               && left.object_capacity.active
                    == right.object_capacity.active
               && left.object_capacity.pending
                    == right.object_capacity.pending
               && left.maintenance_wave
                    == right.maintenance_wave
               && left.state == right.state
               && left.owner_id == right.owner_id
               && left.lease_generation
                    == right.lease_generation;
    }

    static bool same_channel_names (
      const std::map<std::string, int> &left,
      const std::map<std::string, int> &right)
    {
        if (left.size () != right.size ())
            return false;
        return std::equal (
          left.begin (), left.end (), right.begin (),
          [] (const auto &l, const auto &r) {
              return l.first == r.first;
          });
    }

    static std::string mesh_node_key (
      const std::string &mesh_name,
      const zlink::routing_id_t &rid)
    {
        return mesh_node_key (mesh_name, rid.to_string ());
    }

    static std::string mesh_node_key (
      const std::string &mesh_name,
      const std::string &rid)
    {
        return mesh_name + "\x1f" + rid;
    }

    static bool next_generation (std::uint64_t &counter)
    {
        if (counter >= max_generation)
            return false;
        ++counter;
        return true;
    }

    bool store_revisions_available (
      std::size_t count = 1) const
    {
        return count <= max_generation
               && _store_revision
                    <= max_generation
                         - static_cast<std::uint64_t> (count);
    }

    std::string next_store_version ()
    {
        ++_store_revision;
        return std::to_string (_store_revision);
    }

    static std::string object_key (
      const object_creation_key_t &key)
    {
        return std::to_string (static_cast<int> (key.kind))
               + ":" + key.global_id;
    }

    static bool same_owner (
      const location_owner_token_t &left,
      const location_owner_token_t &right)
    {
        return left.owner_id == right.owner_id
               && left.lease_generation == right.lease_generation;
    }

    static bool same_target (
      const object_creation_target_t &left,
      const object_creation_target_t &right)
    {
        return left.mesh_name == right.mesh_name
               && left.node_rid.value () == right.node_rid.value ()
               && left.node_lifecycle_generation
                    == right.node_lifecycle_generation
               && same_owner (left.owner, right.owner);
    }

    static bool allocation_matches_source (
      const placement_allocation_t &allocation,
      const relocation_capacity_reserve_request_t &request)
    {
        return allocation.capacity_state
                 == placement_capacity_state_t::active
               && allocation.object_kind == request.object_kind
               && allocation.stable_type == request.stable_type
               && allocation.mesh_name == request.source.mesh_name
               && allocation.node_rid.value ()
                    == request.source.node_rid.value ()
               && allocation.node_lifecycle_generation
                    == request.source.node_lifecycle_generation
               && allocation.capacity_delta
                    == request.capacity_delta;
    }

    static placement_allocation_t allocation_from_relocation (
      const relocation_capacity_reserve_request_t &request)
    {
        return {
          placement_capacity_state_t::active,
          request.object_kind,
          request.stable_type,
          request.target.mesh_name,
          request.target.node_rid,
          request.target.node_lifecycle_generation,
          request.capacity_delta};
    }

    static bool same_fence (
      const object_reservation_fence_t &left,
      const object_reservation_fence_t &right)
    {
        return left.reservation_id == right.reservation_id
               && left.expected_store_version
                    == right.expected_store_version
               && left.object_generation == right.object_generation
               && left.authority_owner_generation
                    == right.authority_owner_generation
               && left.pending_capacity_delta
                    == right.pending_capacity_delta
               && same_target (left.target, right.target);
    }

    static bool same_relocation_capacity_request (
      const relocation_capacity_reserve_request_t &left,
      const relocation_capacity_reserve_request_t &right)
    {
        return left.reservation_id == right.reservation_id
               && left.key.value == right.key.value
               && left.expected_store_version
                    == right.expected_store_version
               && left.object_kind == right.object_kind
               && left.stable_type == right.stable_type
               && same_target (left.source, right.source)
               && same_target (left.target, right.target)
               && left.capacity_delta == right.capacity_delta;
    }

    static bool same_aggregate_request (
      const aggregate_prepare_request_t &left,
      const aggregate_prepare_request_t &right)
    {
        if (left.aggregate_id.value != right.aggregate_id.value
            || left.aggregate_generation
                 != right.aggregate_generation
            || left.inventory_digest.value
                 != right.inventory_digest.value
            || !same_owner (left.target_owner, right.target_owner)
            || left.participants.size () != right.participants.size ()
            || left.target_reservations.size ()
                 != right.target_reservations.size ())
            return false;
        for (std::size_t index = 0;
             index < left.participants.size (); ++index) {
            const auto &l = left.participants[index];
            const auto &r = right.participants[index];
            if (l.key.value != r.key.value
                || l.expected_store_version
                     != r.expected_store_version
                || l.authority_payload != r.authority_payload
                || l.membership_mutation
                     != r.membership_mutation
                || l.owner_transition != r.owner_transition)
                return false;
        }
        for (std::size_t index = 0;
             index < left.target_reservations.size (); ++index) {
            if (left.target_reservations[index].value
                != right.target_reservations[index].value)
                return false;
        }
        return true;
    }

    static std::size_t aggregate_encoded_size (
      const aggregate_prepare_request_t &request)
    {
        std::size_t size =
          request.target_owner.owner_id.size () + 16 + 8 + 32;
        for (const auto &participant : request.participants) {
            const auto increment =
              participant.key.value.size ()
              + participant.expected_store_version.size ()
              + participant.authority_payload.size ()
              + participant.membership_mutation.size () + 32;
            if (increment > std::numeric_limits<std::size_t>::max ()
                              - size)
                return std::numeric_limits<std::size_t>::max ();
            size += increment;
        }
        for (const auto &fence : request.target_reservations) {
            if (fence.value.size ()
                > std::numeric_limits<std::size_t>::max () - size)
                return std::numeric_limits<std::size_t>::max ();
            size += fence.value.size ();
        }
        return size;
    }

    static bool reservation_bound_to (
      const relocation_capacity_state_t &reservation,
      const aggregate_prepare_request_t &aggregate)
    {
        return reservation.aggregate_id
               && reservation.aggregate_id->value
                    == aggregate.aggregate_id.value
               && reservation.aggregate_generation
                    == aggregate.aggregate_generation;
    }

    static std::string target_capacity_key (
      const object_creation_target_t &target,
      placement_object_kind_t kind,
      const std::string &stable_type)
    {
        return target.mesh_name + "\x1f"
               + std::string (target.node_rid.value ()) + "\x1f"
               + std::to_string (
                 target.node_lifecycle_generation)
               + "\x1f"
               + std::to_string (static_cast<int> (kind))
               + "\x1f" + stable_type;
    }

    static std::string allocation_capacity_key (
      const placement_allocation_t &allocation)
    {
        return allocation.mesh_name + "\x1f"
               + std::string (allocation.node_rid.value ()) + "\x1f"
               + std::to_string (
                 allocation.node_lifecycle_generation)
               + "\x1f"
               + std::to_string (
                 static_cast<int> (allocation.object_kind))
               + "\x1f" + allocation.stable_type;
    }

    void release_pending (const reservation_state_t &reservation)
    {
        auto &pending =
          _pending_by_placement[
            target_capacity_key (
              reservation.fence.target,
              reservation.request.key.kind,
              reservation.request.intent.stable_type)];
        pending =
          pending >= reservation.fence.pending_capacity_delta
            ? pending - reservation.fence.pending_capacity_delta
            : 0;
    }

    void release_relocation_pending (
      const relocation_capacity_state_t &reservation)
    {
        auto &pending =
          _pending_by_placement[
            target_capacity_key (
              reservation.request.target,
              reservation.request.object_kind,
              reservation.request.stable_type)];
        pending -= reservation.request.capacity_delta;
    }

    bool relocation_capacity_counters_available (
      const relocation_capacity_state_t &reservation) const
    {
        const auto source = _active_by_placement.find (
          target_capacity_key (
            reservation.request.source,
            reservation.request.object_kind,
            reservation.request.stable_type));
        const auto target = _pending_by_placement.find (
          target_capacity_key (
            reservation.request.target,
            reservation.request.object_kind,
            reservation.request.stable_type));
        return source != _active_by_placement.end ()
               && source->second
                    >= reservation.request.capacity_delta
               && target != _pending_by_placement.end ()
               && target->second
                    >= reservation.request.capacity_delta;
    }

    void consume_relocation_capacity (
      relocation_capacity_state_t &reservation)
    {
        release_relocation_pending (reservation);
        auto &source_active =
          _active_by_placement[
            target_capacity_key (
              reservation.request.source,
              reservation.request.object_kind,
              reservation.request.stable_type)];
        source_active -= reservation.request.capacity_delta;
        _active_by_placement[
          target_capacity_key (
            reservation.request.target,
            reservation.request.object_kind,
            reservation.request.stable_type)]
          += reservation.request.capacity_delta;
        reservation.status =
          relocation_reservation_status_t::committed;
    }

    static bool all_zero (
      const std::array<std::byte, 16> &value)
    {
        return std::all_of (
          value.begin (), value.end (),
          [] (std::byte item) { return item == std::byte{0}; });
    }

    static std::string aggregate_id_key (
      const aggregate_id_t &id)
    {
        static constexpr char hex[] = "0123456789abcdef";
        std::string result;
        result.reserve (32);
        for (const auto value : id.value) {
            const auto byte = std::to_integer<unsigned char> (value);
            result.push_back (hex[byte >> 4]);
            result.push_back (hex[byte & 0x0f]);
        }
        return result;
    }

    static std::string reservation_id_key (
      const std::array<std::byte, 16> &id)
    {
        return aggregate_id_key (aggregate_id_t{id});
    }

    void cleanup_scans (clock_t::time_point now)
    {
        for (auto scan = _authority_scans.begin ();
             scan != _authority_scans.end ();) {
            if (now - scan->second.created_at > std::chrono::minutes (1))
                scan = _authority_scans.erase (scan);
            else
                ++scan;
        }
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

    location_write_result_t write_actor (const std::string &key,
                                          actor_location_t row,
                                          location_write_intent_t intent,
                                          const std::string &mesh_name)
    {
        std::lock_guard lock (_gate);
        const auto now = clock_t::now ();
        const auto found = _actors.rows.find (key);
        const auto exists = found != _actors.rows.end ();
        if (intent == location_write_intent_t::new_claim && exists
            && owner_is_live (found->second.owner_id, now)) {
            return {location_write_status_t::rejected_conflict, 0, {}};
        }
        if (intent == location_write_intent_t::renew) {
            if (!exists || found->second.owner_id != row.owner_id) {
                return {location_write_status_t::ignored_stale, 0, {}};
            }
            row.updated_at = now;
            _actors.rows[key] = std::move (row);
            bump (location_kind_t::actor, mesh_name);
            return location_write_result_t::stored (_actors.generations[key], now);
        }

        const auto next_generation = _actors.generations[key] + 1;
        _actors.generations[key] = next_generation;
        row.updated_at = now;
        _actors.rows[key] = std::move (row);
        bump (location_kind_t::actor, mesh_name);
        return location_write_result_t::stored (next_generation, now);
    }

    location_write_result_t remove_actor_row (const std::string &key,
                                               const location_owner_token_t &owner,
                                               const std::string &mesh_name)
    {
        std::lock_guard lock (_gate);
        const auto found = _actors.rows.find (key);
        const auto generation = _actors.generations.find (key);
        if (found == _actors.rows.end () || generation == _actors.generations.end ()
            || found->second.owner_id != owner.owner_id
            || generation->second != owner.generation) {
            return {location_write_status_t::ignored_stale, 0, {}};
        }
        _actors.rows.erase (found);
        bump (location_kind_t::actor, mesh_name);
        return location_write_result_t::stored (owner.generation, clock_t::now ());
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
        return (!filter.mesh_name || row.mesh_name == *filter.mesh_name)
               && (!filter.actor_type || row.actor_type == *filter.actor_type)
               && (!filter.owner_node_rid || row.owner_node_rid == *filter.owner_node_rid)
               && (!filter.spot_rid || row.spot_rid == *filter.spot_rid)
               && (!filter.spot_kind || row.spot_kind == *filter.spot_kind);
    }

    static bool matches (const route_location_t &row, const route_location_filter_t &filter)
    {
        return (!filter.route_kind || row.route_kind == *filter.route_kind)
               && (!filter.owner_node_rid || row.owner_node_rid == *filter.owner_node_rid)
               && (!filter.owner_id || row.owner_id == *filter.owner_id);
    }

    mutable std::mutex _gate;
    std::map<std::string, mesh_node_descriptor_t> _mesh_nodes;
    std::map<std::string, owner_lease_t> _leases;
    std::map<std::string, std::int64_t> _active_lease_generations;
    std::uint64_t _lease_generation = 0;
    row_table_t<peer_location_t> _peers;
    row_table_t<spot_location_t> _spots;
    row_table_t<actor_location_t> _actors;
    row_table_t<route_location_t> _routes;
    std::map<std::string, std::int64_t> _stamps;
    std::map<std::string, authority_snapshot_t> _authorities;
    std::map<std::string, std::string> _object_types;
    std::map<std::string, reservation_state_t> _reservations;
    std::map<std::string, relocation_capacity_state_t>
      _relocation_capacity_reservations;
    std::map<std::string, std::string>
      _relocation_capacity_by_id;
    std::map<std::string, aggregate_state_t> _aggregates;
    std::map<std::string, std::uint32_t> _pending_by_placement;
    std::map<std::string, std::uint32_t> _active_by_placement;
    std::uint64_t _object_generation = 0;
    std::uint64_t _authority_owner_generation = 0;
    std::uint64_t _store_revision = 0;
    std::map<std::string, authority_scan_state_t> _authority_scans;
    std::uint64_t _next_scan_id = 0;
};

} // namespace zlink::framework::runtime
