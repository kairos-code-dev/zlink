/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Support/client_support.hpp"

#include <chrono>
#include <iostream>
#include <string>

namespace zlink::framework::e2e::discovery_registry_ha::client
{

inline void run_dr_a1_basic_discovery_scenario ()
{
    const auto registry_url = env_or ("ZLINK_CPP_DRHA_REGISTRY_URL");
    const auto consumer_url = env_or ("ZLINK_CPP_DRHA_CONSUMER_URL");
    const auto provider_a_url = env_or ("ZLINK_CPP_DRHA_PROVIDER_A_URL");
    const auto provider_b_url = env_or ("ZLINK_CPP_DRHA_PROVIDER_B_URL");
    ensure (!registry_url.empty (), "ZLINK_CPP_DRHA_REGISTRY_URL is required");
    ensure (!consumer_url.empty (), "ZLINK_CPP_DRHA_CONSUMER_URL is required");
    ensure (!provider_a_url.empty (), "ZLINK_CPP_DRHA_PROVIDER_A_URL is required");
    ensure (!provider_b_url.empty (), "ZLINK_CPP_DRHA_PROVIDER_B_URL is required");

    post_json<topology_ready_wait_req_t, operation_status_t> (
      registry_url, "/registry/topology/wait", {.ready_count = 2});

    const auto marker =
      "dr-a1-" + std::to_string (std::chrono::steady_clock::now ().time_since_epoch ().count ());
    const auto reply = post_json<profile_req_t, profile_res_t> (
      consumer_url, "/profile/request", {.value = "dr-a1", .marker = marker});
    ensure (reply.value == "profile:dr-a1", "DR-A1 request failed");
    ensure (reply.provider_rid == "api-a" || reply.provider_rid == "api-b",
            "DR-A1 unexpected provider");
    ensure (reply.marker == marker, "DR-A1 marker mismatch");

    const auto provider_url = reply.provider_rid == "api-a" ? provider_a_url : provider_b_url;
    const auto evidence = post_json<evidence_wait_req_t, evidence_snapshot_t> (
      provider_url, "/evidence/wait", {.contains = marker});
    ensure (evidence_contains (evidence, marker, reply.provider_rid),
            "DR-A1 provider evidence was not recorded");

    std::cout << "scenario DiscoveryRegistryHa.A1 passed\n";
}

} // namespace zlink::framework::e2e::discovery_registry_ha::client
