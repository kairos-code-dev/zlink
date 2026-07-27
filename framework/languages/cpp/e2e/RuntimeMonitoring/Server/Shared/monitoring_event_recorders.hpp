/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include "evidence_store.hpp"

#include <zlink/framework.hpp>

#include <algorithm>
#include <string>
#include <vector>

namespace zlink::framework::e2e::runtime_monitoring::server
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
    }
    return "Unknown";
}

inline std::string spot_kind_name (zlink::framework::spot_event_kind_t kind)
{
    switch (kind) {
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

inline void record_socket_event (evidence_store_t &evidence,
                                 const zlink::framework::socket_event_payload_t &event)
{
    evidence.add ("monitor-socket|source=" + event.source_name
                  + "|kind=" + socket_kind_name (event.event) + "|remote=" + event.remote_address);
}

inline void record_spot_event (evidence_store_t &evidence,
                               const zlink::framework::spot_event_payload_t &event)
{
    evidence.add (
      "monitor-spot|source=" + event.source_name + "|kind=" + spot_kind_name (event.event)
      + "|timer="
      + (event.timer_diagnostic ? event.timer_diagnostic->timer_name : std::string ("<null>")));
}

inline void record_location_event (evidence_store_t &evidence,
                                   const zlink::framework::location_event_payload_t &event)
{
    std::vector<std::string> nodes;
    std::vector<std::string> routes;
    for (const auto &entry : event.topology) {
        const auto rid = entry.node_rid.to_string ();
        nodes.push_back (rid);
        if (!entry.endpoint.empty ()
            && entry.state == zlink::framework::location_topology_state_t::ready) {
            routes.push_back (rid + "@" + entry.endpoint);
        }
    }
    auto join_unique = [] (std::vector<std::string> values) {
        std::sort (values.begin (), values.end ());
        values.erase (std::unique (values.begin (), values.end ()), values.end ());
        std::string result;
        for (const auto &value : values) {
            if (!result.empty ()) {
                result += ",";
            }
            result += value;
        }
        return result;
    };
    evidence.add ("monitor-location|source=" + event.source_name + "|kind="
                  + location_kind_name (event.event) + "|nodes=" + join_unique (std::move (nodes))
                  + "|routes=" + join_unique (std::move (routes))
                  + "|topology=" + std::to_string (event.topology.size ())
                  + "|summary=" + std::to_string (event.service_summary.size ()) + "|healthy="
                  + (event.status && event.status->store_healthy ? std::string ("true")
                                                                 : std::string ("false")));
}

} // namespace zlink::framework::e2e::runtime_monitoring::server
