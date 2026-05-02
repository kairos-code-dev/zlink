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
    service_map_t::const_iterator sit = _services.find (service_key);
    if (sit == _services.end ())
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
    service_map_t::const_iterator sit = _services.find (service_key);
    if (sit == _services.end ())
        return false;
    for (provider_map_t::const_iterator pit = sit->second.providers.begin ();
         pit != sit->second.providers.end (); ++pit) {
        const provider_entry_t &provider = pit->second;
        if (provider.service_role != owner_.service_role)
            continue;
        if (provider.source_registry != owner_.source_registry
            || provider.registration_id != owner_.registration_id) {
            continue;
        }
        if (routing_id_key_of (provider.routing_id) == owner_.routing_id_key)
            return true;
    }
    return false;
}

void registry_t::cleanup_owner_records_locked (const owner_identity_t &owner_,
                                               uint64_t now_ms_)
{
    std::map<owner_identity_t, route_key_set_t>::iterator rit =
      _routes_by_owner.find (owner_);
    if (rit != _routes_by_owner.end ()) {
        route_key_set_t owned = rit->second;
        for (route_key_set_t::const_iterator it = owned.begin ();
             it != owned.end (); ++it) {
            _routes.erase (*it);
        }
        _routes_by_owner.erase (rit);
    }

    for (std::map<topology_key_t, topology_entry_t>::iterator it =
           _topology.begin ();
         it != _topology.end (); ++it) {
        topology_entry_t &row = it->second;
        if (!row.has_owner || row.owner.operator< (owner_)
            || owner_.operator< (row.owner)) {
            continue;
        }
        if (row.entry.state == ZLINK_TOPOLOGY_STATE_READY) {
            row.entry.state = ZLINK_TOPOLOGY_STATE_LOST;
            row.entry.last_reported_ms = now_ms_;
        }
    }
}

void registry_t::cleanup_advertised_route_records_locked (
  uint32_t advertising_registry_)
{
    for (std::map<route_key_t, route_entry_t>::iterator it = _routes.begin ();
         it != _routes.end ();) {
        if (it->second.advertising_registry != advertising_registry_) {
            ++it;
            continue;
        }

        const owner_identity_t owner = it->second.owner;
        std::map<owner_identity_t, route_key_set_t>::iterator owner_it =
          _routes_by_owner.find (owner);
        if (owner_it != _routes_by_owner.end ()) {
            owner_it->second.erase (it->first);
            if (owner_it->second.empty ())
                _routes_by_owner.erase (owner_it);
        }
        it = _routes.erase (it);
    }
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

    topology_entry_t &stored = _topology[key];
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
    _summary_last_changed_ms = now_ms_;
}

void registry_t::send_service_list (void *pub_)
{
    uint32_t registry_id = 0;
    {
        scoped_lock_t lock (_sync);
        registry_id = _registry_id;
        if (registry_id == 0)
            registry_id = 1;
    }

    discovery_protocol::send_u16 (pub_, discovery_protocol::msg_service_list,
                                  ZLINK_SNDMORE);
    discovery_protocol::send_u32 (pub_, registry_id, ZLINK_SNDMORE);
    discovery_protocol::send_u64 (pub_, _list_seq, ZLINK_SNDMORE);

    uint32_t service_count = 0;
    for (service_map_t::const_iterator it = _services.begin ();
         it != _services.end (); ++it) {
        if (!it->second.providers.empty ())
            service_count++;
    }

    discovery_protocol::send_u32 (pub_, service_count,
                                  service_count == 0 ? 0 : ZLINK_SNDMORE);

    if (service_count == 0)
        return;

    uint32_t emitted = 0;
    for (service_map_t::const_iterator it = _services.begin ();
         it != _services.end (); ++it) {
        if (it->second.providers.empty ())
            continue;

        const service_key_t &service_key = it->first;
        const service_entry_t &service = it->second;
        const provider_map_t &providers = service.providers;
        const uint32_t provider_count =
          static_cast<uint32_t> (providers.size ());
        uint64_t contract_created_at = 0;
        std::map<std::string, channel_contract_t>::const_iterator cit =
          _channel_contracts.find (service_key.channel_name);
        if (cit != _channel_contracts.end ())
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
    std::vector<route_entry_t> routes;
    uint64_t list_seq = 0;
    {
        scoped_lock_t lock (_sync);
        registry_id = _registry_id == 0 ? 1 : _registry_id;
        if (_routes.empty () && !_route_snapshot_announced)
            return;
        if (!_routes.empty ())
            _route_snapshot_announced = true;
        else
            _route_snapshot_announced = false;
        list_seq = _list_seq;
        routes.reserve (_routes.size ());
        for (std::map<route_key_t, route_entry_t>::const_iterator it =
               _routes.begin ();
             it != _routes.end (); ++it) {
            routes.push_back (it->second);
        }
    }

    discovery_protocol::send_u16 (pub_, discovery_protocol::msg_registry_sync,
                                  ZLINK_SNDMORE);
    discovery_protocol::send_u32 (pub_, registry_id, ZLINK_SNDMORE);
    discovery_protocol::send_u64 (pub_, list_seq, ZLINK_SNDMORE);
    discovery_protocol::send_u32 (
      pub_, static_cast<uint32_t> (routes.size ()),
      routes.empty () ? 0 : ZLINK_SNDMORE);

    uint32_t emitted = 0;
    for (std::vector<route_entry_t>::const_iterator it = routes.begin ();
         it != routes.end (); ++it, ++emitted) {
        const route_entry_t &entry = *it;
        const bool last_route = emitted + 1 == routes.size ();

        discovery_protocol::send_string (pub_, entry.key.channel_name,
                                         ZLINK_SNDMORE);
        discovery_protocol::send_u32 (pub_, entry.key.kind, ZLINK_SNDMORE);
        discovery_protocol::send_frame (pub_, entry.key.key.data (),
                                        entry.key.key.size (), ZLINK_SNDMORE);
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
        discovery_protocol::send_routing_id (
          pub_, entry.owner_routing_id,
          last_route ? 0 : ZLINK_SNDMORE);
    }
}

void registry_t::remove_expired (uint64_t now_ms_)
{
    const uint32_t local_registry_id = _registry_id;
    bool changed = false;
    for (service_map_t::iterator sit = _services.begin ();
         sit != _services.end ();) {
        provider_map_t &providers = sit->second.providers;
        for (provider_map_t::iterator pit = providers.begin ();
             pit != providers.end ();) {
            if (pit->second.source_registry != local_registry_id) {
                ++pit;
                continue;
            }
            if (now_ms_ > pit->second.last_heartbeat
                && now_ms_ - pit->second.last_heartbeat
                     > _heartbeat_timeout_ms) {
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
            sit = _services.erase (sit);
        else
            ++sit;
    }

    uint64_t peer_timeout_ms = _broadcast_interval_ms;
    if (peer_timeout_ms == 0)
        peer_timeout_ms = 30000;
    peer_timeout_ms *= 3;

    for (std::map<uint32_t, uint64_t>::iterator pit = _peer_last_seen.begin ();
         pit != _peer_last_seen.end ();) {
        const uint32_t peer_id = pit->first;
        if (now_ms_ > pit->second && now_ms_ - pit->second > peer_timeout_ms) {
            cleanup_advertised_route_records_locked (peer_id);
            for (service_map_t::iterator sit = _services.begin ();
                 sit != _services.end ();) {
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
                    sit = _services.erase (sit);
                    continue;
                }
                ++sit;
            }
            _peer_seq.erase (peer_id);
            pit = _peer_last_seen.erase (pit);
            continue;
        }
        ++pit;
    }

    const uint64_t report_timeout_ms = _heartbeat_timeout_ms;
    const uint64_t stale_gc_timeout_ms = report_timeout_ms * 2;
    const uint64_t stopped_gc_timeout_ms = 1000;

    for (std::map<topology_key_t, topology_entry_t>::iterator it =
           _topology.begin ();
         it != _topology.end ();) {
        zlink_registry_topology_entry_t &entry = it->second.entry;
        const uint64_t age =
          now_ms_ > entry.last_reported_ms ? now_ms_ - entry.last_reported_ms : 0;
        if (entry.state == ZLINK_TOPOLOGY_STATE_STOPPED
            || entry.state == ZLINK_TOPOLOGY_STATE_LOST) {
            if (age > stopped_gc_timeout_ms) {
                it = _topology.erase (it);
                changed = true;
                continue;
            }
        } else if (age > stale_gc_timeout_ms) {
            it = _topology.erase (it);
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
        _list_seq++;
        _summary_last_changed_ms = now_ms_;
    }
}
}
