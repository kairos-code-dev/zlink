/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "core/recv_internal.hpp"
#include "services/discovery/discovery.hpp"
#include "services/discovery/discovery_protocol.hpp"

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
    {
        scoped_lock_t lock (_sync);
        for (std::set<std::string>::const_iterator it =
               _registry_bootstrap_endpoints.begin ();
             it != _registry_bootstrap_endpoints.end (); ++it) {
            if (_bootstrapped_registry_endpoints.count (*it) == 0)
                bootstrap_endpoints.push_back (*it);
        }
    }

    for (size_t i = 0; i < bootstrap_endpoints.size (); ++i) {
        if (bootstrap_registry (bootstrap_endpoints[i].c_str ()) == 0) {
            scoped_lock_t lock (_sync);
            _bootstrapped_registry_endpoints.insert (bootstrap_endpoints[i]);
        }
    }

    if (ensure_sub_socket () != 0)
        return;

    void *sub = NULL;
    std::set<std::string> endpoints;
    {
        scoped_lock_t lock (_sync);
        sub = _sub_socket;
        endpoints = _registry_pub_endpoints;
    }
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
    flush_gateway_peer_reports ();
}

void discovery_t::notify_observers (const std::set<std::string> &services_)
{
    if (services_.empty ())
        return;
    std::vector<discovery_observer_t *> observers;
    {
        scoped_lock_t lock (_sync);
        observers.assign (_observers.begin (), _observers.end ());
    }
    if (observers.empty ())
        return;
    for (std::set<std::string>::const_iterator sit = services_.begin ();
         sit != services_.end (); ++sit) {
        for (size_t i = 0; i < observers.size (); ++i) {
            if (!observers[i])
                continue;
            {
                scoped_lock_t lock (_sync);
                if (_observers.find (observers[i]) == _observers.end ())
                    continue;
                ++_observer_callbacks_inflight;
            }
            observers[i]->on_service_update (*sit);
            {
                scoped_lock_t lock (_sync);
                if (_observer_callbacks_inflight > 0)
                    --_observer_callbacks_inflight;
                _observer_cv.broadcast ();
            }
        }
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

    std::map<std::string, service_state_t> updated;

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

        service_state_t state;
        for (uint32_t p = 0; p < receiver_count && index + 2 < frames_.size ();
             ++p) {
            provider_info_t info;
            info.service_name = service_name;
            info.endpoint = discovery_protocol::read_string (frames_[index++]);
            discovery_protocol::read_routing_id (frames_[index++],
                                                 &info.routing_id);
            discovery_protocol::read_u32 (frames_[index++], &info.weight);
            info.registered_at = 0;
            if (service_type == _service_type)
                state.providers.push_back (info);
        }

        if (service_type != _service_type)
            continue;

        std::map<std::string, service_state_t>::iterator it =
          updated.find (service_name);
        if (it == updated.end ())
            updated[service_name] = state;
        else
            it->second.providers.insert (it->second.providers.end (),
                                         state.providers.begin (),
                                         state.providers.end ());
    }

    std::set<std::string> changed;
    std::vector<zlink_service_event_t> events;
    {
        scoped_lock_t lock (_sync);
        std::map<uint32_t, uint64_t>::iterator sit =
          _registry_seq.find (registry_id);
        if (sit != _registry_seq.end () && list_seq <= sit->second)
            return;
        _registry_seq[registry_id] = list_seq;

        const auto provider_equal =
          [] (const provider_info_t &a_, const provider_info_t &b_) {
              if (a_.endpoint != b_.endpoint)
                  return false;
              if (a_.routing_id.size != b_.routing_id.size)
                  return false;
              if (a_.routing_id.size > 0
                  && memcmp (a_.routing_id.data, b_.routing_id.data,
                             a_.routing_id.size)
                       != 0)
                  return false;
              return a_.weight == b_.weight;
          };
        const auto providers_equal =
          [&] (const service_state_t &a_, const service_state_t &b_) {
              if (a_.providers.size () != b_.providers.size ())
                  return false;
              for (size_t i = 0; i < a_.providers.size (); ++i) {
                  if (!provider_equal (a_.providers[i], b_.providers[i]))
                      return false;
              }
              return true;
          };

        for (std::map<std::string, service_state_t>::iterator uit =
               updated.begin ();
             uit != updated.end (); ++uit) {
            std::map<std::string, service_state_t>::iterator oit =
              _services.find (uit->first);
            if (oit == _services.end ()
                || !providers_equal (oit->second, uit->second)) {
                _service_seq[uit->first] = _update_seq + 1;
                changed.insert (uit->first);

                zlink_service_event_t ev;
                memset (&ev, 0, sizeof (ev));
                ev.service_kind = ZLINK_SERVICE_KIND_DISCOVERY;
                ev.detail_flags = ZLINK_EVENT_DETAIL_SERVICE_NAME
                                  | ZLINK_EVENT_DETAIL_SUBJECT_RID;
                ev.routing_id = _routing_id;
                strncpy (ev.service_name, uit->first.c_str (),
                         sizeof (ev.service_name) - 1);
                ev.value = static_cast<uint32_t> (uit->second.providers.size ());
                if (oit == _services.end () || oit->second.providers.empty ())
                    ev.event_type = ZLINK_DISCOVERY_SERVICE_UP;
                else if (uit->second.providers.empty ())
                    ev.event_type = ZLINK_DISCOVERY_SERVICE_DOWN;
                else
                    ev.event_type = ZLINK_DISCOVERY_PROVIDERS_CHANGED;
                events.push_back (ev);
            }
        }
        for (std::map<std::string, service_state_t>::iterator oit =
               _services.begin ();
             oit != _services.end (); ++oit) {
            if (updated.find (oit->first) == updated.end ()) {
                _service_seq[oit->first] = _update_seq + 1;
                changed.insert (oit->first);

                zlink_service_event_t ev;
                memset (&ev, 0, sizeof (ev));
                ev.service_kind = ZLINK_SERVICE_KIND_DISCOVERY;
                ev.event_type = ZLINK_DISCOVERY_SERVICE_DOWN;
                ev.detail_flags = ZLINK_EVENT_DETAIL_SERVICE_NAME
                                  | ZLINK_EVENT_DETAIL_SUBJECT_RID;
                ev.routing_id = _routing_id;
                strncpy (ev.service_name, oit->first.c_str (),
                         sizeof (ev.service_name) - 1);
                events.push_back (ev);
            }
        }
        _services.swap (updated);
        _update_seq++;
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
            entry.routing_id = _routing_id;
            entry.service_kind = ZLINK_SERVICE_KIND_DISCOVERY;
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
