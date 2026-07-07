/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "runtime/services/discovery/discovery_protocol.hpp"
#include "runtime/services/registry/registry.hpp"

namespace zlink
{

void registry_t::send_service_list (void *pub_)
{
    uint32_t registry_id = 0;
    {
        scoped_lock_t lock (_sync);
        registry_id = _coordination_state.registry_id;
        if (registry_id == 0)
            registry_id = 1;
    }

    discovery_protocol::send_u16 (pub_, discovery_protocol::msg_service_list, ZLINK_SNDMORE);
    discovery_protocol::send_u32 (pub_, registry_id, ZLINK_SNDMORE);
    discovery_protocol::send_u64 (pub_, _coordination_state.list_seq, ZLINK_SNDMORE);

    uint32_t service_count = 0;
    for (service_map_t::const_iterator it = _projection_state.services.begin ();
         it != _projection_state.services.end (); ++it) {
        if (!it->second.providers.empty ())
            service_count++;
    }

    discovery_protocol::send_u32 (pub_, service_count, service_count == 0 ? 0 : ZLINK_SNDMORE);

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
        const uint32_t provider_count = static_cast<uint32_t> (providers.size ());
        uint64_t contract_created_at = 0;
        std::map<std::string, channel_contract_t>::const_iterator cit =
          _projection_state.channel_contracts.find (service_key.channel_name);
        if (cit != _projection_state.channel_contracts.end ())
            contract_created_at = cit->second.created_at;

        discovery_protocol::send_u16 (pub_, service.auto_connect_type, ZLINK_SNDMORE);
        discovery_protocol::send_string (pub_, service_key.channel_name, ZLINK_SNDMORE);
        discovery_protocol::send_u64 (pub_, contract_created_at, ZLINK_SNDMORE);
        discovery_protocol::send_u32 (pub_, provider_count, ZLINK_SNDMORE);

        uint32_t provider_index = 0;
        for (provider_map_t::const_iterator pit = providers.begin (); pit != providers.end ();
             ++pit, ++provider_index) {
            const provider_entry_t &entry = pit->second;
            const bool last_provider =
              (provider_index + 1) == provider_count && (emitted + 1) == service_count;

            discovery_protocol::send_u16 (pub_, entry.service_role, ZLINK_SNDMORE);
            discovery_protocol::send_string (pub_, entry.endpoint, ZLINK_SNDMORE);
            discovery_protocol::send_routing_id (pub_, entry.routing_id, ZLINK_SNDMORE);
            discovery_protocol::send_u32 (pub_, entry.source_registry, ZLINK_SNDMORE);
            discovery_protocol::send_u64 (pub_, entry.registration_id, ZLINK_SNDMORE);
            discovery_protocol::send_u64 (pub_, entry.provider_update_seq, ZLINK_SNDMORE);
            discovery_protocol::send_u16 (pub_, static_cast<uint16_t> (entry.weight),
                                          ZLINK_SNDMORE);
            discovery_protocol::send_i64 (pub_, entry.value, ZLINK_SNDMORE);
            discovery_protocol::send_frame (
              pub_, entry.metadata.empty () ? NULL : &entry.metadata[0], entry.metadata.size (),
              last_provider ? 0 : ZLINK_SNDMORE);
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
        const uint32_t route_count = static_cast<uint32_t> (routes.size ());

        discovery_protocol::send_u16 (pub_, discovery_protocol::msg_registry_sync, ZLINK_SNDMORE);
        discovery_protocol::send_u32 (pub_, registry_id, ZLINK_SNDMORE);
        discovery_protocol::send_u64 (pub_, list_seq, ZLINK_SNDMORE);
        discovery_protocol::send_u32 (pub_, chunk_index, ZLINK_SNDMORE);
        discovery_protocol::send_u32 (pub_, chunk_count, ZLINK_SNDMORE);
        discovery_protocol::send_u32 (pub_, route_count, route_count == 0 ? 0 : ZLINK_SNDMORE);

        for (size_t index = 0; index < routes.size (); ++index) {
            const route_entry_t &entry = routes[index];
            const bool last_route = index + 1 == routes.size ();

            discovery_protocol::send_string (pub_, entry.key.channel_name, ZLINK_SNDMORE);
            discovery_protocol::send_u32 (pub_, entry.key.kind, ZLINK_SNDMORE);
            discovery_protocol::send_frame (pub_, entry.key.key.data (), entry.key.key.size (),
                                            ZLINK_SNDMORE);
            discovery_protocol::send_frame (pub_, entry.value.empty () ? NULL : &entry.value[0],
                                            entry.value.size (), ZLINK_SNDMORE);
            discovery_protocol::send_string (pub_, entry.owner.channel_name, ZLINK_SNDMORE);
            discovery_protocol::send_u16 (pub_, entry.owner.service_role, ZLINK_SNDMORE);
            discovery_protocol::send_frame (pub_, entry.owner.routing_id_key.data (),
                                            entry.owner.routing_id_key.size (), ZLINK_SNDMORE);
            discovery_protocol::send_u32 (pub_, entry.owner.source_registry, ZLINK_SNDMORE);
            discovery_protocol::send_u64 (pub_, entry.owner.registration_id, ZLINK_SNDMORE);
            discovery_protocol::send_u64 (pub_, entry.updated_at_ms, ZLINK_SNDMORE);
            zlink_routing_id_t owner_rid;
            if (!owner_routing_id_from_key (entry.owner, &owner_rid))
                memset (&owner_rid, 0, sizeof (owner_rid));
            discovery_protocol::send_routing_id (pub_, owner_rid, last_route ? 0 : ZLINK_SNDMORE);
        }
    }
}

}
