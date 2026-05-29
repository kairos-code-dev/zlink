/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "core/recv_internal.hpp"
#include "services/registry/registry.hpp"
#include "services/discovery/discovery_protocol.hpp"

#include "utils/clock.hpp"
#include "utils/routing_id.hpp"

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

bool registry_t::read_peer_frames (void *sub_, scoped_msg_frames_t *frames_) const
{
    if (!frames_)
        return false;
    while (true) {
        zlink_msg_t frame;
        zlink_msg_init (&frame);
        if (recv_msg_internal (sub_, &frame, ZLINK_DONTWAIT) == -1) {
            zlink_msg_close (&frame);
            break;
        }
        frames_->push_back (frame);
        if (!msg_frame_has_more (frame))
            break;
    }
    return !frames_->empty ();
}

bool registry_t::decode_peer_header (const scoped_msg_frames_t &frames_,
                                     uint16_t *msg_id_out_,
                                     uint32_t *peer_registry_id_out_,
                                     uint64_t *list_seq_out_) const
{
    if (!msg_id_out_ || !peer_registry_id_out_ || !list_seq_out_
        || frames_.size () < 4)
        return false;

    uint16_t msg_id = 0;
    if (zlink_msg_size (&frames_[0])
          == sizeof (discovery_protocol::bootstrap_req_t)) {
        discovery_protocol::bootstrap_req_t req;
        memcpy (&req, zlink_msg_data (
                        const_cast<zlink_msg_t *> (&frames_[0])),
                sizeof (req));
        msg_id = req.msg_id;
    } else if (!discovery_protocol::read_u16 (frames_[0], &msg_id)) {
        return false;
    }

    if (msg_id != discovery_protocol::msg_service_list
        && msg_id != discovery_protocol::msg_registry_sync)
        return false;

    uint32_t peer_registry_id = 0;
    uint64_t list_seq = 0;
    uint32_t service_count = 0;
    if (!discovery_protocol::read_u32 (frames_[1], &peer_registry_id)
        || !discovery_protocol::read_u64 (frames_[2], &list_seq)
        || (msg_id == discovery_protocol::msg_service_list
            && !discovery_protocol::read_u32 (frames_[3], &service_count)))
        return false;

    *msg_id_out_ = msg_id;
    *peer_registry_id_out_ = peer_registry_id;
    *list_seq_out_ = list_seq;
    return true;
}

bool registry_t::accept_peer_message_locked (
  uint16_t msg_id_,
  uint32_t peer_registry_id_,
  uint64_t list_seq_,
  uint64_t now_ms_,
  uint32_t *local_registry_id_out_)
{
    uint32_t local_registry_id = _coordination_state.registry_id;
    if (local_registry_id == 0)
        local_registry_id = 1;
    if (local_registry_id_out_)
        *local_registry_id_out_ = local_registry_id;

    if (peer_registry_id_ == local_registry_id)
        return false;

    _projection_state.peer_last_seen[peer_registry_id_] = now_ms_;
    std::map<uint32_t, uint64_t>::iterator it =
      _projection_state.peer_seq.find (peer_registry_id_);
    std::map<uint32_t, uint64_t>::iterator route_it =
      _projection_state.peer_route_seq.find (peer_registry_id_);
    if (msg_id_ == discovery_protocol::msg_service_list
        && it != _projection_state.peer_seq.end () && list_seq_ <= it->second)
        return false;
    if (msg_id_ == discovery_protocol::msg_registry_sync
        && route_it != _projection_state.peer_route_seq.end ()
        && list_seq_ <= route_it->second)
        return false;
    return true;
}

void registry_t::handle_peer_route_sync (
  const scoped_msg_frames_t &frames_,
  uint32_t peer_registry_id,
  uint64_t list_seq,
  uint64_t now,
  uint32_t local_registry_id)
{
    discovery_protocol::route_list_t route_list;
    if (!discovery_protocol::decode_route_list (frames_, &route_list)) {
        return;
    }

    std::vector<route_entry_t> incoming_routes;
    size_t incoming_memory = 0;
    for (size_t i = 0; i < route_list.routes.size (); ++i) {
        const discovery_protocol::route_record_t &record =
          route_list.routes[i];
        route_key_t route_key;
        route_key.channel_name = record.channel_name;
        route_key.kind = static_cast<zlink_route_kind_t> (record.raw_kind);
        if (route_key.channel_name.empty ()
            || route_key.kind == ZLINK_ROUTE_KIND_INVALID
            || record.key.empty () || record.key.size () > ZLINK_ROUTE_KEY_MAX) {
            return;
        }
        route_key.key.assign (
          reinterpret_cast<const char *> (&record.key[0]),
          record.key.size ());

        route_entry_t route;
        route.key = route_key;
        route.value = record.value;
        route.owner.channel_name = record.owner_channel_name;
        route.owner.service_role = record.owner_service_role;
        if (!record.owner_routing_id_key.empty ()) {
            route.owner.routing_id_key.assign (
              reinterpret_cast<const char *> (
                &record.owner_routing_id_key[0]),
              record.owner_routing_id_key.size ());
        }
        route.owner.source_registry = record.owner_source_registry;
        route.owner.registration_id = record.owner_registration_id;
        route.updated_at_ms = record.updated_at_ms;
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
            return;
        }

        route_snapshot_staging_t &staging =
          _projection_state.route_snapshot_staging[peer_registry_id];
        const bool starts_snapshot = route_list.chunk_index == 0;
        if (starts_snapshot) {
            staging = route_snapshot_staging_t ();
            staging.seq = list_seq;
            staging.chunk_count = route_list.chunk_count;
            staging.active = true;
        }
        if (!staging.active || staging.seq != list_seq
            || staging.chunk_count != route_list.chunk_count
            || staging.next_chunk_index != route_list.chunk_index
            || staging.memory_bytes + incoming_memory
                 > _projection_state.route_limits.staging_memory_budget_bytes) {
            _projection_state.route_snapshot_staging.erase (peer_registry_id);
            _projection_state.route_stats.snapshot_staging_abort_count++;
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

        if (route_list.chunk_index + 1 == route_list.chunk_count) {
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
    return;
}

void registry_t::handle_peer_service_list (
  const scoped_msg_frames_t &frames_,
  uint32_t peer_registry_id,
  uint64_t list_seq,
  uint64_t now,
  uint32_t local_registry_id)
{
    discovery_protocol::service_list_t service_list;
    if (!discovery_protocol::decode_service_list (frames_, &service_list)) {
        return;
    }
    service_map_t incoming;
    std::map<std::string, channel_contract_t> incoming_contracts;
    for (size_t i = 0; i < service_list.services.size (); ++i) {
        const discovery_protocol::service_record_t &record =
          service_list.services[i];
        service_key_t service_key;
        service_key.channel_name = record.channel_name;
        channel_contract_t contract;
        contract.auto_connect_type = record.auto_connect_type;
        contract.created_at = record.contract_created_at;
        contract.owner_registry_id = peer_registry_id == 0 ? 1
                                                           : peer_registry_id;
        incoming_contracts[record.channel_name] = contract;

        service_entry_t &service = incoming[service_key];
        service.auto_connect_type = record.auto_connect_type;
        for (size_t p = 0; p < record.providers.size (); ++p) {
            const discovery_protocol::service_provider_record_t &provider =
              record.providers[p];
            provider_entry_t entry;
            entry.service_role = provider.service_role;
            entry.endpoint = provider.endpoint;
            entry.routing_id = provider.routing_id;
            entry.registration_id = provider.registration_id;
            entry.provider_update_seq = provider.provider_update_seq;
            entry.weight = provider.weight <= 100 ? provider.weight : 100;
            entry.value = provider.value;
            entry.metadata = provider.metadata;
            entry.registered_at = now;
            entry.last_heartbeat = now;
            entry.source_registry =
              provider.source_registry == 0 ? peer_registry_id
                                            : provider.source_registry;
            if (!entry.endpoint.empty ()) {
                provider_key_t provider_key;
                provider_key.service_role = entry.service_role;
                provider_key.endpoint = entry.endpoint;
                service.providers[provider_key] = entry;
            }
        }
    }

    {
        scoped_lock_t lock (_sync);
        std::map<uint32_t, uint64_t>::iterator it =
          _projection_state.peer_seq.find (peer_registry_id);
        if (peer_registry_id == local_registry_id
            || (it != _projection_state.peer_seq.end () && list_seq <= it->second)) {
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

        if (!peer_service_snapshot_changed_locked (incoming,
                                                   peer_registry_id)) {
            _projection_state.peer_seq[peer_registry_id] = list_seq;
            return;
        }

        remove_peer_service_providers_locked (peer_registry_id, now);
        apply_peer_service_snapshot_locked (incoming, peer_registry_id);

        _projection_state.peer_seq[peer_registry_id] = list_seq;
        _coordination_state.list_seq++;
    }
}

bool registry_t::peer_provider_matches_locked (
  const provider_entry_t &current_,
  const provider_entry_t &incoming_,
  uint32_t peer_registry_id_) const
{
    if (current_.source_registry != peer_registry_id_)
        return true;
    return current_.service_role == incoming_.service_role
           && current_.weight == incoming_.weight
           && current_.value == incoming_.value
           && current_.metadata == incoming_.metadata
           && current_.source_registry == incoming_.source_registry
           && current_.registration_id == incoming_.registration_id
           && current_.provider_update_seq == incoming_.provider_update_seq
           && current_.routing_id.size == incoming_.routing_id.size
           && (current_.routing_id.size == 0
               || memcmp (current_.routing_id.data, incoming_.routing_id.data,
                          current_.routing_id.size)
                    == 0);
}

bool registry_t::peer_service_snapshot_changed_locked (
  const service_map_t &incoming_, uint32_t peer_registry_id_) const
{
    for (service_map_t::const_iterator sit = incoming_.begin ();
         sit != incoming_.end (); ++sit) {
        service_map_t::const_iterator existing_service =
          _projection_state.services.find (sit->first);
        const provider_map_t &providers = sit->second.providers;
        for (provider_map_t::const_iterator pit = providers.begin ();
             pit != providers.end (); ++pit) {
            if (existing_service == _projection_state.services.end ())
                return true;
            provider_map_t::const_iterator existing_provider =
              existing_service->second.providers.find (pit->first);
            if (existing_provider == existing_service->second.providers.end ())
                return true;
            if (!peer_provider_matches_locked (
                  existing_provider->second, pit->second, peer_registry_id_))
                return true;
        }
    }

    for (service_map_t::const_iterator sit = _projection_state.services.begin ();
         sit != _projection_state.services.end (); ++sit) {
        const provider_map_t &providers = sit->second.providers;
        for (provider_map_t::const_iterator pit = providers.begin ();
             pit != providers.end (); ++pit) {
            if (pit->second.source_registry != peer_registry_id_)
                continue;
            service_map_t::const_iterator incoming_service =
              incoming_.find (sit->first);
            if (incoming_service == incoming_.end ()
                || incoming_service->second.providers.find (pit->first)
                     == incoming_service->second.providers.end ())
                return true;
        }
    }

    return false;
}

void registry_t::remove_peer_service_providers_locked (
  uint32_t peer_registry_id_, uint64_t now_ms_)
{
    for (service_map_t::iterator sit = _projection_state.services.begin ();
         sit != _projection_state.services.end ();) {
        provider_map_t &providers = sit->second.providers;
        for (provider_map_t::iterator pit = providers.begin ();
             pit != providers.end ();) {
            if (pit->second.source_registry == peer_registry_id_) {
                owner_identity_t removed_owner;
                removed_owner.channel_name = sit->first.channel_name;
                removed_owner.service_role = pit->second.service_role;
                removed_owner.routing_id_key =
                  zlink::routing_id_key (pit->second.routing_id);
                removed_owner.source_registry = pit->second.source_registry;
                removed_owner.registration_id = pit->second.registration_id;
                cleanup_owner_records_locked (removed_owner, now_ms_);
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
}

void registry_t::apply_peer_service_snapshot_locked (
  const service_map_t &incoming_, uint32_t peer_registry_id_)
{
    for (service_map_t::const_iterator sit = incoming_.begin ();
         sit != incoming_.end (); ++sit) {
        const service_key_t &service_key = sit->first;
        const provider_map_t &providers = sit->second.providers;
        service_entry_t &service = _projection_state.services[service_key];
        service.auto_connect_type = sit->second.auto_connect_type;
        for (provider_map_t::const_iterator pit = providers.begin ();
             pit != providers.end (); ++pit) {
            provider_map_t::iterator existing =
              service.providers.find (pit->first);
            if (existing != service.providers.end ()
                && existing->second.source_registry != peer_registry_id_)
                continue;
            service.providers[pit->first] = pit->second;
            owner_identity_t owner;
            owner.channel_name = service_key.channel_name;
            owner.service_role = pit->second.service_role;
            owner.routing_id_key =
              zlink::routing_id_key (pit->second.routing_id);
            owner.source_registry = pit->second.source_registry;
            owner.registration_id = pit->second.registration_id;
            promote_owner_route_records_locked (owner);
        }
    }
}

void registry_t::handle_peer (void *sub_)
{
    scoped_msg_frames_t frames;
    if (!read_peer_frames (sub_, &frames))
        return;

    uint16_t msg_id = 0;
    uint32_t peer_registry_id = 0;
    uint64_t list_seq = 0;
    if (!decode_peer_header (frames, &msg_id, &peer_registry_id, &list_seq))
        return;

    const uint64_t now = zlink::clock_t ().now_ms ();
    uint32_t local_registry_id = 0;
    {
        scoped_lock_t lock (_sync);
        if (!accept_peer_message_locked (msg_id, peer_registry_id, list_seq,
                                         now, &local_registry_id))
            return;
    }

    if (msg_id == discovery_protocol::msg_registry_sync) {
        handle_peer_route_sync (frames, peer_registry_id, list_seq, now,
                                local_registry_id);
        return;
    }

    handle_peer_service_list (frames, peer_registry_id, list_seq, now,
                              local_registry_id);
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
    if (frame_count_ >= 8)
        discovery_protocol::read_bytes (frames_[7], &metadata);

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
                old_owner.routing_id_key =
                  zlink::routing_id_key (existing->second.routing_id);
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
            owner.routing_id_key = zlink::routing_id_key (entry.routing_id);
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
    removed_owner.routing_id_key =
      zlink::routing_id_key (pit->second.routing_id);
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
    if (frame_count_ >= 8)
        discovery_protocol::read_bytes (frames_[7], &metadata);

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
