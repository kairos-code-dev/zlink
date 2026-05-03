/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "services/discovery/registry.hpp"
#include "services/discovery/discovery_protocol.hpp"

namespace zlink
{
namespace
{
std::string routing_id_key_of (const zlink_routing_id_t &rid_)
{
    if (rid_.size == 0)
        return std::string ();
    return std::string (reinterpret_cast<const char *> (rid_.data), rid_.size);
}
}

bool registry_t::find_provider_owner_locked (
  const std::string &channel_name_,
  uint16_t service_role_,
  const std::string &endpoint_,
  owner_identity_t *owner_out_,
  zlink_routing_id_t *routing_id_out_) const
{
    service_key_t service_key;
    service_key.channel_name = channel_name_;
    service_map_t::const_iterator sit = _projection_state.services.find (service_key);
    if (sit == _projection_state.services.end ())
        return false;

    provider_key_t provider_key;
    provider_key.service_role = service_role_;
    provider_key.endpoint = endpoint_;
    provider_map_t::const_iterator pit = sit->second.providers.find (provider_key);
    if (pit == sit->second.providers.end ())
        return false;

    if (owner_out_) {
        owner_out_->channel_name = channel_name_;
        owner_out_->service_role = pit->second.service_role;
        owner_out_->routing_id_key = routing_id_key_of (pit->second.routing_id);
        owner_out_->source_registry = pit->second.source_registry;
        owner_out_->registration_id = pit->second.registration_id;
    }
    if (routing_id_out_)
        *routing_id_out_ = pit->second.routing_id;
    return true;
}

bool registry_t::owner_is_live_locked (const owner_identity_t &owner_) const
{
    service_key_t service_key;
    service_key.channel_name = owner_.channel_name;
    service_map_t::const_iterator sit = _projection_state.services.find (service_key);
    if (sit == _projection_state.services.end ())
        return false;
    uint32_t matching_generation_count = 0;
    for (provider_map_t::const_iterator pit = sit->second.providers.begin ();
         pit != sit->second.providers.end (); ++pit) {
        const provider_entry_t &provider = pit->second;
        if (provider.service_role != owner_.service_role)
            continue;
        if (routing_id_key_of (provider.routing_id) != owner_.routing_id_key)
            continue;
        if (provider.source_registry == owner_.source_registry
            && provider.registration_id == owner_.registration_id) {
            matching_generation_count++;
        } else {
            return false;
        }
    }
    return matching_generation_count == 1;
}

bool registry_t::route_entry_wins_locked (const route_entry_t &candidate_,
                                          const route_entry_t &current_) const
{
    if (candidate_.updated_at_ms != current_.updated_at_ms)
        return candidate_.updated_at_ms > current_.updated_at_ms;

    const bool candidate_direct =
      candidate_.advertising_registry == candidate_.owner.source_registry;
    const bool current_direct =
      current_.advertising_registry == current_.owner.source_registry;
    if (candidate_direct != current_direct)
        return candidate_direct;

    if (candidate_.owner.source_registry != current_.owner.source_registry)
        return candidate_.owner.source_registry < current_.owner.source_registry;
    if (candidate_.owner.registration_id != current_.owner.registration_id)
        return candidate_.owner.registration_id > current_.owner.registration_id;
    if (candidate_.owner.routing_id_key != current_.owner.routing_id_key)
        return candidate_.owner.routing_id_key < current_.owner.routing_id_key;
    return candidate_.advertising_registry < current_.advertising_registry;
}

size_t registry_t::route_entry_memory_bytes (const route_entry_t &entry_) const
{
    return sizeof (route_entry_t) + entry_.key.channel_name.size ()
           + entry_.key.key.size () + entry_.value.size ()
           + entry_.owner.channel_name.size ()
           + entry_.owner.routing_id_key.size () + 128;
}

bool registry_t::route_store_can_fit_locked (const route_entry_t &entry_,
                                             size_t removed_memory_,
                                             int *err_out_) const
{
    const size_t entry_bytes = route_entry_memory_bytes (entry_);
    const size_t owner_count =
      _projection_state.routes_by_owner.find (entry_.owner) == _projection_state.routes_by_owner.end ()
        ? 0
        : _projection_state.routes_by_owner.find (entry_.owner)->second.size ();

    if (entry_.key.key.size () > ZLINK_ROUTE_KEY_MAX
        || entry_.value.size () > ZLINK_ROUTE_VALUE_MAX) {
        if (err_out_)
            *err_out_ = E2BIG;
        return false;
    }
    if (_projection_state.routes.find (entry_.key) == _projection_state.routes.end ()
        && _projection_state.routes.size () >= _projection_state.route_limits.max_materialized_routes) {
        if (err_out_)
            *err_out_ = ENOSPC;
        return false;
    }
    if (owner_count >= _projection_state.route_limits.max_observations_per_owner
        && removed_memory_ == 0) {
        if (err_out_)
            *err_out_ = ENOSPC;
        return false;
    }
    if (_projection_state.route_stats.memory_bytes + entry_bytes - removed_memory_
        > _projection_state.route_limits.memory_budget_bytes) {
        if (err_out_)
            *err_out_ = ENOSPC;
        return false;
    }
    return true;
}

void registry_t::erase_route_observation_locked (
  const route_observation_key_t &key_,
  route_key_set_t *dirty_routes_)
{
    route_observation_map_t::iterator it = _projection_state.route_observations.find (key_);
    if (it == _projection_state.route_observations.end ())
        return;

    const size_t entry_bytes = route_entry_memory_bytes (it->second);
    route_observations_by_route_t::iterator route_it =
      _projection_state.route_observations_by_route.find (key_.route_key);
    if (route_it != _projection_state.route_observations_by_route.end ()) {
        route_it->second.erase (key_);
        if (route_it->second.empty ())
            _projection_state.route_observations_by_route.erase (route_it);
    }

    route_owner_index_t::iterator owner_it = _projection_state.routes_by_owner.find (key_.owner);
    if (owner_it != _projection_state.routes_by_owner.end ()) {
        owner_it->second.erase (key_.route_key);
        if (owner_it->second.empty ())
            _projection_state.routes_by_owner.erase (owner_it);
    }

    route_advertiser_index_t::iterator advertiser_it =
      _projection_state.routes_by_advertiser.find (key_.advertising_registry);
    if (advertiser_it != _projection_state.routes_by_advertiser.end ()) {
        advertiser_it->second.erase (key_.route_key);
        if (advertiser_it->second.empty ())
            _projection_state.routes_by_advertiser.erase (advertiser_it);
    }

    _projection_state.route_stats.memory_bytes =
      _projection_state.route_stats.memory_bytes > entry_bytes ? _projection_state.route_stats.memory_bytes
                                                  - entry_bytes
                                              : 0;
    _projection_state.route_observations.erase (it);
    if (dirty_routes_)
        dirty_routes_->insert (key_.route_key);
}

void registry_t::erase_route_observations_by_route_advertiser_locked (
  const route_key_t &route_key_,
  uint32_t advertising_registry_,
  route_key_set_t *dirty_routes_)
{
    route_observations_by_route_t::const_iterator route_it =
      _projection_state.route_observations_by_route.find (route_key_);
    if (route_it == _projection_state.route_observations_by_route.end ())
        return;

    std::vector<route_observation_key_t> remove_keys;
    for (route_observation_key_set_t::const_iterator it =
           route_it->second.begin ();
         it != route_it->second.end (); ++it) {
        if (it->advertising_registry == advertising_registry_)
            remove_keys.push_back (*it);
    }
    for (std::vector<route_observation_key_t>::const_iterator it =
           remove_keys.begin ();
         it != remove_keys.end (); ++it) {
        erase_route_observation_locked (*it, dirty_routes_);
    }
}

void registry_t::upsert_route_observation_locked (
  const route_entry_t &entry_,
  route_key_set_t *dirty_routes_)
{
    route_observation_key_t obs_key;
    obs_key.route_key = entry_.key;
    obs_key.owner = entry_.owner;
    obs_key.advertising_registry = entry_.advertising_registry;

    size_t removed_memory = 0;
    route_observation_map_t::const_iterator existing =
      _projection_state.route_observations.find (obs_key);
    if (existing != _projection_state.route_observations.end ())
        removed_memory = route_entry_memory_bytes (existing->second);

    erase_route_observation_locked (obs_key, dirty_routes_);
    _projection_state.route_observations[obs_key] = entry_;
    _projection_state.route_observations_by_route[entry_.key].insert (obs_key);
    _projection_state.routes_by_owner[entry_.owner].insert (entry_.key);
    _projection_state.routes_by_advertiser[entry_.advertising_registry].insert (entry_.key);
    _projection_state.route_stats.memory_bytes += route_entry_memory_bytes (entry_);
    (void) removed_memory;
    if (dirty_routes_)
        dirty_routes_->insert (entry_.key);
}

void registry_t::materialize_route_winner_locked (const route_key_t &route_key_)
{
    _projection_state.route_stats.winner_recompute_count++;
    route_observations_by_route_t::const_iterator route_it =
      _projection_state.route_observations_by_route.find (route_key_);
    if (route_it == _projection_state.route_observations_by_route.end ()) {
        _projection_state.routes.erase (route_key_);
        return;
    }

    bool have_winner = false;
    route_entry_t winner;
    for (route_observation_key_set_t::const_iterator it =
           route_it->second.begin ();
         it != route_it->second.end (); ++it) {
        route_observation_map_t::const_iterator observation =
          _projection_state.route_observations.find (*it);
        if (observation == _projection_state.route_observations.end ())
            continue;
        _projection_state.route_stats.winner_recompute_observation_visits++;
        if (!owner_is_live_locked (observation->second.owner))
            continue;
        if (!have_winner
            || route_entry_wins_locked (observation->second, winner)) {
            winner = observation->second;
            have_winner = true;
        }
    }

    if (have_winner)
        _projection_state.routes[route_key_] = winner;
    else
        _projection_state.routes.erase (route_key_);
}

void registry_t::materialize_dirty_routes_locked (
  const route_key_set_t &dirty_routes_)
{
    for (route_key_set_t::const_iterator it = dirty_routes_.begin ();
         it != dirty_routes_.end (); ++it) {
        materialize_route_winner_locked (*it);
    }
}

void registry_t::promote_owner_route_records_locked (
  const owner_identity_t &owner_)
{
    route_owner_index_t::const_iterator owner_it =
      _projection_state.routes_by_owner.find (owner_);
    if (owner_it == _projection_state.routes_by_owner.end ())
        return;
    materialize_dirty_routes_locked (owner_it->second);
}

void registry_t::remove_topology_owner_index_locked (
  const topology_key_t &key_,
  const topology_entry_t &entry_)
{
    if (!entry_.has_owner)
        return;
    topology_owner_index_t::iterator owner_it =
      _projection_state.topology_by_owner.find (entry_.owner);
    if (owner_it == _projection_state.topology_by_owner.end ())
        return;
    owner_it->second.erase (key_);
    if (owner_it->second.empty ())
        _projection_state.topology_by_owner.erase (owner_it);
}

void registry_t::index_topology_owner_locked (
  const topology_key_t &key_,
  const topology_entry_t &entry_)
{
    if (entry_.has_owner)
        _projection_state.topology_by_owner[entry_.owner].insert (key_);
}

void registry_t::cleanup_owner_records_locked (const owner_identity_t &owner_,
                                               uint64_t now_ms_)
{
    route_owner_index_t::iterator rit = _projection_state.routes_by_owner.find (owner_);
    if (rit != _projection_state.routes_by_owner.end ()) {
        route_key_set_t dirty_routes;
        route_key_set_t owned = rit->second;
        for (route_key_set_t::const_iterator it = owned.begin ();
             it != owned.end (); ++it) {
            route_observations_by_route_t::const_iterator route_it =
              _projection_state.route_observations_by_route.find (*it);
            if (route_it == _projection_state.route_observations_by_route.end ())
                continue;
            std::vector<route_observation_key_t> remove_keys;
            for (route_observation_key_set_t::const_iterator obs =
                   route_it->second.begin ();
                 obs != route_it->second.end (); ++obs) {
                if (obs->owner == owner_)
                    remove_keys.push_back (*obs);
            }
            for (std::vector<route_observation_key_t>::const_iterator obs =
                   remove_keys.begin ();
                 obs != remove_keys.end (); ++obs) {
                erase_route_observation_locked (*obs, &dirty_routes);
                _projection_state.route_stats.owner_cleanup_observation_visits++;
            }
        }
        _projection_state.route_stats.owner_cleanup_count++;
        materialize_dirty_routes_locked (dirty_routes);
    }

    topology_owner_index_t::iterator topology_it =
      _projection_state.topology_by_owner.find (owner_);
    if (topology_it != _projection_state.topology_by_owner.end ()) {
        topology_key_set_t owned = topology_it->second;
        for (topology_key_set_t::const_iterator it = owned.begin ();
             it != owned.end (); ++it) {
            std::map<topology_key_t, topology_entry_t>::iterator row_it =
              _projection_state.topology.find (*it);
            if (row_it == _projection_state.topology.end ())
                continue;
            topology_entry_t &row = row_it->second;
            if (row.entry.state == ZLINK_TOPOLOGY_STATE_READY) {
                row.entry.state = ZLINK_TOPOLOGY_STATE_LOST;
                row.entry.last_reported_ms = now_ms_;
            }
        }
    }
}

void registry_t::cleanup_advertised_route_records_locked (
  uint32_t advertising_registry_)
{
    route_advertiser_index_t::iterator ait =
      _projection_state.routes_by_advertiser.find (advertising_registry_);
    if (ait == _projection_state.routes_by_advertiser.end ())
        return;

    route_key_set_t advertised = ait->second;
    route_key_set_t dirty_routes;
    for (route_key_set_t::const_iterator it = advertised.begin ();
         it != advertised.end (); ++it) {
        route_observations_by_route_t::const_iterator route_it =
          _projection_state.route_observations_by_route.find (*it);
        if (route_it == _projection_state.route_observations_by_route.end ())
            continue;
        std::vector<route_observation_key_t> remove_keys;
        for (route_observation_key_set_t::const_iterator obs =
               route_it->second.begin ();
             obs != route_it->second.end (); ++obs) {
            if (obs->advertising_registry == advertising_registry_)
                remove_keys.push_back (*obs);
        }
        for (std::vector<route_observation_key_t>::const_iterator obs =
               remove_keys.begin ();
             obs != remove_keys.end (); ++obs) {
            erase_route_observation_locked (*obs, &dirty_routes);
            _projection_state.route_stats.advertiser_cleanup_observation_visits++;
        }
    }
    _projection_state.route_stats.advertiser_cleanup_count++;
    materialize_dirty_routes_locked (dirty_routes);
}

void registry_t::upsert_topology_entry (
  const zlink_registry_topology_entry_t &entry_,
  uint64_t now_ms_)
{
    topology_key_t key;
    key.service_kind = entry_.service_kind;
    key.service_role = entry_.service_role;
    key.routing_id_key = routing_id_key_of (entry_.routing_id);
    key.channel_name = entry_.channel_name;
    key.endpoint = entry_.endpoint;

    topology_entry_t &stored = _projection_state.topology[key];
    remove_topology_owner_index_locked (key, stored);
    stored.entry = entry_;
    stored.entry.last_reported_ms = now_ms_;
    stored.has_owner = false;
    if (entry_.auto_connect_type == ZLINK_AUTO_CONNECT_SPOT_MESH
        && entry_.service_kind == ZLINK_SERVICE_KIND_SPOT_PUB
        && entry_.service_role == ZLINK_SERVICE_ROLE_SPOT) {
        stored.has_owner = find_provider_owner_locked (
          entry_.channel_name, discovery_protocol::service_role_spot,
          entry_.endpoint, &stored.owner, NULL);
    }
    index_topology_owner_locked (key, stored);
    _coordination_state.summary_last_changed_ms = now_ms_;
}

void registry_t::send_service_list (void *pub_)
{
    uint32_t registry_id = 0;
    {
        scoped_lock_t lock (_sync);
        registry_id = _coordination_state.registry_id;
        if (registry_id == 0)
            registry_id = 1;
    }

    discovery_protocol::send_u16 (pub_, discovery_protocol::msg_service_list,
                                  ZLINK_SNDMORE);
    discovery_protocol::send_u32 (pub_, registry_id, ZLINK_SNDMORE);
    discovery_protocol::send_u64 (pub_, _coordination_state.list_seq, ZLINK_SNDMORE);

    uint32_t service_count = 0;
    for (service_map_t::const_iterator it = _projection_state.services.begin ();
         it != _projection_state.services.end (); ++it) {
        if (!it->second.providers.empty ())
            service_count++;
    }

    discovery_protocol::send_u32 (pub_, service_count,
                                  service_count == 0 ? 0 : ZLINK_SNDMORE);

    if (service_count == 0)
        return;

    uint32_t emitted = 0;
    for (service_map_t::const_iterator it = _projection_state.services.begin ();
         it != _projection_state.services.end (); ++it) {
        if (it->second.providers.empty ())
            continue;

        const service_key_t &service_key = it->first;
        const service_entry_t &service = it->second;
        const provider_map_t &providers = service.providers;
        const uint32_t provider_count =
          static_cast<uint32_t> (providers.size ());
        uint64_t contract_created_at = 0;
        std::map<std::string, channel_contract_t>::const_iterator cit =
          _projection_state.channel_contracts.find (service_key.channel_name);
        if (cit != _projection_state.channel_contracts.end ())
            contract_created_at = cit->second.created_at;

        discovery_protocol::send_u16 (pub_, service.auto_connect_type,
                                      ZLINK_SNDMORE);
        discovery_protocol::send_string (pub_, service_key.channel_name,
                                         ZLINK_SNDMORE);
        discovery_protocol::send_u64 (pub_, contract_created_at,
                                      ZLINK_SNDMORE);
        discovery_protocol::send_u32 (pub_, provider_count,
                                      ZLINK_SNDMORE);

        uint32_t provider_index = 0;
        for (provider_map_t::const_iterator pit = providers.begin ();
             pit != providers.end (); ++pit, ++provider_index) {
            const provider_entry_t &entry = pit->second;
            const bool last_provider =
              (provider_index + 1) == provider_count
              && (emitted + 1) == service_count;

            discovery_protocol::send_u16 (pub_, entry.service_role,
                                          ZLINK_SNDMORE);
            discovery_protocol::send_string (pub_, entry.endpoint,
                                             ZLINK_SNDMORE);
            discovery_protocol::send_routing_id (pub_, entry.routing_id,
                                                 ZLINK_SNDMORE);
            discovery_protocol::send_u32 (pub_, entry.source_registry,
                                          ZLINK_SNDMORE);
            discovery_protocol::send_u64 (pub_, entry.registration_id,
                                          ZLINK_SNDMORE);
            discovery_protocol::send_u64 (pub_, entry.provider_update_seq,
                                          ZLINK_SNDMORE);
            discovery_protocol::send_u16 (
              pub_, static_cast<uint16_t> (entry.weight), ZLINK_SNDMORE);
            discovery_protocol::send_i64 (pub_, entry.value, ZLINK_SNDMORE);
            discovery_protocol::send_frame (
              pub_, entry.metadata.empty () ? NULL : &entry.metadata[0],
              entry.metadata.size (), last_provider ? 0 : ZLINK_SNDMORE);
        }

        emitted++;
    }
}

void registry_t::send_route_list (void *pub_)
{
    uint32_t registry_id = 0;
    uint64_t list_seq = 0;
    uint32_t chunk_records = 0;
    uint32_t chunk_count = 1;
    size_t cursor = 0;
    {
        scoped_lock_t lock (_sync);
        registry_id = _coordination_state.registry_id == 0 ? 1 : _coordination_state.registry_id;
        if (_projection_state.routes.empty () && !_coordination_state.route_snapshot_announced)
            return;
        if (!_projection_state.routes.empty ())
            _coordination_state.route_snapshot_announced = true;
        else
            _coordination_state.route_snapshot_announced = false;
        list_seq = _coordination_state.list_seq;
        chunk_records = _projection_state.route_limits.snapshot_chunk_records == 0
                          ? 1024
                          : _projection_state.route_limits.snapshot_chunk_records;
        if (!_projection_state.routes.empty ())
            chunk_count = static_cast<uint32_t> (
              (_projection_state.routes.size () + chunk_records - 1) / chunk_records);
    }

    for (uint32_t chunk_index = 0; chunk_index < chunk_count; ++chunk_index) {
        std::vector<route_entry_t> routes;
        routes.reserve (chunk_records);
        {
            scoped_lock_t lock (_sync);
            route_map_t::rehash_pause_guard_t pause (_projection_state.routes);
            cursor = _projection_state.routes.snapshot_values (cursor, chunk_records, &routes);
        }
        const uint32_t route_count =
          static_cast<uint32_t> (routes.size ());

        discovery_protocol::send_u16 (pub_,
                                      discovery_protocol::msg_registry_sync,
                                      ZLINK_SNDMORE);
        discovery_protocol::send_u32 (pub_, registry_id, ZLINK_SNDMORE);
        discovery_protocol::send_u64 (pub_, list_seq, ZLINK_SNDMORE);
        discovery_protocol::send_u32 (pub_, chunk_index, ZLINK_SNDMORE);
        discovery_protocol::send_u32 (pub_, chunk_count, ZLINK_SNDMORE);
        discovery_protocol::send_u32 (
          pub_, route_count, route_count == 0 ? 0 : ZLINK_SNDMORE);

        for (size_t index = 0; index < routes.size (); ++index) {
            const route_entry_t &entry = routes[index];
            const bool last_route = index + 1 == routes.size ();

            discovery_protocol::send_string (pub_, entry.key.channel_name,
                                             ZLINK_SNDMORE);
            discovery_protocol::send_u32 (pub_, entry.key.kind, ZLINK_SNDMORE);
            discovery_protocol::send_frame (
              pub_, entry.key.key.data (), entry.key.key.size (),
              ZLINK_SNDMORE);
            discovery_protocol::send_frame (
              pub_, entry.value.empty () ? NULL : &entry.value[0],
              entry.value.size (), ZLINK_SNDMORE);
            discovery_protocol::send_string (pub_, entry.owner.channel_name,
                                             ZLINK_SNDMORE);
            discovery_protocol::send_u16 (pub_, entry.owner.service_role,
                                          ZLINK_SNDMORE);
            discovery_protocol::send_frame (
              pub_, entry.owner.routing_id_key.data (),
              entry.owner.routing_id_key.size (), ZLINK_SNDMORE);
            discovery_protocol::send_u32 (pub_, entry.owner.source_registry,
                                          ZLINK_SNDMORE);
            discovery_protocol::send_u64 (pub_, entry.owner.registration_id,
                                          ZLINK_SNDMORE);
            discovery_protocol::send_u64 (pub_, entry.updated_at_ms,
                                          ZLINK_SNDMORE);
            zlink_routing_id_t owner_rid;
            if (!owner_routing_id_from_key (entry.owner, &owner_rid))
                memset (&owner_rid, 0, sizeof (owner_rid));
            discovery_protocol::send_routing_id (
              pub_, owner_rid, last_route ? 0 : ZLINK_SNDMORE);
        }
    }
}

void registry_t::remove_expired (uint64_t now_ms_)
{
    const uint32_t local_registry_id = _coordination_state.registry_id;
    bool changed = false;
    for (service_map_t::iterator sit = _projection_state.services.begin ();
         sit != _projection_state.services.end ();) {
        provider_map_t &providers = sit->second.providers;
        for (provider_map_t::iterator pit = providers.begin ();
             pit != providers.end ();) {
            if (pit->second.source_registry != local_registry_id) {
                ++pit;
                continue;
            }
            if (now_ms_ > pit->second.last_heartbeat
                && now_ms_ - pit->second.last_heartbeat
                     > _coordination_state.heartbeat_timeout_ms) {
                owner_identity_t removed_owner;
                removed_owner.channel_name = sit->first.channel_name;
                removed_owner.service_role = pit->second.service_role;
                removed_owner.routing_id_key =
                  routing_id_key_of (pit->second.routing_id);
                removed_owner.source_registry = pit->second.source_registry;
                removed_owner.registration_id = pit->second.registration_id;
                cleanup_owner_records_locked (removed_owner, now_ms_);
                pit = providers.erase (pit);
                changed = true;
                continue;
            }
            ++pit;
        }
        if (providers.empty ())
            sit = _projection_state.services.erase (sit);
        else
            ++sit;
    }

    uint64_t peer_timeout_ms = _coordination_state.broadcast_interval_ms;
    if (peer_timeout_ms == 0)
        peer_timeout_ms = 30000;
    peer_timeout_ms *= 3;

    for (std::map<uint32_t, uint64_t>::iterator pit = _projection_state.peer_last_seen.begin ();
         pit != _projection_state.peer_last_seen.end ();) {
        const uint32_t peer_id = pit->first;
        if (now_ms_ > pit->second && now_ms_ - pit->second > peer_timeout_ms) {
            cleanup_advertised_route_records_locked (peer_id);
            for (service_map_t::iterator sit = _projection_state.services.begin ();
                 sit != _projection_state.services.end ();) {
                provider_map_t &providers = sit->second.providers;
                for (provider_map_t::iterator eit = providers.begin ();
                     eit != providers.end ();) {
                    if (eit->second.source_registry == peer_id) {
                        owner_identity_t removed_owner;
                        removed_owner.channel_name = sit->first.channel_name;
                        removed_owner.service_role = eit->second.service_role;
                        removed_owner.routing_id_key =
                          routing_id_key_of (eit->second.routing_id);
                        removed_owner.source_registry =
                          eit->second.source_registry;
                        removed_owner.registration_id =
                          eit->second.registration_id;
                        cleanup_owner_records_locked (removed_owner, now_ms_);
                        eit = providers.erase (eit);
                        changed = true;
                        continue;
                    }
                    ++eit;
                }
                if (providers.empty ()) {
                    sit = _projection_state.services.erase (sit);
                    continue;
                }
                ++sit;
            }
            _projection_state.peer_seq.erase (peer_id);
            pit = _projection_state.peer_last_seen.erase (pit);
            continue;
        }
        ++pit;
    }

    const uint64_t report_timeout_ms = _coordination_state.heartbeat_timeout_ms;
    const uint64_t stale_gc_timeout_ms = report_timeout_ms * 2;
    const uint64_t stopped_gc_timeout_ms = 1000;

    for (std::map<topology_key_t, topology_entry_t>::iterator it =
           _projection_state.topology.begin ();
         it != _projection_state.topology.end ();) {
        zlink_registry_topology_entry_t &entry = it->second.entry;
        const uint64_t age =
          now_ms_ > entry.last_reported_ms ? now_ms_ - entry.last_reported_ms : 0;
        if (entry.state == ZLINK_TOPOLOGY_STATE_STOPPED
            || entry.state == ZLINK_TOPOLOGY_STATE_LOST) {
            if (age > stopped_gc_timeout_ms) {
                remove_topology_owner_index_locked (it->first, it->second);
                it = _projection_state.topology.erase (it);
                changed = true;
                continue;
            }
        } else if (age > stale_gc_timeout_ms) {
            remove_topology_owner_index_locked (it->first, it->second);
            it = _projection_state.topology.erase (it);
            changed = true;
            continue;
        } else if (age > report_timeout_ms
                   && entry.state == ZLINK_TOPOLOGY_STATE_READY) {
            entry.state = ZLINK_TOPOLOGY_STATE_LOST;
            changed = true;
        }
        ++it;
    }

    if (changed) {
        _coordination_state.list_seq++;
        _coordination_state.summary_last_changed_ms = now_ms_;
    }
}
}
