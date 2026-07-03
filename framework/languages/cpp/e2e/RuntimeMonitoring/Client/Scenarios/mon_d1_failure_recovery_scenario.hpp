/* SPDX-License-Identifier: MPL-2.0 */

#pragma once

#include "../Support/client_support.hpp"
#include "../../Shared/runtime_monitoring_contracts.hpp"

#include <chrono>
#include <iostream>

namespace zlink::framework::e2e::runtime_monitoring::client
{

inline void run_mon_d1_failure_recovery_scenario (const client_options_t &options)
{
    constexpr auto location_topology_event =
      "monitor-location|source=location-runtime|kind=TopologyChanged";
    const auto initial_location_evidence = fetch_evidence (options.service_url);
    const auto initial_location_topology_count =
      count_contains (initial_location_evidence, location_topology_event);

    auto reply = post_profile_request (options.trigger_url, "/profile/request/service-b",
                                       profile_req_t{.value = "restart", .marker = "mon-d1"});
    ensure (reply.provider_rid == "svc-b" && reply.marker == "mon-d1"
              && reply.value == "profile:restart",
            "MON-D1 restarted service reply mismatch");

    const auto evidence = wait_evidence_contains (
      options.filtered_service_url,
      "profile-request|rid=svc-b|marker=mon-d1|value=restart",
      std::chrono::milliseconds (10000));
    ensure (any_contains (evidence, "profile-request|rid=svc-b|marker=mon-d1|value=restart"),
            "MON-D1 restarted service evidence missing");

    const auto location_evidence = wait_evidence_count_at_least (
      options.service_url, location_topology_event,
      initial_location_topology_count + 1, std::chrono::milliseconds (10000));
    ensure (count_contains (location_evidence, location_topology_event)
              >= initial_location_topology_count + 1,
            "MON-D1 location topology continuity evidence missing");
    std::cout << "scenario MON-D1 passed\n";
}

} // namespace zlink::framework::e2e::runtime_monitoring::client
