/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "core/recv_internal.hpp"
#include "services/discovery/discovery_access.hpp"
#include "services/discovery/discovery.hpp"
#include "services/discovery/discovery_debug.hpp"
#include "services/discovery/discovery_protocol.hpp"
#include "services/discovery/routing_id_utils.hpp"
#include "services/discovery/discovery_runtime_internal.hpp"

namespace zlink
{
namespace
{
static bool send_topology_report_frames_local (socket_base_t *dealer_,
                                               const zlink_registry_topology_entry_t &entry_)
{
    if (!dealer_)
        return false;

    return zlink::discovery_protocol::send_u16 (dealer_, discovery_protocol::msg_topology_report,
                                                ZLINK_SNDMORE)
             >= 0
           && zlink::discovery_protocol::send_frame (dealer_, &entry_, sizeof (entry_), 0) >= 0;
}

static bool wait_socket_event_local (void *socket_, short events_, long timeout_ms_)
{
    return zlink::wait_socket_events_internal (socket_, events_, timeout_ms_) > 0;
}

}

int discovery_uplink_runtime_t::ensure_topology_reporters (discovery_t *owner_)
{
    scoped_lock_t uplink_lock (discovery_access_t::uplink_sync (owner_));
    std::vector<std::string> endpoints;
    collect_uplink_endpoints (owner_, &endpoints);
    size_t ready_count = 0;
    for (size_t i = 0; i < endpoints.size (); ++i) {
        socket_base_t *dealer = NULL;
        if (ensure_topology_reporter (owner_, endpoints[i], &dealer) != 0) {
            discovery_debugf ("uplink connect failed endpoint=%s errno=%d", endpoints[i].c_str (),
                              errno);
            continue;
        }

        if (dealer && wait_socket_event_local (static_cast<void *> (dealer), ZLINK_POLLOUT, 10)) {
            ++ready_count;
        }
    }
    return ready_count == 0 ? -1 : 0;
}

void discovery_uplink_runtime_t::flush_topology_reports (discovery_t *owner_)
{
    if (ensure_topology_reporters (owner_) != 0)
        return;

    std::vector<zlink_registry_topology_entry_t> entries;
    {
        scoped_lock_t lock (discovery_access_t::sync (owner_));
        discovery_access_t::collect_dirty_summary_entries (owner_, &entries);
    }

    if (entries.empty ())
        return;

    std::vector<bool> sent (entries.size (), false);
    std::vector<socket_base_t *> dealers;
    {
        scoped_lock_t lock (discovery_access_t::sync (owner_));
        for (std::map<std::string, socket_base_t *>::const_iterator it =
               _socket_owner_state.report_dealers.begin ();
             it != _socket_owner_state.report_dealers.end (); ++it) {
            if (it->second)
                dealers.push_back (it->second);
        }
    }
    if (dealers.empty ())
        return;

    scoped_lock_t uplink_lock (discovery_access_t::uplink_sync (owner_));
    for (size_t i = 0; i < entries.size (); ++i) {
        bool all_sent = true;
        for (size_t d = 0; d < dealers.size (); ++d) {
            if (!wait_socket_event_local (static_cast<void *> (dealers[d]), ZLINK_POLLOUT, 0)) {
                all_sent = false;
                continue;
            }
            const bool dealer_sent = send_topology_report_frames_local (dealers[d], entries[i]);
            if (!dealer_sent)
                all_sent = false;
        }
        sent[i] = all_sent;
        if (!all_sent) {
            discovery_debugf ("topology report send failed kind=%u service=%s errno=%d",
                              static_cast<unsigned int> (entries[i].service_kind),
                              entries[i].channel_name, errno);
        }
    }

    {
        scoped_lock_t lock (discovery_access_t::sync (owner_));
        discovery_access_t::mark_summary_entries_sent (owner_, entries, sent);
    }
}

void discovery_uplink_runtime_t::refresh_registered_service_heartbeats (discovery_t *owner_,
                                                                        uint64_t now_ms_)
{
    std::vector<discovery_registered_service_snapshot_t> services;
    {
        scoped_lock_t lock (discovery_access_t::sync (owner_));
        discovery_access_t::collect_registered_services_for_heartbeat (
          owner_, now_ms_, discovery_access_t::bootstrap_runtime (owner_)->heartbeat_interval_ms (),
          &services);
    }

    if (services.empty ())
        return;

    scoped_lock_t uplink_lock (discovery_access_t::uplink_sync (owner_));
    for (size_t i = 0; i < services.size (); ++i) {
        socket_base_t *dealer = NULL;
        if (ensure_control_dealer (owner_, services[i].uplink_endpoint, &dealer) != 0) {
            continue;
        }
        if (!wait_socket_event_local (static_cast<void *> (dealer), ZLINK_POLLOUT, 0)) {
            continue;
        }
        if (discovery_protocol::send_u16 (dealer, discovery_protocol::msg_heartbeat, ZLINK_SNDMORE)
              < 0
            || discovery_protocol::send_u16 (dealer, owner_->auto_connect_type (), ZLINK_SNDMORE)
                 < 0
            || discovery_protocol::send_u16 (dealer, services[i].service_role, ZLINK_SNDMORE) < 0
            || discovery_protocol::send_string (dealer, services[i].channel_name, ZLINK_SNDMORE) < 0
            || discovery_protocol::send_string (dealer, services[i].endpoint, 0) < 0) {
            continue;
        }

        scoped_lock_t lock (discovery_access_t::sync (owner_));
        discovery_access_t::mark_registered_service_heartbeat (owner_, services[i], now_ms_);
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
