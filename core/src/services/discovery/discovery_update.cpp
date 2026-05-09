/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "core/c_api_copy_internal.hpp"
#include "core/recv_internal.hpp"
#include "services/discovery/discovery.hpp"
#include "services/discovery/discovery_debug.hpp"
#include "services/discovery/discovery_protocol.hpp"
#include "services/discovery/discovery_runtime_internal.hpp"

#include <cstring>
#include <unordered_map>

namespace zlink
{
namespace
{
static bool wait_socket_event_local (void *socket_,
                                     short events_,
                                     long timeout_ms_)
{
    return zlink::wait_socket_events_internal (socket_, events_, timeout_ms_) > 0;
}

typedef std::pair<uint16_t, std::string> peer_admission_key_t;

struct peer_admission_key_hash_t
{
    size_t operator() (const peer_admission_key_t &key_) const
    {
        size_t seed = std::hash<uint16_t> () (key_.first);
        seed ^= std::hash<std::string> () (key_.second) + 0x9e3779b9
                + (seed << 6) + (seed >> 2);
        return seed;
    }
};

static void append_peer_admission_events_local (
  const std::vector<provider_info_t> &before_,
  const std::vector<provider_info_t> &after_,
  const std::string &channel_name_,
  std::vector<zlink_service_observation_event_t> *events_out_)
{
    if (!events_out_)
        return;

    std::unordered_map<peer_admission_key_t,
                       provider_info_t,
                       peer_admission_key_hash_t>
      before_map;
    before_map.reserve (before_.size ());
    for (size_t i = 0; i < before_.size (); ++i)
        before_map[peer_admission_key_t (before_[i].service_role,
                                         before_[i].endpoint)] = before_[i];

    for (size_t i = 0; i < after_.size (); ++i) {
        const provider_info_t &provider = after_[i];
        const peer_admission_key_t key (provider.service_role, provider.endpoint);
        std::unordered_map<peer_admission_key_t,
                           provider_info_t,
                           peer_admission_key_hash_t>::const_iterator it =
          before_map.find (key);
        if (it == before_map.end ()
            || it->second.weight == provider.weight) {
            continue;
        }

        zlink_service_observation_event_t event;
        memset (&event, 0, sizeof (event));
        event.service_kind = ZLINK_SERVICE_KIND_DISCOVERY;
        event.event_type = ZLINK_SERVICE_EVENT_INTERNAL_PEER_WEIGHT_CHANGED;
        event.value = static_cast<uint32_t> (provider.weight);
        event.detail_flags = static_cast<zlink_service_event_detail_mask_t> (
          ZLINK_SERVICE_EVENT_DETAIL_CHANNEL_NAME
          | ZLINK_SERVICE_EVENT_DETAIL_ENDPOINT
          | ZLINK_SERVICE_EVENT_DETAIL_PEER_RID);
        copy_fixed_c_string_from_cstr (event.channel_name,
                                       sizeof (event.channel_name),
                                       channel_name_.c_str ());
        copy_fixed_c_string_from_cstr (event.endpoint,
                                       sizeof (event.endpoint),
                                       provider.endpoint.c_str ());
        event.routing_id = provider.routing_id;
        events_out_->push_back (event);
    }
}
}

void discovery_t::tick ()
{
    if (_stop.get () != 0)
        return;

    std::vector<std::string> bootstrap_endpoints;
    _bootstrap_runtime->collect_pending_bootstrap_endpoints (
      this, &bootstrap_endpoints);

    for (size_t i = 0; i < bootstrap_endpoints.size (); ++i) {
        (void) bootstrap_registry (bootstrap_endpoints[i].c_str ());
    }

    if (ensure_sub_socket () != 0)
        return;

    void *sub = NULL;
    std::set<std::string> endpoints;
    {
        scoped_lock_t lock (_sync);
        sub = _sub_socket;
    }
    _bootstrap_runtime->collect_registry_pub_endpoints (this, &endpoints);
    if (!sub)
        return;

    for (std::set<std::string>::const_iterator it = endpoints.begin ();
         it != endpoints.end (); ++it) {
        scoped_lock_t lock (_sync);
        if (_connected_endpoints.find (*it) == _connected_endpoints.end ()
            && _sub_socket == sub) {
            zlink_connect (sub, it->c_str ());
            _connected_endpoints.insert (*it);
        }
    }

    while (true) {
        if (!wait_socket_event_local (sub, ZLINK_POLLIN, 0))
            break;

        std::vector<zlink_msg_t> frames;
        frames.reserve (8);
        while (true) {
            zlink_msg_t frame;
            zlink_msg_init (&frame);
            if (recv_msg_internal (sub, &frame, ZLINK_DONTWAIT) == -1) {
                zlink_msg_close (&frame);
                break;
            }
            frames.push_back (frame);
            if (!msg_frame_has_more (frame))
                break;
        }
        if (!frames.empty ())
            handle_service_list (frames);
        close_msg_frames (&frames);
    }

    refresh_registered_service_heartbeats (clock_t ().now_ms ());
    flush_topology_reports ();
}

void discovery_t::notify_observers (const std::set<std::string> &services_)
{
    std::vector<discovery_observer_t *> observers;
    {
        scoped_lock_t lock (_sync);
        _service_state.begin_observer_notification (_channel_name, services_,
                                                    &observers);
    }
    if (observers.empty ())
        return;
    for (size_t i = 0; i < observers.size (); ++i) {
        if (!observers[i])
            continue;
        observers[i]->on_service_update (_channel_name);
    }
    {
        scoped_lock_t lock (_sync);
        _service_state.finish_observer_notification (observers.size ());
    }
}

void discovery_t::handle_service_list (const std::vector<zlink_msg_t> &frames_)
{
    if (frames_.size () < 4)
        return;

    uint16_t msg_id = 0;
    if (!discovery_protocol::read_u16 (frames_[0], &msg_id))
        return;
    if (msg_id != discovery_protocol::msg_service_list)
        return;

    uint32_t registry_id = 0;
    uint64_t list_seq = 0;
    uint32_t service_count = 0;

    if (!discovery_protocol::read_u32 (frames_[1], &registry_id)
        || !discovery_protocol::read_u64 (frames_[2], &list_seq)
        || !discovery_protocol::read_u32 (frames_[3], &service_count)) {
        return;
    }

    std::vector<provider_info_t> updated;
    updated.reserve (service_count);

    size_t index = 4;
    for (uint32_t i = 0; i < service_count && index < frames_.size (); ++i) {
        if (index + 3 >= frames_.size ())
            break;
        uint16_t auto_connect_type = 0;
        if (!discovery_protocol::read_u16 (frames_[index++],
                                           &auto_connect_type))
            break;
        const std::string channel_name =
          discovery_protocol::read_string (frames_[index++]);
        uint64_t contract_created_at = 0;
        if (!discovery_protocol::read_u64 (frames_[index++],
                                           &contract_created_at))
            break;
        uint32_t receiver_count = 0;
        if (!discovery_protocol::read_u32 (frames_[index++], &receiver_count))
            break;

        std::vector<provider_info_t> service_providers;
        service_providers.reserve (receiver_count);
        for (uint32_t p = 0; p < receiver_count && index + 8 < frames_.size ();
             ++p) {
            provider_info_t info;
            info.auto_connect_type = auto_connect_type;
            info.channel_name = channel_name;
            if (!discovery_protocol::read_u16 (frames_[index++],
                                               &info.service_role))
                break;
            info.endpoint = discovery_protocol::read_string (frames_[index++]);
            discovery_protocol::read_routing_id (frames_[index++],
                                                 &info.routing_id);
            discovery_protocol::read_u32 (frames_[index++],
                                          &info.source_registry);
            discovery_protocol::read_u64 (frames_[index++],
                                          &info.registration_id);
            uint64_t provider_update_seq = 0;
            discovery_protocol::read_u64 (frames_[index++],
                                          &provider_update_seq);
            uint16_t raw_weight = 0;
            if (!discovery_protocol::read_u16 (frames_[index++],
                                               &raw_weight))
                break;
            info.weight =
              raw_weight == 0
                ? 0
                : 100;
            discovery_protocol::read_i64 (frames_[index++], &info.value);
            const size_t metadata_size = zlink_msg_size (&frames_[index]);
            info.metadata.resize (metadata_size);
            if (metadata_size > 0) {
                memcpy (&info.metadata[0],
                        zlink_msg_data (const_cast<zlink_msg_t *> (
                          &frames_[index])),
                        metadata_size);
            }
            ++index;
            info.registered_at = 0;
            if (auto_connect_type == _auto_connect_type)
                service_providers.push_back (info);
        }

        if (auto_connect_type != _auto_connect_type
            || channel_name != _channel_name)
            continue;
        updated.insert (updated.end (), service_providers.begin (),
                        service_providers.end ());
    }

    std::set<std::string> changed;
    std::vector<zlink_service_observation_event_t> events;
    events.reserve (updated.size () + 1);
    {
        scoped_lock_t lock (_sync);
        std::vector<provider_info_t> previous;
        _service_state.snapshot_providers (&previous);
        discovery_service_change_t service_change;
        _service_state.apply_provider_snapshot (
          registry_id, list_seq, updated, _channel_name,
          _bootstrap_runtime->routing_id_value (), &service_change);
        append_peer_admission_events_local (previous, updated, _channel_name,
                                            &events);
        if (service_change.changed) {
            changed.insert (_channel_name);
            events.push_back (service_change.event);
        }
    }

    if (!changed.empty ())
        notify_observers (changed);
    for (size_t i = 0; i < events.size (); ++i) {
        discovery_debugf ("service event type=%u name=%s value=%u",
                                static_cast<unsigned int> (events[i].event_type),
                                events[i].channel_name,
                                static_cast<unsigned int> (events[i].value));
        uint16_t state = ZLINK_TOPOLOGY_STATE_DISCOVERED;
        if (events[i].event_type == ZLINK_DISCOVERY_SERVICE_UP
            || events[i].event_type == ZLINK_DISCOVERY_PROVIDERS_CHANGED)
            state = events[i].value > 0 ? ZLINK_TOPOLOGY_STATE_READY
                                        : ZLINK_TOPOLOGY_STATE_DISCOVERED;
        else if (events[i].event_type == ZLINK_DISCOVERY_SERVICE_DOWN)
            state = ZLINK_TOPOLOGY_STATE_LOST;
        if (_discovery_summary_enabled) {
            zlink_registry_topology_entry_t entry;
            memset (&entry, 0, sizeof (entry));
            entry.routing_id = _bootstrap_runtime->routing_id_value ();
            entry.service_kind = ZLINK_SERVICE_KIND_DISCOVERY;
            entry.service_role = ZLINK_SERVICE_ROLE_INVALID;
            entry.auto_connect_type =
              static_cast<zlink_auto_connect_type_t> (_auto_connect_type);
            copy_fixed_c_string_from_cstr (entry.channel_name,
                                           sizeof (entry.channel_name),
                                           events[i].channel_name);
            entry.source = ZLINK_TOPOLOGY_SOURCE_REGISTRY;
            entry.state = static_cast<zlink_topology_state_t> (state);
            entry.desired_count = 1;
            entry.ready_count = events[i].value;
            entry.last_reported_ms = zlink::clock_t ().now_ms ();
            upsert_service_summary (entry);
        }
    }
}
}
