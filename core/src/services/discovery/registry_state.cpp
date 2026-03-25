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
namespace
{
bool registry_frame_has_more (const zlink_msg_t &frame_)
{
    return (reinterpret_cast<const msg_t *> (&frame_)->flags () & msg_t::more)
           != 0;
}

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

    topology_entry_t &stored = _topology[key];
    stored.entry = entry_;
    stored.entry.last_reported_ms = now_ms_;
    _summary_last_changed_ms = now_ms_;
}

void registry_t::upsert_gateway_peer_entry (
  const zlink_registry_gateway_peer_entry_t &entry_,
  uint64_t now_ms_)
{
    gateway_peer_key_t key;
    key.gateway_routing_id_key = routing_id_key_of (entry_.gateway_routing_id);
    key.service_name = entry_.service_name;
    key.peer_routing_id_key = routing_id_key_of (entry_.peer_routing_id);

    gateway_peer_entry_t &stored = _gateway_peers[key];
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
            discovery_protocol::send_u32 (pub_, entry.weight,
                                          last_provider ? 0 : ZLINK_SNDMORE);
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
        if (!registry_frame_has_more (frame))
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
        for (size_t i = 0; i < frames.size (); ++i)
            zlink_msg_close (&frames[i]);
        return;
    }

    if (msg_id != discovery_protocol::msg_service_list
        && msg_id != discovery_protocol::msg_registry_sync) {
        for (size_t i = 0; i < frames.size (); ++i)
            zlink_msg_close (&frames[i]);
        return;
    }

    if (frames.size () < 4) {
        for (size_t i = 0; i < frames.size (); ++i)
            zlink_msg_close (&frames[i]);
        return;
    }

    uint32_t peer_registry_id = 0;
    uint64_t list_seq = 0;
    uint32_t service_count = 0;
    if (!discovery_protocol::read_u32 (frames[1], &peer_registry_id)
        || !discovery_protocol::read_u64 (frames[2], &list_seq)
        || !discovery_protocol::read_u32 (frames[3], &service_count)) {
        for (size_t i = 0; i < frames.size (); ++i)
            zlink_msg_close (&frames[i]);
        return;
    }

    service_map_t incoming;
    const uint64_t now = zlink::clock_t ().now_ms ();

    uint32_t local_registry_id = 0;
    {
        scoped_lock_t lock (_sync);
        local_registry_id = _registry_id;
        if (local_registry_id == 0)
            local_registry_id = 1;

        if (peer_registry_id == local_registry_id) {
            for (size_t i = 0; i < frames.size (); ++i)
                zlink_msg_close (&frames[i]);
            return;
        }

        _peer_last_seen[peer_registry_id] = now;
        std::map<uint32_t, uint64_t>::iterator it =
          _peer_seq.find (peer_registry_id);
        if (it != _peer_seq.end () && list_seq <= it->second) {
            for (size_t i = 0; i < frames.size (); ++i)
                zlink_msg_close (&frames[i]);
            return;
        }
    }

    size_t index = 4;
    for (uint32_t i = 0; i < service_count && index < frames.size (); ++i) {
        if (index + 2 >= frames.size ())
            break;
        uint16_t service_type = 0;
        if (!discovery_protocol::read_u16 (frames[index++], &service_type))
            break;
        const std::string service_name =
          discovery_protocol::read_string (frames[index++]);
        uint32_t provider_count = 0;
        if (!discovery_protocol::read_u32 (frames[index++], &provider_count))
            break;

        service_key_t service_key;
        service_key.service_type = service_type;
        service_key.service_name = service_name;
        service_entry_t &service = incoming[service_key];
        for (uint32_t p = 0; p < provider_count && index + 3 < frames.size ();
             ++p) {
            provider_entry_t entry;
            if (!discovery_protocol::read_u16 (frames[index++],
                                               &entry.service_role))
                break;
            if (!discovery_protocol::is_valid_service_role_for_type (
                  service_type, entry.service_role))
                break;
            entry.endpoint = discovery_protocol::read_string (frames[index++]);
            discovery_protocol::read_routing_id (frames[index++],
                                                 &entry.routing_id);
            uint32_t weight = 0;
            discovery_protocol::read_u32 (frames[index++], &weight);
            entry.weight = weight;
            entry.registered_at = now;
            entry.last_heartbeat = now;
            entry.source_registry = peer_registry_id;
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
          _peer_seq.find (peer_registry_id);
        if (peer_registry_id == local_registry_id
            || (it != _peer_seq.end () && list_seq <= it->second)) {
            for (size_t i = 0; i < frames.size (); ++i)
                zlink_msg_close (&frames[i]);
            return;
        }

        for (service_map_t::const_iterator sit = incoming.begin ();
             sit != incoming.end (); ++sit) {
            const service_key_t &service_key = sit->first;
            const provider_map_t &providers = sit->second.providers;
            service_map_t::const_iterator existing_service =
              _services.find (service_key);
            for (provider_map_t::const_iterator pit = providers.begin ();
                 pit != providers.end (); ++pit) {
                bool match = false;
                if (existing_service != _services.end ()) {
                    provider_map_t::const_iterator ep =
                      existing_service->second.providers.find (pit->first);
                    if (ep != existing_service->second.providers.end ()
                        && ep->second.source_registry == peer_registry_id) {
                        const provider_entry_t &cur = ep->second;
                        const provider_entry_t &incoming_entry = pit->second;
                        match =
                          cur.service_role == incoming_entry.service_role
                          &&
                          cur.weight == incoming_entry.weight
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
            for (service_map_t::const_iterator sit = _services.begin ();
                 sit != _services.end (); ++sit) {
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
            _peer_seq[peer_registry_id] = list_seq;
            for (size_t i = 0; i < frames.size (); ++i)
                zlink_msg_close (&frames[i]);
            return;
        }

        for (service_map_t::iterator sit = _services.begin ();
             sit != _services.end ();) {
            provider_map_t &providers = sit->second.providers;
            for (provider_map_t::iterator pit = providers.begin ();
                 pit != providers.end ();) {
                if (pit->second.source_registry == peer_registry_id) {
                    pit = providers.erase (pit);
                    continue;
                }
                ++pit;
            }
            if (providers.empty ()) {
                sit = _services.erase (sit);
                continue;
            }
            ++sit;
        }

        for (service_map_t::const_iterator sit = incoming.begin ();
             sit != incoming.end (); ++sit) {
            const service_key_t &service_key = sit->first;
            const provider_map_t &providers = sit->second.providers;
            service_entry_t &service = _services[service_key];
            for (provider_map_t::const_iterator pit = providers.begin ();
                 pit != providers.end (); ++pit) {
                provider_map_t::iterator existing =
                  service.providers.find (pit->first);
                if (existing != service.providers.end ()
                    && existing->second.source_registry != peer_registry_id) {
                    continue;
                }
                service.providers[pit->first] = pit->second;
            }
        }

        _peer_seq[peer_registry_id] = list_seq;
        _list_seq++;
    }

    for (size_t i = 0; i < frames.size (); ++i)
        zlink_msg_close (&frames[i]);
}

void registry_t::handle_register (void *router_,
                                  const zlink_msg_t *frames_,
                                  size_t frame_count_,
                                  const zlink_routing_id_t &sender_id_)
{
    if (frame_count_ < 5) {
        send_register_ack (router_, sender_id_, 0xFF, std::string (),
                           "invalid register");
        return;
    }

    uint16_t service_type = 0;
    if (!discovery_protocol::read_u16 (frames_[1], &service_type)
        || !discovery_protocol::is_valid_service_type (service_type)) {
        send_register_ack (router_, sender_id_, 0xFF, std::string (),
                           "invalid type");
        return;
    }
    uint16_t service_role = 0;
    if (!discovery_protocol::read_u16 (frames_[2], &service_role)
        || !discovery_protocol::is_valid_service_role_for_type (service_type,
                                                                service_role)) {
        send_register_ack (router_, sender_id_, 0xFF, std::string (),
                           "invalid role");
        return;
    }
    const std::string service_name =
      discovery_protocol::read_string (frames_[3]);
    const std::string endpoint =
      discovery_protocol::read_string (frames_[4]);

    if (service_name.empty () || endpoint.empty ()) {
        send_register_ack (router_, sender_id_, 0x02, endpoint,
                           "invalid endpoint");
        return;
    }

    uint32_t weight = 0;
    if (frame_count_ >= 6)
        discovery_protocol::read_u32 (frames_[5], &weight);

    const uint64_t now = zlink::clock_t ().now_ms ();

    service_key_t service_key;
    service_key.service_type = service_type;
    service_key.service_name = service_name;
    service_entry_t &service = _services[service_key];
    provider_key_t provider_key;
    provider_key.service_role = service_role;
    provider_key.endpoint = endpoint;
    provider_entry_t &entry = service.providers[provider_key];
    entry.service_role = service_role;
    entry.endpoint = endpoint;
    entry.routing_id = sender_id_;
    entry.weight = weight;
    entry.registered_at = now;
    entry.last_heartbeat = now;
    entry.source_registry = _registry_id;

    _list_seq++;
    send_register_ack (router_, sender_id_, 0x00, endpoint, std::string ());
}

void registry_t::handle_unregister (void *router_,
                                    const zlink_msg_t *frames_,
                                    size_t frame_count_,
                                    const zlink_routing_id_t &sender_id_)
{
    if (frame_count_ < 5) {
        send_unregister_ack (router_, sender_id_, 0xFF, "invalid unregister");
        return;
    }

    uint16_t service_type = 0;
    if (!discovery_protocol::read_u16 (frames_[1], &service_type)) {
        send_unregister_ack (router_, sender_id_, 0xFF, "invalid type");
        return;
    }
    uint16_t service_role = 0;
    if (!discovery_protocol::read_u16 (frames_[2], &service_role)
        || !discovery_protocol::is_valid_service_role_for_type (service_type,
                                                                service_role)) {
        send_unregister_ack (router_, sender_id_, 0xFF, "invalid role");
        return;
    }
    const std::string service_name =
      discovery_protocol::read_string (frames_[3]);
    const std::string endpoint =
      discovery_protocol::read_string (frames_[4]);

    service_key_t service_key;
    service_key.service_type = service_type;
    service_key.service_name = service_name;

    service_map_t::iterator sit = _services.find (service_key);
    if (sit == _services.end ()) {
        send_unregister_ack (router_, sender_id_, 0x01, "service not found");
        return;
    }

    provider_key_t provider_key;
    provider_key.service_role = service_role;
    provider_key.endpoint = endpoint;
    provider_map_t::iterator pit = sit->second.providers.find (provider_key);
    if (pit == sit->second.providers.end ()) {
        send_unregister_ack (router_, sender_id_, 0x01, "endpoint not found");
        return;
    }
    if (pit->second.source_registry != _registry_id) {
        send_unregister_ack (router_, sender_id_, 0x01, "foreign provider");
        return;
    }

    sit->second.providers.erase (pit);
    if (sit->second.providers.empty ())
        _services.erase (sit);

    _list_seq++;
    send_unregister_ack (router_, sender_id_, 0x00, std::string ());
}

void registry_t::handle_heartbeat (const zlink_msg_t *frames_,
                                   size_t frame_count_)
{
    if (frame_count_ < 5)
        return;

    uint16_t service_type = 0;
    if (!discovery_protocol::read_u16 (frames_[1], &service_type))
        return;
    uint16_t service_role = 0;
    if (!discovery_protocol::read_u16 (frames_[2], &service_role)
        || !discovery_protocol::is_valid_service_role_for_type (service_type,
                                                                service_role)) {
        return;
    }
    const std::string service_name =
      discovery_protocol::read_string (frames_[3]);
    const std::string endpoint =
      discovery_protocol::read_string (frames_[4]);

    service_key_t service_key;
    service_key.service_type = service_type;
    service_key.service_name = service_name;

    service_map_t::iterator sit = _services.find (service_key);
    if (sit == _services.end ())
        return;

    provider_key_t provider_key;
    provider_key.service_role = service_role;
    provider_key.endpoint = endpoint;
    provider_map_t::iterator pit = sit->second.providers.find (provider_key);
    if (pit == sit->second.providers.end ())
        return;

    pit->second.last_heartbeat = zlink::clock_t ().now_ms ();
}

void registry_t::handle_update_weight (void *router_,
                                       const zlink_msg_t *frames_,
                                       size_t frame_count_,
                                       const zlink_routing_id_t &sender_id_)
{
    if (frame_count_ < 6) {
        send_register_ack (router_, sender_id_, 0xFF, std::string (),
                           "invalid update");
        return;
    }

    uint16_t service_type = 0;
    if (!discovery_protocol::read_u16 (frames_[1], &service_type)
        || !discovery_protocol::is_valid_service_type (service_type)) {
        send_register_ack (router_, sender_id_, 0xFF, std::string (),
                           "invalid type");
        return;
    }
    uint16_t service_role = 0;
    if (!discovery_protocol::read_u16 (frames_[2], &service_role)
        || !discovery_protocol::is_valid_service_role_for_type (service_type,
                                                                service_role)) {
        send_register_ack (router_, sender_id_, 0xFF, std::string (),
                           "invalid role");
        return;
    }
    const std::string service_name =
      discovery_protocol::read_string (frames_[3]);
    const std::string endpoint =
      discovery_protocol::read_string (frames_[4]);
    uint32_t weight = 0;
    discovery_protocol::read_u32 (frames_[5], &weight);

    service_key_t service_key;
    service_key.service_type = service_type;
    service_key.service_name = service_name;
    service_map_t::iterator sit = _services.find (service_key);
    if (sit == _services.end ()) {
        send_register_ack (router_, sender_id_, 0x01, endpoint,
                           "service not found");
        return;
    }

    provider_key_t provider_key;
    provider_key.service_role = service_role;
    provider_key.endpoint = endpoint;
    provider_map_t::iterator pit = sit->second.providers.find (provider_key);
    if (pit == sit->second.providers.end ()) {
        send_register_ack (router_, sender_id_, 0x01, endpoint,
                           "provider not found");
        return;
    }
    if (pit->second.source_registry != _registry_id) {
        send_register_ack (router_, sender_id_, 0x01, endpoint,
                           "provider not local");
        return;
    }

    pit->second.weight = weight;
    _list_seq++;
    send_register_ack (router_, sender_id_, 0x00, endpoint, std::string ());
}
}
