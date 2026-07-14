/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#pragma once

#include "../Support/client_support.hpp"
#include "../../Shared/runtime_monitoring_contracts.hpp"

#include <chrono>
#include <iostream>

namespace zlink::framework::e2e::runtime_monitoring::client
{

inline int verify_down_up_cycles (const std::vector<std::string> &entries)
{
    bool provider_ready = false;
    bool provider_seen = false;
    bool down_pending = false;
    int completed_cycles = 0;
    for (const auto &entry : entries) {
        if (!contains (entry, "kind=TopologyChanged|")) {
            continue;
        }
        const auto ready = contains (entry, "routes=") && contains (entry, "svc-b@");
        if (ready) {
            if (down_pending) {
                ++completed_cycles;
                down_pending = false;
            }
            provider_ready = true;
            provider_seen = true;
        } else if (provider_seen && provider_ready) {
            provider_ready = false;
            down_pending = true;
        }
    }
    return completed_cycles;
}

inline void run_mon_d1_failure_recovery_scenario (const client_options_t &options)
{
    const auto location_evidence = fetch_evidence (options.trigger_url);
    ensure (verify_down_up_cycles (location_evidence) >= 2,
            "MON-D1 did not observe two ordered svc-b down/up cycles");

    auto reply = post_profile_request (options.trigger_url, "/profile/request/service-b",
                                       profile_req_t{.value = "restart", .marker = "mon-d1"});
    ensure (reply.provider_rid == "svc-b" && reply.marker == "mon-d1"
              && reply.value == "profile:restart",
            "MON-D1 restarted service reply mismatch");

    const auto evidence = wait_evidence_contains (
      options.filtered_service_url, "profile-request|rid=svc-b|marker=mon-d1|value=restart",
      std::chrono::milliseconds (10000));
    ensure (any_contains (evidence, "profile-request|rid=svc-b|marker=mon-d1|value=restart"),
            "MON-D1 restarted service evidence missing");

    std::cout << "scenario MON-D1 passed\n";
}

} // namespace zlink::framework::e2e::runtime_monitoring::client
