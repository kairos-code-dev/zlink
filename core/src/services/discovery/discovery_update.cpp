/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "core/recv_internal.hpp"
#include "services/discovery/discovery.hpp"
#include "services/discovery/discovery_protocol.hpp"
#include "services/discovery/discovery_runtime_internal.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace zlink
{
namespace
{
static void discovery_debugf_local (const char *fmt_, ...)
{
    if (!std::getenv ("ZLINK_DISCOVERY_DEBUG"))
        return;
    va_list args;
    va_start (args, fmt_);
    std::fprintf (stderr, "[discovery] ");
    std::vfprintf (stderr, fmt_, args);
    std::fprintf (stderr, "\n");
    va_end (args);
}

static bool discovery_frame_has_more_local (const zlink_msg_t &frame_)
{
    return (reinterpret_cast<const msg_t *> (&frame_)->flags () & msg_t::more)
           != 0;
}

static void close_frames_local (std::vector<zlink_msg_t> *frames_)
{
    if (!frames_)
        return;
    for (size_t i = 0; i < frames_->size (); ++i)
        zlink_msg_close (&(*frames_)[i]);
    frames_->clear ();
}

static bool wait_socket_event_local (void *socket_,
                                     short events_,
                                     long timeout_ms_)
{
    return zlink::wait_socket_events_internal (socket_, events_, timeout_ms_) > 0;
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
        while (true) {
            zlink_msg_t frame;
            zlink_msg_init (&frame);
            if (recv_msg_internal (sub, &frame, ZLINK_DONTWAIT) == -1) {
                zlink_msg_close (&frame);
                break;
            }
            frames.push_back (frame);
            if (!discovery_frame_has_more_local (frame))
                break;
        }
        if (!frames.empty ())
            handle_service_list (frames);
        close_frames_local (&frames);
    }

    refresh_registered_service_heartbeats (clock_t ().now_ms ());
    flush_topology_reports ();
}

void discovery_t::notify_observers (const std::set<std::string> &services_)
{
    std::vector<discovery_observer_t *> observers;
    {
        scoped_lock_t lock (_sync);
        _service_state.begin_observer_notification (_service_name, services_,
                                                    &observers);
    }
    if (observers.empty ())
        return;
    for (size_t i = 0; i < observers.size (); ++i) {
        if (!observers[i])
            continue;
        observers[i]->on_service_update (_service_name);
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

    size_t index = 4;
    for (uint32_t i = 0; i < service_count && index < frames_.size (); ++i) {
        if (index + 2 >= frames_.size ())
            break;
        uint16_t service_type = 0;
        if (!discovery_protocol::read_u16 (frames_[index++], &service_type))
            break;
        const std::string service_name =
          discovery_protocol::read_string (frames_[index++]);
        uint32_t receiver_count = 0;
        if (!discovery_protocol::read_u32 (frames_[index++], &receiver_count))
            break;

        std::vector<provider_info_t> service_providers;
        for (uint32_t p = 0; p < receiver_count && index + 4 < frames_.size ();
             ++p) {
            provider_info_t info;
            info.service_name = service_name;
            if (!discovery_protocol::read_u16 (frames_[index++],
                                               &info.service_role))
                break;
            info.endpoint = discovery_protocol::read_string (frames_[index++]);
            discovery_protocol::read_routing_id (frames_[index++],
                                                 &info.routing_id);
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
            if (service_type == _service_type)
                service_providers.push_back (info);
        }

        if (service_type != _service_type || service_name != _service_name)
            continue;
        updated.insert (updated.end (), service_providers.begin (),
                        service_providers.end ());
    }

    std::set<std::string> changed;
    std::vector<zlink_service_event_t> events;
    {
        scoped_lock_t lock (_sync);
        discovery_service_change_t service_change;
        _service_state.apply_provider_snapshot (
          registry_id, list_seq, updated, _service_name,
          _bootstrap_runtime->routing_id_value (), &service_change);
        if (service_change.changed) {
            changed.insert (_service_name);
            events.push_back (service_change.event);
        }
    }

    if (!changed.empty ())
        notify_observers (changed);
    for (size_t i = 0; i < events.size (); ++i) {
        discovery_debugf_local ("service event type=%u name=%s value=%u",
                                static_cast<unsigned int> (events[i].event_type),
                                events[i].service_name,
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
            strncpy (entry.service_name, events[i].service_name,
                     sizeof (entry.service_name) - 1);
            entry.source = ZLINK_TOPOLOGY_SOURCE_REGISTRY;
            entry.state = static_cast<zlink_topology_state_t> (state);
            entry.desired_count = 1;
            entry.ready_count = events[i].value;
            entry.last_reported_ms = zlink::clock_t ().now_ms ();
            upsert_service_summary (entry);
        }
        _monitor.emit (events[i]);
    }
}
}
