/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#pragma once

#include "../Support/client_support.hpp"

#include <chrono>
#include <iostream>

namespace zlink::framework::e2e::runtime_monitoring::client
{

inline bool contains_nonzero_location_event (const std::vector<std::string> &entries,
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

inline void run_mon_a2_location_events_scenario (const client_options_t &options)
{
    const auto entries = wait_evidence_contains (
      options.service_url,
      "monitor-location|source=location-runtime|kind=TopologyChanged|nodes=svc-a,svc-b",
      std::chrono::milliseconds (10000));
    ensure (contains_nonzero_location_event (entries, "kind=TopologyChanged", "topology=0")
              && any_contains (entries, "kind=TopologyChanged|nodes=svc-a,svc-b"),
            "MON-A2 svc-b topology change evidence missing");
    ensure (contains_nonzero_location_event (entries, "kind=ServiceSummaryChanged", "summary=0"),
            "MON-A2 location service summary evidence missing");
    const auto topology_count = count_contains (entries, "kind=TopologyChanged");
    const auto summary_count = count_contains (entries, "kind=ServiceSummaryChanged");
    std::this_thread::sleep_for (std::chrono::milliseconds (500));
    const auto stable_entries = fetch_evidence (options.service_url);
    ensure (count_contains (stable_entries, "kind=TopologyChanged") == topology_count,
            "MON-A2 emitted duplicate topology events without a location change");
    ensure (count_contains (stable_entries, "kind=ServiceSummaryChanged") == summary_count,
            "MON-A2 emitted duplicate service summary events without a location change");
    std::cout << "scenario MON-A2 passed\n";
}

} // namespace zlink::framework::e2e::runtime_monitoring::client
