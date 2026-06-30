/* SPDX-License-Identifier: MPL-2.0 */

#pragma once

#include "../Support/client_support.hpp"

#include <chrono>
#include <iostream>

namespace zlink::framework::e2e::runtime_monitoring::client
{

inline bool contains_nonzero_registry_event (const std::vector<std::string> &entries,
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

inline void run_mon_a2_registry_events_scenario (const client_options_t &options)
{
    const auto entries = wait_evidence_contains (
      options.registry_url, "kind=ServiceSummaryChanged",
      std::chrono::milliseconds (10000));
    ensure (contains_nonzero_registry_event (entries, "kind=TopologyChanged", "topology=0"),
            "MON-A2 registry topology evidence missing");
    ensure (contains_nonzero_registry_event (entries, "kind=ServiceSummaryChanged", "summary=0"),
            "MON-A2 registry service summary evidence missing");
    std::cout << "scenario MON-A2 passed\n";
}

} // namespace zlink::framework::e2e::runtime_monitoring::client
