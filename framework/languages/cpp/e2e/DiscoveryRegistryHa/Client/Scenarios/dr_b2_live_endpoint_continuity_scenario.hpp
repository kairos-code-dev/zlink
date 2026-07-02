/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Support/client_support.hpp"

#include <chrono>
#include <iostream>
#include <string>

namespace zlink::framework::e2e::discovery_registry_ha::client
{

inline void run_dr_b2_live_endpoint_continuity_scenario ()
{
    const auto reg1_url = env_or ("ZLINK_CPP_DRHA_REG1_URL");
    const auto consumer_url = env_or ("ZLINK_CPP_DRHA_REG1_REG2_CONSUMER_URL");
    const auto provider_a_url = env_or ("ZLINK_CPP_DRHA_PROVIDER_A_URL");
    const auto provider_b_url = env_or ("ZLINK_CPP_DRHA_PROVIDER_B_URL");
    const auto api_a_endpoint = env_or ("ZLINK_CPP_DRHA_API_A_ENDPOINT");

    ensure (!reg1_url.empty (), "ZLINK_CPP_DRHA_REG1_URL is required");
    ensure (!consumer_url.empty (), "ZLINK_CPP_DRHA_REG1_REG2_CONSUMER_URL is required");
    ensure (!provider_a_url.empty (), "ZLINK_CPP_DRHA_PROVIDER_A_URL is required");
    ensure (!provider_b_url.empty (), "ZLINK_CPP_DRHA_PROVIDER_B_URL is required");
    ensure (!api_a_endpoint.empty (), "ZLINK_CPP_DRHA_API_A_ENDPOINT is required");

    post_json<member_endpoint_wait_req_t, operation_status_t> (
      reg1_url, "/registry/members/wait", {.endpoint = api_a_endpoint});

    const auto marker =
      "dr-b2-" + std::to_string (std::chrono::steady_clock::now ().time_since_epoch ().count ());
    const auto reply = post_json<profile_req_t, profile_res_t> (
      consumer_url, "/profile/request", {.value = "dr-b2", .marker = marker});
    ensure (reply.value == "profile:dr-b2", "DR-B2 request failed");
    ensure (reply.provider_rid == "api-a" || reply.provider_rid == "api-b",
            "DR-B2 routed to an unexpected provider");
    ensure (reply.marker == marker, "DR-B2 marker mismatch");

    const auto evidence_url = reply.provider_rid == "api-a" ? provider_a_url : provider_b_url;
    const auto evidence = post_json<evidence_wait_req_t, evidence_snapshot_t> (
      evidence_url, "/evidence/wait", {.contains = marker});
    ensure (evidence_contains (evidence, marker, reply.provider_rid),
            "DR-B2 provider evidence was not recorded");

    std::cout << "scenario DR-B2 passed\n";
}

} // namespace zlink::framework::e2e::discovery_registry_ha::client
