/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "core/recv_internal.hpp"
#include "services/discovery/discovery.hpp"
#include "services/discovery/discovery_protocol.hpp"
#include "services/discovery/routing_id_utils.hpp"
#include "services/discovery/discovery_runtime_internal.hpp"

#include <cstdarg>
#include <cstdio>

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

static bool send_topology_report_frames_local (
  socket_base_t *dealer_,
  const zlink_registry_topology_entry_t &entry_)
{
    if (!dealer_)
        return false;

    return zlink::discovery_protocol::send_u16 (
             dealer_, discovery_protocol::msg_topology_report, ZLINK_SNDMORE)
             >= 0
           && zlink::discovery_protocol::send_frame (
                dealer_, &entry_, sizeof (entry_), 0)
                >= 0;
}

static bool wait_socket_event_local (void *socket_,
                                     short events_,
                                     long timeout_ms_)
{
    return zlink::wait_socket_events_internal (socket_, events_, timeout_ms_) > 0;
}

}

int discovery_uplink_runtime_t::ensure_topology_reporters (discovery_t *owner_)
{
    scoped_lock_t uplink_lock (owner_->_uplink_sync);
    std::vector<std::string> endpoints;
    collect_uplink_endpoints (owner_, &endpoints);
    size_t ready_count = 0;
    for (size_t i = 0; i < endpoints.size (); ++i) {
        socket_base_t *dealer = NULL;
        if (ensure_topology_reporter (owner_, endpoints[i], &dealer) != 0) {
            discovery_debugf_local ("uplink connect failed endpoint=%s errno=%d",
                                    endpoints[i].c_str (), errno);
            continue;
        }

        if (dealer
            && wait_socket_event_local (static_cast<void *> (dealer),
                                        ZLINK_POLLOUT, 10)) {
            ++ready_count;
        }
    }
    return ready_count == 0 ? -1 : 0;
}

void discovery_uplink_runtime_t::flush_topology_reports (discovery_t *owner_)
{
    if (ensure_topology_reporters (owner_) != 0)
        return;

    std::vector<discovery_t::topology_key_t> keys;
    std::vector<zlink_registry_topology_entry_t> entries;
    {
        scoped_lock_t lock (owner_->_sync);
        for (std::map<discovery_t::topology_key_t,
                      discovery_t::topology_summary_t>::iterator it =
               owner_->_summary_store.begin ();
             it != owner_->_summary_store.end (); ++it) {
            if (!it->second.dirty)
                continue;
            if (!owner_->should_publish_summary_entry_locked (
                  it->second.entry))
                continue;
            keys.push_back (it->first);
            entries.push_back (it->second.entry);
        }
    }

    if (entries.empty ())
        return;

    std::vector<bool> sent (entries.size (), false);
    std::vector<socket_base_t *> dealers;
    {
        scoped_lock_t lock (owner_->_sync);
        for (std::map<std::string, socket_base_t *>::const_iterator it =
               _report_dealers.begin ();
             it != _report_dealers.end (); ++it) {
            if (it->second)
                dealers.push_back (it->second);
        }
    }
    if (dealers.empty ())
        return;

    scoped_lock_t uplink_lock (owner_->_uplink_sync);
    for (size_t i = 0; i < entries.size (); ++i) {
        bool all_sent = true;
        for (size_t d = 0; d < dealers.size (); ++d) {
            if (!wait_socket_event_local (static_cast<void *> (dealers[d]),
                                          ZLINK_POLLOUT, 0)) {
                all_sent = false;
                continue;
            }
            const bool dealer_sent =
              send_topology_report_frames_local (dealers[d], entries[i]);
            if (!dealer_sent)
                all_sent = false;
        }
        sent[i] = all_sent;
        if (!all_sent) {
            discovery_debugf_local (
              "topology report send failed kind=%u service=%s errno=%d",
              static_cast<unsigned int> (entries[i].service_kind),
              entries[i].channel_name, errno);
        }
    }

    {
        scoped_lock_t lock (owner_->_sync);
        for (size_t i = 0; i < keys.size (); ++i) {
            std::map<discovery_t::topology_key_t,
                     discovery_t::topology_summary_t>::iterator it =
              owner_->_summary_store.find (keys[i]);
            if (it == owner_->_summary_store.end () || !sent[i])
                continue;
            it->second.dirty = false;
        }
    }
}

void discovery_uplink_runtime_t::refresh_registered_service_heartbeats (
  discovery_t *owner_, uint64_t now_ms_)
{
    std::vector<discovery_t::registered_service_t> services;
    {
        scoped_lock_t lock (owner_->_sync);
        for (std::map<discovery_t::registered_service_key_t,
                      discovery_t::registered_service_t>::const_iterator it =
               owner_->_registered_services.begin ();
             it != owner_->_registered_services.end (); ++it) {
            if (it->second.uplink_endpoint.empty ())
                continue;
            if (it->second.last_heartbeat_ms != 0
                && now_ms_ - it->second.last_heartbeat_ms
                     < owner_->_bootstrap_runtime->heartbeat_interval_ms ()) {
                continue;
            }
            services.push_back (it->second);
        }
    }

    if (services.empty ())
        return;

    scoped_lock_t uplink_lock (owner_->_uplink_sync);
    for (size_t i = 0; i < services.size (); ++i) {
        socket_base_t *dealer = NULL;
        if (ensure_control_dealer (owner_, services[i].uplink_endpoint, &dealer)
            != 0) {
            continue;
        }
        if (!wait_socket_event_local (static_cast<void *> (dealer),
                                      ZLINK_POLLOUT, 0)) {
            continue;
        }
        if (discovery_protocol::send_u16 (
              dealer, discovery_protocol::msg_heartbeat, ZLINK_SNDMORE)
              < 0
            || discovery_protocol::send_u16 (dealer, owner_->_auto_connect_type,
                                             ZLINK_SNDMORE)
                 < 0
            || discovery_protocol::send_u16 (dealer, services[i].service_role,
                                             ZLINK_SNDMORE)
                 < 0
            || discovery_protocol::send_string (dealer, services[i].channel_name,
                                                ZLINK_SNDMORE)
                 < 0
            || discovery_protocol::send_string (dealer, services[i].endpoint, 0)
                 < 0) {
            continue;
        }

        scoped_lock_t lock (owner_->_sync);
        discovery_t::registered_service_key_t key;
        key.service_role = services[i].service_role;
        key.channel_name = services[i].channel_name;
        key.endpoint = services[i].endpoint;
        std::map<discovery_t::registered_service_key_t,
                 discovery_t::registered_service_t>::iterator it =
          owner_->_registered_services.find (key);
        if (it != owner_->_registered_services.end ())
            it->second.last_heartbeat_ms = now_ms_;
    }
}

int discovery_t::ensure_topology_reporters ()
{
    return _uplink_runtime->ensure_topology_reporters (this);
}

void discovery_t::flush_topology_reports ()
{
    _uplink_runtime->flush_topology_reports (this);
}

void discovery_t::refresh_registered_service_heartbeats (uint64_t now_ms_)
{
    _uplink_runtime->refresh_registered_service_heartbeats (this, now_ms_);
}

bool discovery_t::latest_registry_uplink (std::string *out_)
{
    return _uplink_runtime->latest_registry_uplink (this, out_);
}
}
