/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#pragma once

#include "mon_a1_socket_events_scenario.hpp"

#include <iostream>
#include <set>

namespace zlink::framework::e2e::runtime_monitoring::client
{

inline void run_mon_a2_location_events_scenario (const client_options_t &options)
{
    const auto snapshot = runtime_snapshot (options.service_url);
    ensure (!snapshot.at ("peers").empty (),
            "MON-A2 admitted peer snapshot missing");
    std::set<std::string> peer_ids;
    for (const auto &peer : snapshot.at ("peers")) {
        ensure (peer.at ("generation").get<std::uint64_t> () > 0,
                "MON-A2 peer generation missing");
        ensure (peer.at ("revision").get<std::uint64_t> () > 0,
                "MON-A2 peer descriptor revision missing");
        ensure (!peer.at ("endpoint").get<std::string> ().empty (),
                "MON-A2 peer endpoint missing");
        ensure (peer.at ("ready").get<bool> (),
                "MON-A2 peer is not ready");
        ensure (peer.at ("admissionState") == "ready",
                "MON-A2 admission state mismatch");
        peer_ids.insert (peer.at ("rid").get<std::string> ());
    }
    ensure (peer_ids.size () == snapshot.at ("peers").size (),
            "MON-A2 duplicate peer identity remained in snapshot");
    std::cout << "scenario MON-A2 passed\n";
}

} // namespace zlink::framework::e2e::runtime_monitoring::client
