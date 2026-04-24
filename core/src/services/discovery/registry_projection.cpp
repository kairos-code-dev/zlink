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

void registry_t::upsert_topology_entry (
  const zlink_registry_topology_entry_t &entry_,
  uint64_t now_ms_)
{
    topology_key_t key;
    key.service_kind = entry_.service_kind;
    key.service_role = entry_.service_role;
    key.routing_id_key = routing_id_key_of (entry_.routing_id);
    key.service_name = entry_.service_name;
    key.endpoint = entry_.endpoint;

    topology_entry_t &stored = _topology[key];
    stored.entry = entry_;
    stored.entry.last_reported_ms = now_ms_;
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
        const provider_map_t &providers = it->second.providers;
        const uint32_t provider_count =
          static_cast<uint32_t> (providers.size ());

        discovery_protocol::send_u16 (pub_, service_key.service_type,
                                      ZLINK_SNDMORE);
        discovery_protocol::send_string (pub_, service_key.service_name,
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
            for (service_map_t::iterator sit = _services.begin ();
                 sit != _services.end ();) {
                provider_map_t &providers = sit->second.providers;
                for (provider_map_t::iterator eit = providers.begin ();
                     eit != providers.end ();) {
                    if (eit->second.source_registry == peer_id) {
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
        if (entry.state == ZLINK_TOPOLOGY_STATE_STOPPED) {
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
