/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Support/client_support.hpp"

#include <chrono>
#include <iostream>
#include <optional>
#include <string>

namespace zlink::framework::e2e::discovery_registry_ha::client
{

inline std::optional<evidence_snapshot_t> try_wait_for_evidence (const std::string &provider_url,
                                                                 const std::string &marker)
{
    try {
        return post_json<evidence_wait_req_t, evidence_snapshot_t> (
          provider_url, "/evidence/wait", {.contains = marker, .timeout_milliseconds = 1000},
          std::chrono::milliseconds (1500));
    }
    catch (const std::exception &) {
        return std::nullopt;
    }
}

inline void run_dr_a4_third_registry_scenario ()
{
    const auto consumer_url = env_or ("ZLINK_CPP_DRHA_REG2_CONSUMER_URL");
    const auto provider_a_url = env_or ("ZLINK_CPP_DRHA_PROVIDER_A_URL");
    const auto duplicate_provider_url = env_or ("ZLINK_CPP_DRHA_DUPLICATE_PROVIDER_URL");
    ensure (!consumer_url.empty (), "ZLINK_CPP_DRHA_REG2_CONSUMER_URL is required");
    ensure (!provider_a_url.empty (), "ZLINK_CPP_DRHA_PROVIDER_A_URL is required");
    ensure (!duplicate_provider_url.empty (), "ZLINK_CPP_DRHA_DUPLICATE_PROVIDER_URL is required");

    const auto marker =
      "dr-a4-" + std::to_string (std::chrono::steady_clock::now ().time_since_epoch ().count ());
    const auto reply = post_json<profile_req_t, profile_res_t> (
      consumer_url, "/profile/request/wait", {.value = "dr-a4", .marker = marker});
    ensure (reply.value == "profile:dr-a4", "DR-A4 request failed");
    ensure (reply.provider_rid == "api-a", "DR-A4 same-rid providers should report api-a");
    ensure (reply.marker == marker, "DR-A4 marker mismatch");

    const auto provider_a_evidence = try_wait_for_evidence (provider_a_url, marker);
    if (provider_a_evidence && evidence_contains (*provider_a_evidence, marker, "api-a")) {
        std::cout << "scenario DR-A4 passed\n";
        return;
    }

    const auto duplicate_evidence = try_wait_for_evidence (duplicate_provider_url, marker);
    ensure (duplicate_evidence && evidence_contains (*duplicate_evidence, marker, "api-a"),
            "DR-A4 provider evidence was not recorded");

    std::cout << "scenario DR-A4 passed\n";
}

} // namespace zlink::framework::e2e::discovery_registry_ha::client
