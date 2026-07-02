/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Support/client_support.hpp"

#include <chrono>
#include <iostream>
#include <string>
#include <vector>

namespace zlink::framework::e2e::discovery_registry_ha::client
{

struct dr_b1_registry_case_t
{
    std::string name;
    std::string registry_url;
    std::string consumer_url;
};

inline void run_dr_b1_late_start_registry_scenario ()
{
    const auto reg2_url = env_or ("ZLINK_CPP_DRHA_REG2_URL");
    const auto reg3_url = env_or ("ZLINK_CPP_DRHA_REG3_URL");
    const auto reg2_consumer_url = env_or ("ZLINK_CPP_DRHA_REG2_CONSUMER_URL");
    const auto reg3_consumer_url = env_or ("ZLINK_CPP_DRHA_REG3_CONSUMER_URL");
    const auto provider_a_url = env_or ("ZLINK_CPP_DRHA_PROVIDER_A_URL");
    const auto provider_b_url = env_or ("ZLINK_CPP_DRHA_PROVIDER_B_URL");
    const auto api_a_endpoint = env_or ("ZLINK_CPP_DRHA_API_A_ENDPOINT");
    const auto api_b_endpoint = env_or ("ZLINK_CPP_DRHA_API_B_ENDPOINT");

    ensure (!reg2_url.empty (), "ZLINK_CPP_DRHA_REG2_URL is required");
    ensure (!reg3_url.empty (), "ZLINK_CPP_DRHA_REG3_URL is required");
    ensure (!reg2_consumer_url.empty (), "ZLINK_CPP_DRHA_REG2_CONSUMER_URL is required");
    ensure (!reg3_consumer_url.empty (), "ZLINK_CPP_DRHA_REG3_CONSUMER_URL is required");
    ensure (!provider_a_url.empty (), "ZLINK_CPP_DRHA_PROVIDER_A_URL is required");
    ensure (!provider_b_url.empty (), "ZLINK_CPP_DRHA_PROVIDER_B_URL is required");
    ensure (!api_a_endpoint.empty (), "ZLINK_CPP_DRHA_API_A_ENDPOINT is required");
    ensure (!api_b_endpoint.empty (), "ZLINK_CPP_DRHA_API_B_ENDPOINT is required");

    const std::vector<dr_b1_registry_case_t> cases{
      {"reg-2", reg2_url, reg2_consumer_url},
      {"reg-3", reg3_url, reg3_consumer_url},
    };

    for (const auto &registry_case : cases) {
        post_json<member_endpoint_wait_req_t, operation_status_t> (
          registry_case.registry_url, "/registry/members/wait", {.endpoint = api_a_endpoint});
        post_json<member_endpoint_wait_req_t, operation_status_t> (
          registry_case.registry_url, "/registry/members/wait", {.endpoint = api_b_endpoint});

        const auto marker =
          "dr-b1-" + registry_case.name + "-"
          + std::to_string (std::chrono::steady_clock::now ().time_since_epoch ().count ());
        const auto reply = post_json<profile_req_t, profile_res_t> (
          registry_case.consumer_url, "/profile/request", {.value = "dr-b1", .marker = marker});
        ensure (reply.value == "profile:dr-b1", "DR-B1 " + registry_case.name + " request failed");
        ensure (reply.provider_rid == "api-a" || reply.provider_rid == "api-b",
                "DR-B1 " + registry_case.name + " routed to an unexpected provider");
        ensure (reply.marker == marker, "DR-B1 " + registry_case.name + " marker mismatch");

        const auto evidence_url = reply.provider_rid == "api-a" ? provider_a_url : provider_b_url;
        const auto evidence = post_json<evidence_wait_req_t, evidence_snapshot_t> (
          evidence_url, "/evidence/wait", {.contains = marker});
        ensure (evidence_contains (evidence, marker, reply.provider_rid),
                "DR-B1 " + registry_case.name + " provider evidence was not recorded");
    }

    std::cout << "scenario DR-B1 passed\n";
}

} // namespace zlink::framework::e2e::discovery_registry_ha::client
