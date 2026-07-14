/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#pragma once

#include "../Support/resilience_request_support.hpp"

#include <chrono>
#include <iostream>
#include <string>

namespace zlink::framework::e2e::resilience_lifecycle::client
{

inline void run_rl_b6_gray_fault_scenario ()
{
    post_provider_admin (env_or ("ZLINK_CPP_E2E_HTTP_B_ENDPOINT"), "/admin/fault/gray");

    int failures = 0;
    int healthy_successes = 0;
    for (int index = 0; index < 60; ++index) {
        const auto marker = "rl-b6-" + std::to_string (index);
        const auto value = index % 3 == 0 ? "gray" : "fast";
        try {
            const auto reply = post_consumer_profile (value, marker);
            if (reply.provider_rid == "api-a") {
                ++healthy_successes;
            }
        }
        catch (const zlink::framework::framework_exception_t &error) {
            ensure (error.kind () == zlink::framework::framework_error_kind_t::request_failed,
                    "RL-B6 gray request returned an unexpected public error");
            ++failures;
        }
    }
    ensure (healthy_successes > 0, "RL-B6 healthy provider did not handle gray-fault traffic");
    ensure (failures > 0, "RL-B6 gray provider did not fail any gray request");

    post_provider_admin (env_or ("ZLINK_CPP_E2E_HTTP_B_ENDPOINT"), "/admin/fault/none");
    const auto follow_up = request_profile ("fast", "rl-b6-after");
    ensure (follow_up.value == "profile:fast", "RL-B6 follow-up request failed");
    wait_provider_evidence_contains ("ProfileReq", "rl-b6-after",
                                     std::chrono::milliseconds (5000));

    std::cout << "scenario RL-B6 passed\n";
}

} // namespace zlink::framework::e2e::resilience_lifecycle::client
