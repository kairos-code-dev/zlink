/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

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
static bool wait_socket_event_local (void *socket_, short events_, long timeout_ms_)
{
    return zlink::wait_socket_events_internal (socket_, events_, timeout_ms_) > 0;
}

typedef std::pair<uint16_t, std::string> peer_admission_key_t;

struct peer_admission_key_hash_t
{
    size_t operator() (const peer_admission_key_t &key_) const
    {
        size_t seed = std::hash<uint16_t> () (key_.first);
        seed ^= std::hash<std::string> () (key_.second) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        return seed;
    }
};

static void
append_peer_admission_events_local (const std::vector<provider_info_t> &before_,
                                    const std::vector<provider_info_t> &after_,
                                    const std::string &channel_name_,
                                    std::vector<zlink_service_observation_event_t> *events_out_)
{
    if (!events_out_)
        return;

    std::unordered_map<peer_admission_key_t, provider_info_t, peer_admission_key_hash_t> before_map;
    before_map.reserve (before_.size ());
    for (size_t i = 0; i < before_.size (); ++i)
        before_map[peer_admission_key_t (before_[i].service_role, before_[i].endpoint)] =
          before_[i];

    for (size_t i = 0; i < after_.size (); ++i) {
        const provider_info_t &provider = after_[i];
        const peer_admission_key_t key (provider.service_role, provider.endpoint);
        std::unordered_map<peer_admission_key_t, provider_info_t,
                           peer_admission_key_hash_t>::const_iterator it = before_map.find (key);
        if (it == before_map.end () || it->second.weight == provider.weight) {
            continue;
        }

        zlink_service_observation_event_t event;
        memset (&event, 0, sizeof (event));
        event.service_kind = ZLINK_SERVICE_KIND_DISCOVERY;
        event.event_type = ZLINK_SERVICE_EVENT_INTERNAL_PEER_WEIGHT_CHANGED;
        event.value = static_cast<uint32_t> (provider.weight);
        event.detail_flags = static_cast<zlink_service_event_detail_mask_t> (
          ZLINK_SERVICE_EVENT_DETAIL_CHANNEL_NAME | ZLINK_SERVICE_EVENT_DETAIL_ENDPOINT
          | ZLINK_SERVICE_EVENT_DETAIL_PEER_RID);
        copy_fixed_c_string_from_cstr (event.channel_name, sizeof (event.channel_name),
                                       channel_name_.c_str ());
        copy_fixed_c_string_from_cstr (event.endpoint, sizeof (event.endpoint),
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
    _bootstrap_runtime->collect_pending_bootstrap_endpoints (this, &bootstrap_endpoints);

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

    for (std::set<std::string>::const_iterator it = endpoints.begin (); it != endpoints.end ();
         ++it) {
        scoped_lock_t lock (_sync);
        if (_connected_endpoints.find (*it) == _connected_endpoints.end () && _sub_socket == sub) {
            zlink_connect (sub, it->c_str ());
            _connected_endpoints.insert (*it);
        }
    }

    while (true) {
        if (!wait_socket_event_local (sub, ZLINK_POLLIN, 0))
            break;

        scoped_msg_frames_t frames;
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
    }

    refresh_registered_service_heartbeats (clock_t ().now_ms ());
    flush_topology_reports ();
}

void discovery_t::notify_observers (const std::set<std::string> &services_)
{
    std::vector<discovery_observer_t *> observers;
    {
        scoped_lock_t lock (_sync);
        _service_state.begin_observer_notification (_channel_name, services_, &observers);
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
    discovery_protocol::service_list_t service_list;
    if (!discovery_protocol::decode_service_list (frames_, &service_list))
        return;

    std::vector<provider_info_t> updated;
    updated.reserve (service_list.services.size ());

    for (size_t i = 0; i < service_list.services.size (); ++i) {
        const discovery_protocol::service_record_t &service = service_list.services[i];
        std::vector<provider_info_t> service_providers;
        service_providers.reserve (service.providers.size ());
        for (size_t p = 0; p < service.providers.size (); ++p) {
            const discovery_protocol::service_provider_record_t &provider = service.providers[p];
            provider_info_t info;
            info.auto_connect_type = service.auto_connect_type;
            info.channel_name = service.channel_name;
            info.service_role = provider.service_role;
            info.endpoint = provider.endpoint;
            info.routing_id = provider.routing_id;
            info.source_registry = provider.source_registry;
            info.registration_id = provider.registration_id;
            info.weight = provider.weight;
            info.value = provider.value;
            info.metadata = provider.metadata;
            info.registered_at = 0;
            if (service.auto_connect_type == _auto_connect_type)
                service_providers.push_back (info);
        }

        if (service.auto_connect_type != _auto_connect_type
            || service.channel_name != _channel_name)
            continue;
        updated.insert (updated.end (), service_providers.begin (), service_providers.end ());
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
          service_list.registry_id, service_list.list_seq, updated, _channel_name,
          _bootstrap_runtime->routing_id_value (), &service_change);
        std::vector<provider_info_t> current;
        _service_state.snapshot_providers (&current);
        append_peer_admission_events_local (previous, current, _channel_name, &events);
        if (service_change.changed) {
            changed.insert (_channel_name);
            events.push_back (service_change.event);
        }
    }

    if (!changed.empty ())
        notify_observers (changed);
    for (size_t i = 0; i < events.size (); ++i) {
        discovery_debugf ("service event type=%u name=%s value=%u",
                          static_cast<unsigned int> (events[i].event_type), events[i].channel_name,
                          static_cast<unsigned int> (events[i].value));
        uint16_t state = ZLINK_TOPOLOGY_STATE_DISCOVERED;
        if (events[i].event_type == ZLINK_DISCOVERY_SERVICE_UP
            || events[i].event_type == ZLINK_DISCOVERY_PROVIDERS_CHANGED)
            state =
              events[i].value > 0 ? ZLINK_TOPOLOGY_STATE_READY : ZLINK_TOPOLOGY_STATE_DISCOVERED;
        else if (events[i].event_type == ZLINK_DISCOVERY_SERVICE_DOWN)
            state = ZLINK_TOPOLOGY_STATE_LOST;
        if (_discovery_summary_enabled) {
            zlink_registry_topology_entry_t entry;
            memset (&entry, 0, sizeof (entry));
            entry.routing_id = _bootstrap_runtime->routing_id_value ();
            entry.service_kind = ZLINK_SERVICE_KIND_DISCOVERY;
            entry.service_role = ZLINK_SERVICE_ROLE_INVALID;
            entry.auto_connect_type = static_cast<zlink_auto_connect_type_t> (_auto_connect_type);
            copy_fixed_c_string_from_cstr (entry.channel_name, sizeof (entry.channel_name),
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
