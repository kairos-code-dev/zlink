/* SPDX-License-Identifier: MPL-2.0 */

#pragma once

#include "../Support/client_support.hpp"
#include "../../Shared/runtime_monitoring_contracts.hpp"
#include "mon_a1_socket_events_scenario.hpp"

#include <zlink/framework.hpp>

#include <chrono>
#include <iostream>

namespace zlink::framework::e2e::runtime_monitoring::client
{

inline void run_mon_d1_failure_recovery_scenario (zlink::framework::channel_client_t &channels)
{
    constexpr auto registry_topology_event =
      "monitor-registry|source=registry|kind=TopologyChanged";
    const auto initial_registry_evidence = fetch_evidence (env_or ("ZLINK_CPP_E2E_REGISTRY_URL"));
    const auto initial_registry_topology_count =
      count_contains (initial_registry_evidence, registry_topology_event);

    profile_reply_t reply;
    if (const auto trigger_url = env_or ("ZLINK_CPP_E2E_TRIGGER_URL"); !trigger_url.empty ()) {
        reply = post_profile_request (trigger_url, "/profile/request/service-b",
                                      profile_request_t{.value = "restart", .marker = "mon-d1"});
    } else {
        auto request = channels
                         .request (profile_channel,
                                   profile_request_t{.value = "restart", .marker = "mon-d1"})
                         .timeout (std::chrono::milliseconds (3000))
                         .async<profile_reply_t> ();
        const auto &result = request.result ();
        ensure (result.has_value (),
                "MON-D1 restarted service request failed: "
                  + std::string (result.error () ? result.error ()->what () : "unknown"));
        reply = result.value ();
    }
    ensure (reply.provider_rid == "svc-b" && reply.marker == "mon-d1"
              && reply.value == "profile:restart",
            "MON-D1 restarted service reply mismatch");

    const auto evidence = wait_evidence_contains (
      env_or ("ZLINK_CPP_E2E_FILTERED_SERVICE_URL"),
      "profile-request|rid=svc-b|marker=mon-d1|value=restart",
      std::chrono::milliseconds (10000));
    ensure (any_contains (evidence, "profile-request|rid=svc-b|marker=mon-d1|value=restart"),
            "MON-D1 restarted service evidence missing");

    const auto registry_evidence = wait_evidence_count_at_least (
      env_or ("ZLINK_CPP_E2E_REGISTRY_URL"), registry_topology_event,
      initial_registry_topology_count + 1, std::chrono::milliseconds (10000));
    ensure (count_contains (registry_evidence, registry_topology_event)
              >= initial_registry_topology_count + 1,
            "MON-D1 registry topology continuity evidence missing");
    std::cout << "scenario MON-D1 passed\n";
}

} // namespace zlink::framework::e2e::runtime_monitoring::client
