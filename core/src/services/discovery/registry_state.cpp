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
        for (uint32_t p = 0; p < provider_count && index + 4 < frames.size ();
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
                          cur.value == incoming_entry.value
                          && cur.metadata == incoming_entry.metadata
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

    int64_t value = 0;
    if (frame_count_ >= 6)
        discovery_protocol::read_i64 (frames_[5], &value);
    std::vector<unsigned char> metadata;
    if (frame_count_ >= 7) {
        const size_t metadata_size = zlink_msg_size (&frames_[6]);
        metadata.resize (metadata_size);
        if (metadata_size > 0)
            memcpy (&metadata[0],
                    zlink_msg_data (const_cast<zlink_msg_t *> (&frames_[6])),
                    metadata_size);
    }

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
    entry.value = value;
    entry.metadata = metadata;
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

void registry_t::handle_update_attributes (void *router_,
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
    int64_t value = 0;
    discovery_protocol::read_i64 (frames_[5], &value);
    std::vector<unsigned char> metadata;
    if (frame_count_ >= 7) {
        const size_t metadata_size = zlink_msg_size (&frames_[6]);
        metadata.resize (metadata_size);
        if (metadata_size > 0)
            memcpy (&metadata[0],
                    zlink_msg_data (const_cast<zlink_msg_t *> (&frames_[6])),
                    metadata_size);
    }

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

    pit->second.value = value;
    pit->second.metadata = metadata;
    _list_seq++;
    send_register_ack (router_, sender_id_, 0x00, endpoint, std::string ());
}
}
