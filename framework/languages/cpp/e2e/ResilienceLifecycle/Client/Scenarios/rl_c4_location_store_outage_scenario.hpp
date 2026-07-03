/* SPDX-License-Identifier: MPL-2.0 */

#pragma once

#include "../Support/resilience_request_support.hpp"

#include <chrono>
#include <iostream>
#include <string>

namespace zlink::framework::e2e::resilience_lifecycle::client
{

inline void run_location_store_outage_scenario ()
{
    const auto before = post_consumer_profile ("fast", "rl-c4-before-outage",
                                               "/profile/request/manual");
    ensure (before.value == "profile:fast",
            "RL-C4 request failed before location store outage");

    touch_file (env_or ("ZLINK_CPP_E2E_READY_FILE"));
    wait_for_file (env_or ("ZLINK_CPP_E2E_CONTINUE_FILE"));

    const auto during = post_consumer_profile ("fast", "rl-c4-during-outage",
                                               "/profile/request/manual");
    ensure (during.value == "profile:fast",
            "RL-C4 established channel failed during location store outage");

    wait_provider_evidence_contains ("ProfileReq", "rl-c4-before-outage",
                                     std::chrono::seconds (10));
    wait_provider_evidence_contains ("ProfileReq", "rl-c4-during-outage",
                                     std::chrono::seconds (10));

    touch_file (env_or ("ZLINK_CPP_E2E_DRAINED_FILE"));
    std::cout << "scenario RL-C4 passed\n";
}

inline void run_location_store_recovered_scenario ()
{
    const auto deadline = std::chrono::steady_clock::now () + std::chrono::seconds (20);
    while (std::chrono::steady_clock::now () < deadline) {
        try {
            const auto reply = post_consumer_profile ("fast", "rl-c4-after-restart",
                                                      "/profile/request/new-client");
            if (reply.value != "profile:fast") {
                std::this_thread::sleep_for (std::chrono::milliseconds (500));
                continue;
            }
            wait_provider_evidence_contains ("ProfileReq", "rl-c4-after-restart",
                                             std::chrono::seconds (10));
            std::cout << "scenario RL-C4 recovery passed\n";
            return;
        }
        catch (const std::exception &) {
        }
        std::this_thread::sleep_for (std::chrono::milliseconds (500));
    }
    throw std::runtime_error (
      "RL-C4 new discovery client did not recover after location store recovery");
}

} // namespace zlink::framework::e2e::resilience_lifecycle::client
