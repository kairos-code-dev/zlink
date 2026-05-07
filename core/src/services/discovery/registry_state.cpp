/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "core/recv_internal.hpp"
#include "services/discovery/registry.hpp"
#include "services/discovery/discovery_protocol.hpp"

#include "utils/clock.hpp"

#include <string.h>
#include <vector>

namespace zlink
{
int registry_t::ensure_channel_contract_locked (
  const std::string &channel_name_,
  uint16_t auto_connect_type_,
  uint64_t now_ms_,
  uint32_t owner_registry_id_)
{
    if (channel_name_.empty ()
        || !discovery_protocol::is_valid_auto_connect_type (
          auto_connect_type_)) {
        errno = EINVAL;
        return -1;
    }

    std::map<std::string, channel_contract_t>::iterator it =
      _projection_state.channel_contracts.find (channel_name_);
    if (it == _projection_state.channel_contracts.end ()) {
        channel_contract_t contract;
        contract.auto_connect_type = auto_connect_type_;
        contract.created_at = now_ms_;
        contract.owner_registry_id = owner_registry_id_ == 0 ? 1
                                                             : owner_registry_id_;
        _projection_state.channel_contracts[channel_name_] = contract;
        return 0;
    }

    if (it->second.auto_connect_type != auto_connect_type_) {
        const uint32_t existing_owner =
          it->second.owner_registry_id == 0 ? 1 : it->second.owner_registry_id;
        const uint32_t incoming_owner =
          owner_registry_id_ == 0 ? 1 : owner_registry_id_;
        const bool incoming_wins =
          incoming_owner < existing_owner
          || (incoming_owner == existing_owner
              && (now_ms_ < it->second.created_at
                  || (now_ms_ == it->second.created_at
                      && auto_connect_type_ < it->second.auto_connect_type)));
        if (incoming_wins) {
            channel_contract_t contract;
            contract.auto_connect_type = auto_connect_type_;
            contract.created_at = now_ms_;
            contract.owner_registry_id = owner_registry_id_ == 0
                                           ? 1
                                           : owner_registry_id_;
            it->second = contract;
            remove_channel_providers_locked (channel_name_);
            return 0;
        }
        errno = EEXIST;
        return -1;
    }
    return 0;
}

void registry_t::remove_channel_providers_locked (
  const std::string &channel_name_)
{
    service_key_t key;
    key.channel_name = channel_name_;
    _projection_state.services.erase (key);
}

void registry_t::handle_peer (void *sub_)
{
    std::vector<zlink_msg_t> frames;
    while (true) {
        zlink_msg_t frame;
        zlink_msg_init (&frame);
        if (recv_msg_internal (sub_, &frame, ZLINK_DONTWAIT) == -1) {
            zlink_msg_close (&frame);
            break;
        }
        frames.push_back (frame);
        if (!msg_frame_has_more (frame))
            break;
    }

    if (frames.empty ())
        return;

    uint16_t msg_id = 0;
    if (zlink_msg_size (&frames[0])
          == sizeof (discovery_protocol::bootstrap_req_t)) {
        discovery_protocol::bootstrap_req_t req;
        memcpy (&req, zlink_msg_data (&frames[0]), sizeof (req));
        msg_id = req.msg_id;
    } else if (!discovery_protocol::read_u16 (frames[0], &msg_id)) {
        close_msg_frames (&frames);
        return;
    }

    if (msg_id != discovery_protocol::msg_service_list
        && msg_id != discovery_protocol::msg_registry_sync) {
        close_msg_frames (&frames);
        return;
    }

    if (frames.size () < 4) {
        close_msg_frames (&frames);
        return;
    }

    uint32_t peer_registry_id = 0;
    uint64_t list_seq = 0;
    uint32_t service_count = 0;
    if (!discovery_protocol::read_u32 (frames[1], &peer_registry_id)
        || !discovery_protocol::read_u64 (frames[2], &list_seq)
        || (msg_id == discovery_protocol::msg_service_list
            && !discovery_protocol::read_u32 (frames[3], &service_count))) {
        close_msg_frames (&frames);
        return;
    }

    service_map_t incoming;
    std::map<std::string, channel_contract_t> incoming_contracts;
    const uint64_t now = zlink::clock_t ().now_ms ();

    uint32_t local_registry_id = 0;
    {
        scoped_lock_t lock (_sync);
        local_registry_id = _coordination_state.registry_id;
        if (local_registry_id == 0)
            local_registry_id = 1;

        if (peer_registry_id == local_registry_id) {
            close_msg_frames (&frames);
            return;
        }

        _projection_state.peer_last_seen[peer_registry_id] = now;
        std::map<uint32_t, uint64_t>::iterator it =
          _projection_state.peer_seq.find (peer_registry_id);
        std::map<uint32_t, uint64_t>::iterator route_it =
          _projection_state.peer_route_seq.find (peer_registry_id);
        if (msg_id == discovery_protocol::msg_service_list
            && it != _projection_state.peer_seq.end () && list_seq <= it->second) {
            close_msg_frames (&frames);
            return;
        }
        if (msg_id == discovery_protocol::msg_registry_sync
            && route_it != _projection_state.peer_route_seq.end ()
            && list_seq <= route_it->second) {
            close_msg_frames (&frames);
            return;
        }
    }

    if (msg_id == discovery_protocol::msg_registry_sync) {
        if (frames.size () < 6) {
            close_msg_frames (&frames);
            return;
        }

        uint32_t chunk_index = 0;
        uint32_t chunk_count = 0;
        uint32_t route_count = 0;
        if (!discovery_protocol::read_u32 (frames[3], &chunk_index)
            || !discovery_protocol::read_u32 (frames[4], &chunk_count)
            || !discovery_protocol::read_u32 (frames[5], &route_count)
            || chunk_count == 0 || chunk_index >= chunk_count) {
            close_msg_frames (&frames);
            return;
        }

        std::vector<route_entry_t> incoming_routes;
        size_t incoming_memory = 0;
        size_t route_index = 6;
        for (uint32_t i = 0; i < route_count && route_index < frames.size ();
             ++i) {
            if (route_index + 10 >= frames.size ())
                break;

            const std::string channel_name =
              discovery_protocol::read_string (frames[route_index++]);
            uint32_t raw_kind = 0;
            if (!discovery_protocol::read_u32 (frames[route_index++],
                                               &raw_kind))
                break;

            route_key_t route_key;
            if (!read_route_key (raw_kind, frames[route_index++],
                                 channel_name, &route_key))
                break;

            route_entry_t route;
            route.key = route_key;
            const size_t value_size = zlink_msg_size (&frames[route_index]);
            if (value_size > ZLINK_ROUTE_VALUE_MAX)
                break;
            if (value_size > 0) {
                const unsigned char *value_data =
                  static_cast<const unsigned char *> (
                    zlink_msg_data (
                      const_cast<zlink_msg_t *> (&frames[route_index])));
                route.value.assign (value_data, value_data + value_size);
            }
            ++route_index;

            route.owner.channel_name =
              discovery_protocol::read_string (frames[route_index++]);
            if (!discovery_protocol::read_u16 (
                  frames[route_index++], &route.owner.service_role))
                break;
            const size_t owner_rid_size = zlink_msg_size (&frames[route_index]);
            if (owner_rid_size > sizeof (((zlink_routing_id_t *) 0)->data))
                break;
            route.owner.routing_id_key.assign (
              static_cast<const char *> (
                zlink_msg_data (const_cast<zlink_msg_t *> (
                  &frames[route_index]))),
              owner_rid_size);
            ++route_index;
            if (!discovery_protocol::read_u32 (
                  frames[route_index++], &route.owner.source_registry)
                || !discovery_protocol::read_u64 (
                  frames[route_index++], &route.owner.registration_id))
                break;
            if (!discovery_protocol::read_u64 (
                  frames[route_index++], &route.updated_at_ms))
                break;
            ++route_index;
            if (route.updated_at_ms == 0)
                route.updated_at_ms = now;
            route.advertising_registry = peer_registry_id;
            incoming_memory += route_entry_memory_bytes (route);
            incoming_routes.push_back (route);
        }

        {
            scoped_lock_t lock (_sync);
            std::map<uint32_t, uint64_t>::iterator route_it =
              _projection_state.peer_route_seq.find (peer_registry_id);
            if (peer_registry_id == local_registry_id
                || (route_it != _projection_state.peer_route_seq.end ()
                    && list_seq <= route_it->second)) {
                close_msg_frames (&frames);
                return;
            }

            route_snapshot_staging_t &staging =
              _projection_state.route_snapshot_staging[peer_registry_id];
            const bool starts_snapshot = chunk_index == 0;
            if (starts_snapshot) {
                staging = route_snapshot_staging_t ();
                staging.seq = list_seq;
                staging.chunk_count = chunk_count;
                staging.active = true;
            }
            if (!staging.active || staging.seq != list_seq
                || staging.chunk_count != chunk_count
                || staging.next_chunk_index != chunk_index
                || staging.memory_bytes + incoming_memory
                     > _projection_state.route_limits.staging_memory_budget_bytes) {
                _projection_state.route_snapshot_staging.erase (peer_registry_id);
                _projection_state.route_stats.snapshot_staging_abort_count++;
                close_msg_frames (&frames);
                return;
            }

            for (std::vector<route_entry_t>::const_iterator it =
                   incoming_routes.begin ();
                 it != incoming_routes.end (); ++it) {
                route_observation_key_t obs_key;
                obs_key.route_key = it->key;
                obs_key.owner = it->owner;
                obs_key.advertising_registry = it->advertising_registry;
                staging.observations[obs_key] = *it;
            }
            staging.memory_bytes += incoming_memory;
            staging.next_chunk_index++;

            if (chunk_index + 1 == chunk_count) {
                route_key_set_t dirty_routes;
                cleanup_advertised_route_records_locked (peer_registry_id);
                for (route_observation_map_t::const_iterator it =
                       staging.observations.begin ();
                     it != staging.observations.end (); ++it) {
                    int route_error = 0;
                    if (!route_store_can_fit_locked (it->second, 0,
                                                     &route_error)) {
                        _projection_state.route_snapshot_staging.erase (peer_registry_id);
                        _projection_state.route_stats.snapshot_staging_abort_count++;
                        close_msg_frames (&frames);
                        return;
                    }
                    upsert_route_observation_locked (it->second,
                                                     &dirty_routes);
                }
                materialize_dirty_routes_locked (dirty_routes);
                _projection_state.peer_route_seq[peer_registry_id] = list_seq;
                _projection_state.route_snapshot_staging.erase (peer_registry_id);
                _coordination_state.list_seq++;
            }
        }

        close_msg_frames (&frames);
        return;
    }

    size_t index = 4;
    for (uint32_t i = 0; i < service_count && index < frames.size (); ++i) {
        if (index + 3 >= frames.size ())
            break;
        uint16_t auto_connect_type = 0;
        if (!discovery_protocol::read_u16 (frames[index++],
                                           &auto_connect_type)
            || !discovery_protocol::is_valid_auto_connect_type (
              auto_connect_type))
            break;
        const std::string channel_name =
          discovery_protocol::read_string (frames[index++]);
        uint64_t contract_created_at = 0;
        if (!discovery_protocol::read_u64 (frames[index++],
                                           &contract_created_at))
            break;
        uint32_t provider_count = 0;
        if (!discovery_protocol::read_u32 (frames[index++], &provider_count))
            break;

        service_key_t service_key;
        service_key.channel_name = channel_name;
        channel_contract_t contract;
        contract.auto_connect_type = auto_connect_type;
        contract.created_at = contract_created_at;
        contract.owner_registry_id = peer_registry_id == 0 ? 1
                                                           : peer_registry_id;
        incoming_contracts[channel_name] = contract;

        service_entry_t &service = incoming[service_key];
        service.auto_connect_type = auto_connect_type;
        for (uint32_t p = 0; p < provider_count && index + 8 < frames.size ();
             ++p) {
            provider_entry_t entry;
            if (!discovery_protocol::read_u16 (frames[index++],
                                               &entry.service_role))
                break;
            if (!discovery_protocol::auto_connect_type_allows_role (
                  auto_connect_type, entry.service_role))
                break;
            entry.endpoint = discovery_protocol::read_string (frames[index++]);
            discovery_protocol::read_routing_id (frames[index++],
                                                 &entry.routing_id);
            uint32_t source_registry = 0;
            discovery_protocol::read_u32 (frames[index++], &source_registry);
            discovery_protocol::read_u64 (frames[index++],
                                          &entry.registration_id);
            discovery_protocol::read_u64 (frames[index++],
                                          &entry.provider_update_seq);
            uint16_t raw_weight = 0;
            if (!discovery_protocol::read_u16 (frames[index++],
                                               &raw_weight))
                break;
            entry.weight = raw_weight <= 100 ? raw_weight : 100;
            int64_t value = 0;
            discovery_protocol::read_i64 (frames[index++], &value);
            entry.value = value;
            const size_t metadata_size = zlink_msg_size (&frames[index]);
            entry.metadata.resize (metadata_size);
            if (metadata_size > 0) {
                memcpy (&entry.metadata[0],
                        zlink_msg_data (const_cast<zlink_msg_t *> (
                          &frames[index])),
                        metadata_size);
            }
            ++index;
            entry.registered_at = now;
            entry.last_heartbeat = now;
            entry.source_registry =
              source_registry == 0 ? peer_registry_id : source_registry;
            if (!entry.endpoint.empty ()) {
                provider_key_t provider_key;
                provider_key.service_role = entry.service_role;
                provider_key.endpoint = entry.endpoint;
                service.providers[provider_key] = entry;
            }
        }
    }

    bool changed = false;
    {
        scoped_lock_t lock (_sync);
        std::map<uint32_t, uint64_t>::iterator it =
          _projection_state.peer_seq.find (peer_registry_id);
        if (peer_registry_id == local_registry_id
            || (it != _projection_state.peer_seq.end () && list_seq <= it->second)) {
            close_msg_frames (&frames);
            return;
        }

        for (std::map<std::string, channel_contract_t>::const_iterator cit =
               incoming_contracts.begin ();
             cit != incoming_contracts.end (); ++cit) {
            const channel_contract_t &contract = cit->second;
            if (ensure_channel_contract_locked (
                  cit->first, contract.auto_connect_type, contract.created_at,
                  contract.owner_registry_id)
                != 0) {
                service_key_t rejected_key;
                rejected_key.channel_name = cit->first;
                incoming.erase (rejected_key);
            }
        }

        for (service_map_t::const_iterator sit = incoming.begin ();
             sit != incoming.end (); ++sit) {
            const service_key_t &service_key = sit->first;
            const provider_map_t &providers = sit->second.providers;
            service_map_t::const_iterator existing_service =
              _projection_state.services.find (service_key);
            for (provider_map_t::const_iterator pit = providers.begin ();
                 pit != providers.end (); ++pit) {
                bool match = false;
                if (existing_service != _projection_state.services.end ()) {
                    provider_map_t::const_iterator ep =
                      existing_service->second.providers.find (pit->first);
                    if (ep != existing_service->second.providers.end ()
                        && ep->second.source_registry == peer_registry_id) {
                        const provider_entry_t &cur = ep->second;
                        const provider_entry_t &incoming_entry = pit->second;
                        match =
                          cur.service_role == incoming_entry.service_role
                          && cur.weight
                               == incoming_entry.weight
                          && cur.value == incoming_entry.value
                          && cur.metadata == incoming_entry.metadata
                          && cur.source_registry
                               == incoming_entry.source_registry
                          && cur.registration_id
                               == incoming_entry.registration_id
                          && cur.provider_update_seq
                               == incoming_entry.provider_update_seq
                          && cur.routing_id.size
                               == incoming_entry.routing_id.size
                          && (cur.routing_id.size == 0
                              || memcmp (cur.routing_id.data,
                                         incoming_entry.routing_id.data,
                                         cur.routing_id.size)
                                   == 0);
                    } else if (ep != existing_service->second.providers.end ()
                               && ep->second.source_registry
                                    != peer_registry_id) {
                        match = true;
                    }
                }
                if (!match) {
                    changed = true;
                    break;
                }
            }
            if (changed)
                break;
        }

        if (!changed) {
            for (service_map_t::const_iterator sit = _projection_state.services.begin ();
                 sit != _projection_state.services.end (); ++sit) {
                const provider_map_t &providers = sit->second.providers;
                for (provider_map_t::const_iterator pit = providers.begin ();
                     pit != providers.end (); ++pit) {
                    if (pit->second.source_registry != peer_registry_id)
                        continue;
                    service_map_t::const_iterator incoming_service =
                      incoming.find (sit->first);
                    if (incoming_service == incoming.end ()
                        || incoming_service->second.providers.find (pit->first)
                             == incoming_service->second.providers.end ()) {
                        changed = true;
                        break;
                    }
                }
                if (changed)
                    break;
            }
        }

        if (!changed) {
            _projection_state.peer_seq[peer_registry_id] = list_seq;
            close_msg_frames (&frames);
            return;
        }

        for (service_map_t::iterator sit = _projection_state.services.begin ();
             sit != _projection_state.services.end ();) {
            provider_map_t &providers = sit->second.providers;
            for (provider_map_t::iterator pit = providers.begin ();
                 pit != providers.end ();) {
                if (pit->second.source_registry == peer_registry_id) {
                    owner_identity_t removed_owner;
                    removed_owner.channel_name = sit->first.channel_name;
                    removed_owner.service_role = pit->second.service_role;
                    removed_owner.routing_id_key.assign (
                      reinterpret_cast<const char *> (
                        pit->second.routing_id.data),
                      pit->second.routing_id.size);
                    removed_owner.source_registry = pit->second.source_registry;
                    removed_owner.registration_id = pit->second.registration_id;
                    cleanup_owner_records_locked (removed_owner,
                                                  zlink::clock_t ().now_ms ());
                    pit = providers.erase (pit);
                    continue;
                }
                ++pit;
            }
            if (providers.empty ()) {
                sit = _projection_state.services.erase (sit);
                continue;
            }
            ++sit;
        }

        for (service_map_t::const_iterator sit = incoming.begin ();
             sit != incoming.end (); ++sit) {
            const service_key_t &service_key = sit->first;
            const provider_map_t &providers = sit->second.providers;
            service_entry_t &service = _projection_state.services[service_key];
            service.auto_connect_type = sit->second.auto_connect_type;
            for (provider_map_t::const_iterator pit = providers.begin ();
                 pit != providers.end (); ++pit) {
                provider_map_t::iterator existing =
                  service.providers.find (pit->first);
                if (existing != service.providers.end ()
                    && existing->second.source_registry != peer_registry_id) {
                    continue;
                }
                service.providers[pit->first] = pit->second;
                owner_identity_t owner;
                owner.channel_name = service_key.channel_name;
                owner.service_role = pit->second.service_role;
                owner.routing_id_key.assign (
                  reinterpret_cast<const char *> (pit->second.routing_id.data),
                  pit->second.routing_id.size);
                owner.source_registry = pit->second.source_registry;
                owner.registration_id = pit->second.registration_id;
                promote_owner_route_records_locked (owner);
            }
        }

        _projection_state.peer_seq[peer_registry_id] = list_seq;
        _coordination_state.list_seq++;
    }

    close_msg_frames (&frames);
}

void registry_t::handle_register (void *router_,
                                  const zlink_msg_t *frames_,
                                  size_t frame_count_,
                                  const zlink_routing_id_t &sender_id_)
{
    if (frame_count_ < 5) {
        send_register_ack (router_, sender_id_, discovery_protocol::status_invalid, std::string (), 0, 0,
                           "invalid register");
        return;
    }

    uint16_t auto_connect_type = 0;
    if (!discovery_protocol::read_u16 (frames_[1], &auto_connect_type)
        || !discovery_protocol::is_valid_auto_connect_type (
          auto_connect_type)) {
        send_register_ack (router_, sender_id_, discovery_protocol::status_invalid, std::string (), 0, 0,
                           "invalid type");
        return;
    }
    uint16_t service_role = 0;
    if (!discovery_protocol::read_u16 (frames_[2], &service_role)
        || !discovery_protocol::auto_connect_type_allows_role (
          auto_connect_type, service_role)) {
        send_register_ack (router_, sender_id_, discovery_protocol::status_invalid, std::string (), 0, 0,
                           "invalid role");
        return;
    }
    const std::string channel_name =
      discovery_protocol::read_string (frames_[3]);
    const std::string endpoint =
      discovery_protocol::read_string (frames_[4]);

    if (channel_name.empty () || endpoint.empty ()) {
        send_register_ack (router_, sender_id_, discovery_protocol::status_rejected, endpoint, 0, 0,
                           "invalid endpoint");
        return;
    }

    uint32_t weight = 100;
    if (frame_count_ >= 6) {
        uint16_t raw_weight = 0;
        if (discovery_protocol::read_u16 (frames_[5], &raw_weight)
            && raw_weight <= 100) {
            weight = static_cast<uint32_t> (raw_weight);
        }
    }
    int64_t value = 0;
    if (frame_count_ >= 7)
        discovery_protocol::read_i64 (frames_[6], &value);
    std::vector<unsigned char> metadata;
    if (frame_count_ >= 8) {
        const size_t metadata_size = zlink_msg_size (&frames_[7]);
        metadata.resize (metadata_size);
        if (metadata_size > 0)
            memcpy (&metadata[0],
                    zlink_msg_data (const_cast<zlink_msg_t *> (&frames_[7])),
                    metadata_size);
    }

    const uint64_t now = zlink::clock_t ().now_ms ();

    uint8_t status = discovery_protocol::status_ok;
    uint32_t source_registry = 0;
    uint64_t registration_id = 0;
    std::string error;
    {
        scoped_lock_t lock (_sync);
        uint32_t registry_id = _coordination_state.registry_id == 0 ? 1 : _coordination_state.registry_id;
        if (ensure_channel_contract_locked (channel_name, auto_connect_type,
                                            now, registry_id)
            != 0) {
            status = errno == EEXIST ? discovery_protocol::status_conflict : discovery_protocol::status_invalid;
            error = "channel type conflict";
        } else {
            service_key_t service_key;
            service_key.channel_name = channel_name;
            service_entry_t &service = _projection_state.services[service_key];
            service.auto_connect_type = auto_connect_type;
            provider_key_t provider_key;
            provider_key.service_role = service_role;
            provider_key.endpoint = endpoint;
            provider_map_t::iterator existing =
              service.providers.find (provider_key);
            if (existing != service.providers.end ()) {
                owner_identity_t old_owner;
                old_owner.channel_name = channel_name;
                old_owner.service_role = existing->second.service_role;
                old_owner.routing_id_key.assign (
                  reinterpret_cast<const char *> (
                    existing->second.routing_id.data),
                  existing->second.routing_id.size);
                old_owner.source_registry = existing->second.source_registry;
                old_owner.registration_id = existing->second.registration_id;
                cleanup_owner_records_locked (old_owner, now);
            }
            provider_entry_t &entry = service.providers[provider_key];
            entry.service_role = service_role;
            entry.endpoint = endpoint;
            entry.routing_id = sender_id_;
            entry.weight = weight;
            entry.value = value;
            entry.metadata = metadata;
            entry.registration_id = _coordination_state.next_registration_id++;
            entry.provider_update_seq = _coordination_state.next_provider_update_seq++;
            entry.registered_at = now;
            entry.last_heartbeat = now;
            entry.source_registry = registry_id;
            source_registry = entry.source_registry;
            registration_id = entry.registration_id;

            owner_identity_t owner;
            owner.channel_name = channel_name;
            owner.service_role = entry.service_role;
            owner.routing_id_key.assign (
              reinterpret_cast<const char *> (entry.routing_id.data),
              entry.routing_id.size);
            owner.source_registry = entry.source_registry;
            owner.registration_id = entry.registration_id;
            promote_owner_route_records_locked (owner);

            _coordination_state.list_seq++;
        }
    }

    send_register_ack (router_, sender_id_, status, endpoint, source_registry,
                       registration_id, error);
}

void registry_t::handle_unregister (void *router_,
                                    const zlink_msg_t *frames_,
                                    size_t frame_count_,
                                    const zlink_routing_id_t &sender_id_)
{
    if (frame_count_ < 5) {
        send_unregister_ack (router_, sender_id_, discovery_protocol::status_invalid, "invalid unregister");
        return;
    }

    uint16_t auto_connect_type = 0;
    if (!discovery_protocol::read_u16 (frames_[1], &auto_connect_type)) {
        send_unregister_ack (router_, sender_id_, discovery_protocol::status_invalid, "invalid type");
        return;
    }
    uint16_t service_role = 0;
    if (!discovery_protocol::read_u16 (frames_[2], &service_role)
        || !discovery_protocol::auto_connect_type_allows_role (
          auto_connect_type, service_role)) {
        send_unregister_ack (router_, sender_id_, discovery_protocol::status_invalid, "invalid role");
        return;
    }
    const std::string channel_name =
      discovery_protocol::read_string (frames_[3]);
    const std::string endpoint =
      discovery_protocol::read_string (frames_[4]);

    service_key_t service_key;
    service_key.channel_name = channel_name;

    service_map_t::iterator sit = _projection_state.services.find (service_key);
    if (sit == _projection_state.services.end ()) {
        send_unregister_ack (router_, sender_id_, discovery_protocol::status_not_found, "service not found");
        return;
    }

    provider_key_t provider_key;
    provider_key.service_role = service_role;
    provider_key.endpoint = endpoint;
    provider_map_t::iterator pit = sit->second.providers.find (provider_key);
    if (pit == sit->second.providers.end ()) {
        send_unregister_ack (router_, sender_id_, discovery_protocol::status_not_found, "endpoint not found");
        return;
    }
    if (pit->second.source_registry != _coordination_state.registry_id) {
        send_unregister_ack (router_, sender_id_, discovery_protocol::status_not_found, "foreign provider");
        return;
    }

    owner_identity_t removed_owner;
    removed_owner.channel_name = channel_name;
    removed_owner.service_role = pit->second.service_role;
    removed_owner.routing_id_key.assign (
      reinterpret_cast<const char *> (pit->second.routing_id.data),
      pit->second.routing_id.size);
    removed_owner.source_registry = pit->second.source_registry;
    removed_owner.registration_id = pit->second.registration_id;
    cleanup_owner_records_locked (removed_owner, zlink::clock_t ().now_ms ());

    sit->second.providers.erase (pit);
    if (sit->second.providers.empty ())
        _projection_state.services.erase (sit);

    _coordination_state.list_seq++;
    send_unregister_ack (router_, sender_id_, discovery_protocol::status_ok, std::string ());
}

void registry_t::handle_heartbeat (const zlink_msg_t *frames_,
                                   size_t frame_count_)
{
    if (frame_count_ < 5)
        return;

    uint16_t auto_connect_type = 0;
    if (!discovery_protocol::read_u16 (frames_[1], &auto_connect_type))
        return;
    uint16_t service_role = 0;
    if (!discovery_protocol::read_u16 (frames_[2], &service_role)
        || !discovery_protocol::auto_connect_type_allows_role (
          auto_connect_type, service_role)) {
        return;
    }
    const std::string channel_name =
      discovery_protocol::read_string (frames_[3]);
    const std::string endpoint =
      discovery_protocol::read_string (frames_[4]);

    service_key_t service_key;
    service_key.channel_name = channel_name;

    service_map_t::iterator sit = _projection_state.services.find (service_key);
    if (sit == _projection_state.services.end ())
        return;

    provider_key_t provider_key;
    provider_key.service_role = service_role;
    provider_key.endpoint = endpoint;
    provider_map_t::iterator pit = sit->second.providers.find (provider_key);
    if (pit == sit->second.providers.end ())
        return;

    pit->second.last_heartbeat = zlink::clock_t ().now_ms ();
}

void registry_t::handle_update_attributes (void *router_,
                                           const zlink_msg_t *frames_,
                                           size_t frame_count_,
                                           const zlink_routing_id_t &sender_id_)
{
    if (frame_count_ < 7) {
        send_register_ack (router_, sender_id_, discovery_protocol::status_invalid, std::string (), 0, 0,
                           "invalid update");
        return;
    }

    uint16_t auto_connect_type = 0;
    if (!discovery_protocol::read_u16 (frames_[1], &auto_connect_type)
        || !discovery_protocol::is_valid_auto_connect_type (
          auto_connect_type)) {
        send_register_ack (router_, sender_id_, discovery_protocol::status_invalid, std::string (), 0, 0,
                           "invalid type");
        return;
    }
    uint16_t service_role = 0;
    if (!discovery_protocol::read_u16 (frames_[2], &service_role)
        || !discovery_protocol::auto_connect_type_allows_role (
          auto_connect_type, service_role)) {
        send_register_ack (router_, sender_id_, discovery_protocol::status_invalid, std::string (), 0, 0,
                           "invalid role");
        return;
    }
    const std::string channel_name =
      discovery_protocol::read_string (frames_[3]);
    const std::string endpoint =
      discovery_protocol::read_string (frames_[4]);
    uint32_t weight = 100;
    uint16_t raw_weight = 0;
    if (discovery_protocol::read_u16 (frames_[5], &raw_weight)
        && raw_weight <= 100) {
        weight = static_cast<uint32_t> (raw_weight);
    }
    int64_t value = 0;
    discovery_protocol::read_i64 (frames_[6], &value);
    std::vector<unsigned char> metadata;
    if (frame_count_ >= 8) {
        const size_t metadata_size = zlink_msg_size (&frames_[7]);
        metadata.resize (metadata_size);
        if (metadata_size > 0)
            memcpy (&metadata[0],
                    zlink_msg_data (const_cast<zlink_msg_t *> (&frames_[7])),
                    metadata_size);
    }

    service_key_t service_key;
    service_key.channel_name = channel_name;
    service_map_t::iterator sit = _projection_state.services.find (service_key);
    if (sit == _projection_state.services.end ()) {
        send_register_ack (router_, sender_id_, discovery_protocol::status_not_found, endpoint, 0, 0,
                           "service not found");
        return;
    }

    provider_key_t provider_key;
    provider_key.service_role = service_role;
    provider_key.endpoint = endpoint;
    provider_map_t::iterator pit = sit->second.providers.find (provider_key);
    if (pit == sit->second.providers.end ()) {
        send_register_ack (router_, sender_id_, discovery_protocol::status_not_found, endpoint, 0, 0,
                           "provider not found");
        return;
    }
    const uint32_t local_registry_id = _coordination_state.registry_id == 0 ? 1 : _coordination_state.registry_id;
    if (pit->second.source_registry != local_registry_id) {
        send_register_ack (router_, sender_id_, discovery_protocol::status_not_found, endpoint, 0, 0,
                           "provider not local");
        return;
    }

    pit->second.weight = weight;
    pit->second.value = value;
    pit->second.metadata = metadata;
    pit->second.provider_update_seq = _coordination_state.next_provider_update_seq++;
    _coordination_state.list_seq++;
    send_register_ack (router_, sender_id_, discovery_protocol::status_ok, endpoint,
                       pit->second.source_registry,
                       pit->second.registration_id, std::string ());
}
}
