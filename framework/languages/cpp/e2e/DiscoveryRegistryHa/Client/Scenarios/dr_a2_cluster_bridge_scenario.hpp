/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Support/client_support.hpp"

#include <chrono>
#include <iostream>
#include <string>

namespace zlink::framework::e2e::discovery_registry_ha::client
{

inline void run_dr_a2_cluster_bridge_scenario ()
{
    const auto reg2_url = env_or ("ZLINK_CPP_DRHA_REG2_URL");
    const auto consumer_url = env_or ("ZLINK_CPP_DRHA_CONSUMER_URL");
    const auto provider_a_url = env_or ("ZLINK_CPP_DRHA_PROVIDER_A_URL");
    const auto api_a_endpoint = env_or ("ZLINK_CPP_DRHA_API_A_ENDPOINT");
    ensure (!reg2_url.empty (), "ZLINK_CPP_DRHA_REG2_URL is required");
    ensure (!consumer_url.empty (), "ZLINK_CPP_DRHA_CONSUMER_URL is required");
    ensure (!provider_a_url.empty (), "ZLINK_CPP_DRHA_PROVIDER_A_URL is required");
    ensure (!api_a_endpoint.empty (), "ZLINK_CPP_DRHA_API_A_ENDPOINT is required");

    post_json<member_endpoint_wait_req_t, operation_status_t> (reg2_url, "/registry/members/wait",
                                                               {.endpoint = api_a_endpoint});

    const auto marker =
      "dr-a2-" + std::to_string (std::chrono::steady_clock::now ().time_since_epoch ().count ());
    const auto reply = post_json<profile_req_t, profile_res_t> (
      consumer_url, "/profile/request", {.value = "dr-a2", .marker = marker});
    ensure (reply.value == "profile:dr-a2", "DR-A2 request failed");
    ensure (reply.provider_rid == "api-a", "DR-A2 request should route to api-a");
    ensure (reply.marker == marker, "DR-A2 marker mismatch");

    const auto evidence = post_json<evidence_wait_req_t, evidence_snapshot_t> (
      provider_a_url, "/evidence/wait", {.contains = marker});
    ensure (evidence_contains (evidence, marker, "api-a"), "DR-A2 api-a evidence was not recorded");

    std::cout << "scenario DR-A2 passed\n";
}

} // namespace zlink::framework::e2e::discovery_registry_ha::client
