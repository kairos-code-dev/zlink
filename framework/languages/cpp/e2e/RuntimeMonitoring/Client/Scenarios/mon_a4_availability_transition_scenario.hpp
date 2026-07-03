/* SPDX-License-Identifier: MPL-2.0 */

#pragma once

#include "../Support/client_support.hpp"
#include "mon_a1_socket_events_scenario.hpp"

#include <zlink/http_client.hpp>

#include <chrono>
#include <iostream>

namespace zlink::framework::e2e::runtime_monitoring::client
{

inline bool mon_a4_contains_nonzero_location_event (const std::vector<std::string> &entries,
                                                    const std::string &kind,
                                                    const std::string &zero_marker)
{
    for (const auto &entry : entries) {
        if (contains (entry, kind) && !contains (entry, zero_marker)) {
            return true;
        }
    }
    return false;
}

inline void run_mon_a4_availability_transition_scenario (const client_options_t &options)
{
    auto http = zlink::http_client::client_t::create ()
                  .base_url (options.service_url)
                  .timeout (std::chrono::milliseconds (1000))
                  .build ();

    auto drained = http.post ("/admin/server-weight?weight=0").submit_raw ().result ();
    ensure (drained && drained.value ().status < 400, "MON-A4 drain admin call failed");
    auto drain_entries =
      wait_evidence_contains (options.service_url,
                              "admin|rid=svc-a|action=server-weight|weight=0",
                              std::chrono::milliseconds (10000));
    ensure (any_contains (drain_entries, "kind=PeerAdmissionChanged"),
            "MON-A4 drain socket admission evidence missing");

    auto restored = http.post ("/admin/server-weight?weight=100").submit_raw ().result ();
    ensure (restored && restored.value ().status < 400, "MON-A4 restore admin call failed");
    auto restore_entries =
      wait_evidence_contains (options.service_url,
                              "admin|rid=svc-a|action=server-weight|weight=100",
                              std::chrono::milliseconds (10000));
    ensure (count_contains (restore_entries, "kind=PeerAdmissionChanged") >= 2,
            "MON-A4 restore socket admission evidence missing");
    const auto location_entries = wait_evidence_count_at_least (
      options.service_url, "monitor-location|source=location-runtime|kind=TopologyChanged", 2,
      std::chrono::milliseconds (10000));
    ensure (mon_a4_contains_nonzero_location_event (location_entries, "kind=TopologyChanged",
                                                   "topology=0"),
            "MON-A4 location topology transition evidence missing");
    std::cout << "scenario MON-A4 passed\n";
}

} // namespace zlink::framework::e2e::runtime_monitoring::client
