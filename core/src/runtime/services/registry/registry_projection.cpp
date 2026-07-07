/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/registry/registry.hpp"
#include "services/discovery/discovery_protocol.hpp"
#include "utils/routing_id.hpp"

namespace zlink
{
registry_t::owner_identity_t
registry_t::owner_identity_for_provider_locked (const std::string &channel_name_,
                                                const provider_entry_t &provider_) const
{
    owner_identity_t owner;
    owner.channel_name = channel_name_;
    owner.service_role = provider_.service_role;
    owner.routing_id_key = routing_id_key (provider_.routing_id);
    owner.source_registry = provider_.source_registry;
    owner.registration_id = provider_.registration_id;
    return owner;
}

bool registry_t::find_provider_owner_locked (const std::string &channel_name_,
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

    if (owner_out_)
        *owner_out_ = owner_identity_for_provider_locked (channel_name_, pit->second);
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
        if (routing_id_key (provider.routing_id) != owner_.routing_id_key)
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
    const bool current_direct = current_.advertising_registry == current_.owner.source_registry;
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
    return sizeof (route_entry_t) + entry_.key.channel_name.size () + entry_.key.key.size ()
           + entry_.value.size () + entry_.owner.channel_name.size ()
           + entry_.owner.routing_id_key.size () + 128;
}

bool registry_t::route_store_can_fit_locked (const route_entry_t &entry_,
                                             size_t removed_memory_,
                                             bool replaces_owner_route_,
                                             int *err_out_) const
{
    const size_t entry_bytes = route_entry_memory_bytes (entry_);
    const size_t owner_count =
      _projection_state.routes_by_owner.find (entry_.owner)
          == _projection_state.routes_by_owner.end ()
        ? 0
        : _projection_state.routes_by_owner.find (entry_.owner)->second.size ();

    if (entry_.key.key.size () > ZLINK_ROUTE_KEY_MAX
        || entry_.value.size () > ZLINK_ROUTE_VALUE_MAX) {
        if (err_out_)
            *err_out_ = E2BIG;
        return false;
    }
    if (_projection_state.routes.find (entry_.key) == _projection_state.routes.end ()
        && _projection_state.routes.size ()
             >= _projection_state.route_limits.max_materialized_routes) {
        if (err_out_)
            *err_out_ = ENOSPC;
        return false;
    }
    if (owner_count >= _projection_state.route_limits.max_observations_per_owner
        && !replaces_owner_route_) {
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

void registry_t::erase_route_observation_locked (const route_observation_key_t &key_,
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
      _projection_state.route_stats.memory_bytes > entry_bytes
        ? _projection_state.route_stats.memory_bytes - entry_bytes
        : 0;
    _projection_state.route_observations.erase (it);
    if (dirty_routes_)
        dirty_routes_->insert (key_.route_key);
}

void registry_t::erase_route_observations_by_route_advertiser_locked (
  const route_key_t &route_key_, uint32_t advertising_registry_, route_key_set_t *dirty_routes_)
{
    route_observations_by_route_t::const_iterator route_it =
      _projection_state.route_observations_by_route.find (route_key_);
    if (route_it == _projection_state.route_observations_by_route.end ())
        return;

    std::vector<route_observation_key_t> remove_keys;
    for (route_observation_key_set_t::const_iterator it = route_it->second.begin ();
         it != route_it->second.end (); ++it) {
        if (it->advertising_registry == advertising_registry_)
            remove_keys.push_back (*it);
    }
    for (std::vector<route_observation_key_t>::const_iterator it = remove_keys.begin ();
         it != remove_keys.end (); ++it) {
        erase_route_observation_locked (*it, dirty_routes_);
    }
}

void registry_t::upsert_route_observation_locked (const route_entry_t &entry_,
                                                  route_key_set_t *dirty_routes_)
{
    route_observation_key_t obs_key;
    obs_key.route_key = entry_.key;
    obs_key.owner = entry_.owner;
    obs_key.advertising_registry = entry_.advertising_registry;

    erase_route_observation_locked (obs_key, dirty_routes_);
    _projection_state.route_observations[obs_key] = entry_;
    _projection_state.route_observations_by_route[entry_.key].insert (obs_key);
    _projection_state.routes_by_owner[entry_.owner].insert (entry_.key);
    _projection_state.routes_by_advertiser[entry_.advertising_registry].insert (entry_.key);
    _projection_state.route_stats.memory_bytes += route_entry_memory_bytes (entry_);
    if (dirty_routes_)
        dirty_routes_->insert (entry_.key);
}

bool registry_t::replace_route_observation_for_advertiser_locked (const route_entry_t &entry_,
                                                                  int *err_out_)
{
    size_t replaced_memory = 0;
    bool replaces_owner_route = false;
    route_observations_by_route_t::const_iterator route_it =
      _projection_state.route_observations_by_route.find (entry_.key);
    if (route_it != _projection_state.route_observations_by_route.end ()) {
        for (route_observation_key_set_t::const_iterator obs = route_it->second.begin ();
             obs != route_it->second.end (); ++obs) {
            if (obs->advertising_registry != entry_.advertising_registry)
                continue;
            route_observation_map_t::const_iterator current =
              _projection_state.route_observations.find (*obs);
            if (current != _projection_state.route_observations.end ())
                replaced_memory += route_entry_memory_bytes (current->second);
            if (obs->owner == entry_.owner)
                replaces_owner_route = true;
        }
    }

    int route_error = 0;
    if (!route_store_can_fit_locked (entry_, replaced_memory, replaces_owner_route, &route_error)) {
        if (err_out_)
            *err_out_ = route_error;
        return false;
    }

    route_key_set_t dirty_routes;
    erase_route_observations_by_route_advertiser_locked (entry_.key, entry_.advertising_registry,
                                                         &dirty_routes);
    upsert_route_observation_locked (entry_, &dirty_routes);
    materialize_dirty_routes_locked (dirty_routes);
    if (err_out_)
        *err_out_ = 0;
    return true;
}

bool registry_t::erase_route_observation_for_owner_locked (const route_key_t &route_key_,
                                                           const owner_identity_t &owner_,
                                                           uint32_t advertising_registry_,
                                                           int *err_out_)
{
    route_observation_key_t obs_key;
    obs_key.route_key = route_key_;
    obs_key.owner = owner_;
    obs_key.advertising_registry = advertising_registry_;

    route_observation_map_t::const_iterator obs_it =
      _projection_state.route_observations.find (obs_key);
    if (obs_it == _projection_state.route_observations.end ()) {
        if (err_out_)
            *err_out_ = ENOENT;
        return false;
    }

    route_key_set_t dirty_routes;
    erase_route_observation_locked (obs_key, &dirty_routes);
    materialize_dirty_routes_locked (dirty_routes);
    if (err_out_)
        *err_out_ = 0;
    return true;
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
    for (route_observation_key_set_t::const_iterator it = route_it->second.begin ();
         it != route_it->second.end (); ++it) {
        route_observation_map_t::const_iterator observation =
          _projection_state.route_observations.find (*it);
        if (observation == _projection_state.route_observations.end ())
            continue;
        _projection_state.route_stats.winner_recompute_observation_visits++;
        if (!owner_is_live_locked (observation->second.owner))
            continue;
        if (!have_winner || route_entry_wins_locked (observation->second, winner)) {
            winner = observation->second;
            have_winner = true;
        }
    }

    if (have_winner)
        _projection_state.routes[route_key_] = winner;
    else
        _projection_state.routes.erase (route_key_);
}

void registry_t::materialize_dirty_routes_locked (const route_key_set_t &dirty_routes_)
{
    for (route_key_set_t::const_iterator it = dirty_routes_.begin (); it != dirty_routes_.end ();
         ++it) {
        materialize_route_winner_locked (*it);
    }
}

void registry_t::promote_owner_route_records_locked (const owner_identity_t &owner_)
{
    route_owner_index_t::const_iterator owner_it = _projection_state.routes_by_owner.find (owner_);
    if (owner_it == _projection_state.routes_by_owner.end ())
        return;
    materialize_dirty_routes_locked (owner_it->second);
}

void registry_t::remove_topology_owner_index_locked (const topology_key_t &key_,
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

void registry_t::index_topology_owner_locked (const topology_key_t &key_,
                                              const topology_entry_t &entry_)
{
    if (entry_.has_owner)
        _projection_state.topology_by_owner[entry_.owner].insert (key_);
}

void registry_t::cleanup_owner_records_locked (const owner_identity_t &owner_, uint64_t now_ms_)
{
    route_owner_index_t::iterator rit = _projection_state.routes_by_owner.find (owner_);
    if (rit != _projection_state.routes_by_owner.end ()) {
        route_key_set_t dirty_routes;
        route_key_set_t owned = rit->second;
        for (route_key_set_t::const_iterator it = owned.begin (); it != owned.end (); ++it) {
            route_observations_by_route_t::const_iterator route_it =
              _projection_state.route_observations_by_route.find (*it);
            if (route_it == _projection_state.route_observations_by_route.end ())
                continue;
            std::vector<route_observation_key_t> remove_keys;
            for (route_observation_key_set_t::const_iterator obs = route_it->second.begin ();
                 obs != route_it->second.end (); ++obs) {
                if (obs->owner == owner_)
                    remove_keys.push_back (*obs);
            }
            for (std::vector<route_observation_key_t>::const_iterator obs = remove_keys.begin ();
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
        for (topology_key_set_t::const_iterator it = owned.begin (); it != owned.end (); ++it) {
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

void registry_t::cleanup_advertised_route_records_locked (uint32_t advertising_registry_)
{
    route_advertiser_index_t::iterator ait =
      _projection_state.routes_by_advertiser.find (advertising_registry_);
    if (ait == _projection_state.routes_by_advertiser.end ())
        return;

    route_key_set_t advertised = ait->second;
    route_key_set_t dirty_routes;
    for (route_key_set_t::const_iterator it = advertised.begin (); it != advertised.end (); ++it) {
        route_observations_by_route_t::const_iterator route_it =
          _projection_state.route_observations_by_route.find (*it);
        if (route_it == _projection_state.route_observations_by_route.end ())
            continue;
        std::vector<route_observation_key_t> remove_keys;
        for (route_observation_key_set_t::const_iterator obs = route_it->second.begin ();
             obs != route_it->second.end (); ++obs) {
            if (obs->advertising_registry == advertising_registry_)
                remove_keys.push_back (*obs);
        }
        for (std::vector<route_observation_key_t>::const_iterator obs = remove_keys.begin ();
             obs != remove_keys.end (); ++obs) {
            erase_route_observation_locked (*obs, &dirty_routes);
            _projection_state.route_stats.advertiser_cleanup_observation_visits++;
        }
    }
    _projection_state.route_stats.advertiser_cleanup_count++;
    materialize_dirty_routes_locked (dirty_routes);
}

void registry_t::upsert_topology_entry (const zlink_registry_topology_entry_t &entry_,
                                        uint64_t now_ms_)
{
    topology_key_t key;
    key.service_kind = entry_.service_kind;
    key.service_role = entry_.service_role;
    key.routing_id_key = routing_id_key (entry_.routing_id);
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
        stored.has_owner =
          find_provider_owner_locked (entry_.channel_name, discovery_protocol::service_role_spot,
                                      entry_.endpoint, &stored.owner, NULL);
        if (!stored.has_owner) {
            stored.has_owner = find_provider_owner_locked (entry_.channel_name,
                                                           discovery_protocol::service_role_router,
                                                           entry_.endpoint, &stored.owner, NULL);
        }
    }
    index_topology_owner_locked (key, stored);
    _coordination_state.summary_last_changed_ms = now_ms_;
}

void registry_t::refresh_topology_heartbeat_locked (uint16_t auto_connect_type_,
                                                    uint16_t service_role_,
                                                    const std::string &channel_name_,
                                                    const std::string &endpoint_,
                                                    uint64_t now_ms_)
{
    for (std::map<topology_key_t, topology_entry_t>::iterator it =
           _projection_state.topology.begin ();
         it != _projection_state.topology.end (); ++it) {
        zlink_registry_topology_entry_t &entry = it->second.entry;
        if (static_cast<uint16_t> (entry.auto_connect_type) != auto_connect_type_
            || static_cast<uint16_t> (entry.service_role) != service_role_
            || channel_name_ != entry.channel_name || endpoint_ != entry.endpoint) {
            continue;
        }
        if (entry.state == ZLINK_TOPOLOGY_STATE_STOPPED
            || entry.state == ZLINK_TOPOLOGY_STATE_LOST) {
            continue;
        }
        entry.last_reported_ms = now_ms_;
    }
}

void registry_t::remove_expired (uint64_t now_ms_)
{
    const uint32_t local_registry_id = _coordination_state.registry_id;
    bool changed = false;
    for (service_map_t::iterator sit = _projection_state.services.begin ();
         sit != _projection_state.services.end ();) {
        provider_map_t &providers = sit->second.providers;
        for (provider_map_t::iterator pit = providers.begin (); pit != providers.end ();) {
            if (pit->second.source_registry != local_registry_id) {
                ++pit;
                continue;
            }
            if (now_ms_ > pit->second.last_heartbeat
                && now_ms_ - pit->second.last_heartbeat
                     > _coordination_state.heartbeat_timeout_ms) {
                const owner_identity_t removed_owner =
                  owner_identity_for_provider_locked (sit->first.channel_name, pit->second);
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
                for (provider_map_t::iterator eit = providers.begin (); eit != providers.end ();) {
                    if (eit->second.source_registry == peer_id) {
                        const owner_identity_t removed_owner =
                          owner_identity_for_provider_locked (sit->first.channel_name, eit->second);
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
        } else if (age > report_timeout_ms && entry.state == ZLINK_TOPOLOGY_STATE_READY) {
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
