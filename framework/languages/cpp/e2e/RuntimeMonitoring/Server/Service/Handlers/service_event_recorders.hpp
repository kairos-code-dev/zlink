/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include "../../Shared/evidence_store.hpp"

#include <zlink/framework.hpp>

#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

namespace zlink::framework::e2e::runtime_monitoring::service
{

inline std::string socket_kind_name (zlink::framework::socket_event_kind_t kind)
{
    switch (kind) {
        case zlink::framework::socket_event_kind_t::connected:
            return "Connected";
        case zlink::framework::socket_event_kind_t::connection_ready:
            return "ConnectionReady";
        case zlink::framework::socket_event_kind_t::disconnected:
            return "Disconnected";
        case zlink::framework::socket_event_kind_t::closed:
            return "Closed";
        case zlink::framework::socket_event_kind_t::handshake_failed:
            return "HandshakeFailed";
        case zlink::framework::socket_event_kind_t::peer_admission_changed:
            return "PeerAdmissionChanged";
        case zlink::framework::socket_event_kind_t::internal:
            return "Internal";
    }
    return "Unknown";
}

inline std::string spot_kind_name (zlink::framework::spot_event_kind_t kind)
{
    switch (kind) {
        case zlink::framework::spot_event_kind_t::status_changed:
            return "StatusChanged";
        case zlink::framework::spot_event_kind_t::peers_changed:
            return "PeersChanged";
        case zlink::framework::spot_event_kind_t::subjects_changed:
            return "SubjectsChanged";
        case zlink::framework::spot_event_kind_t::timer_handler_failed:
            return "TimerHandlerFailed";
        case zlink::framework::spot_event_kind_t::timer_stopped_after_unhandled_exception:
            return "TimerStoppedAfterUnhandledException";
    }
    return "Unknown";
}

inline std::string location_kind_name (zlink::framework::location_event_kind_t kind)
{
    switch (kind) {
        case zlink::framework::location_event_kind_t::status_changed:
            return "StatusChanged";
        case zlink::framework::location_event_kind_t::topology_changed:
            return "TopologyChanged";
        case zlink::framework::location_event_kind_t::service_summary_changed:
            return "ServiceSummaryChanged";
    }
    return "Unknown";
}

inline void record_socket_event (server::evidence_store_t &evidence,
                                 const zlink::framework::socket_event_payload_t &event)
{
    evidence.add ("monitor-socket|source=" + event.source_name
                  + "|kind=" + socket_kind_name (event.event)
                  + "|remote=" + event.remote_address);
}

inline void record_spot_event (server::evidence_store_t &evidence,
                               const zlink::framework::spot_event_payload_t &event)
{
    evidence.add ("monitor-spot|source=" + event.source_name
                  + "|node=" + event.spot_node_name
                  + "|kind=" + spot_kind_name (event.event)
                  + "|peers=" + std::to_string (event.peers.size ())
                  + "|subjects=" + std::to_string (event.subjects.size ())
                  + "|timer="
                  + (event.timer_diagnostic ? event.timer_diagnostic->timer_name
                                            : std::string ("<null>")));
}

inline void record_location_event (server::evidence_store_t &evidence,
                                   const zlink::framework::location_event_payload_t &event)
{
    std::vector<std::string> nodes;
    for (const auto &entry : event.topology) {
        if (entry.node_rid) {
            nodes.push_back (entry.node_rid->to_string ());
        }
    }
    std::sort (nodes.begin (), nodes.end ());
    nodes.erase (std::unique (nodes.begin (), nodes.end ()), nodes.end ());
    std::string node_list;
    for (const auto &node : nodes) {
        if (!node_list.empty ()) {
            node_list += ",";
        }
        node_list += node;
    }
    evidence.add ("monitor-location|source=" + event.source_name
                  + "|kind=" + location_kind_name (event.event)
                  + "|nodes=" + node_list
                  + "|topology=" + std::to_string (event.topology.size ())
                  + "|summary=" + std::to_string (event.service_summary.size ())
                  + "|healthy="
                  + (event.status && event.status->store_healthy ? std::string ("true")
                                                                  : std::string ("false")));
}

inline void record_throwing_socket_event (
  server::evidence_store_t &evidence,
  const zlink::framework::socket_event_payload_t &event)
{
    evidence.add ("monitor-throw|source=" + event.source_name
                  + "|kind=" + socket_kind_name (event.event));
    throw std::runtime_error ("monitoring dispatch failure for e2e");
}

} // namespace zlink::framework::e2e::runtime_monitoring::service
