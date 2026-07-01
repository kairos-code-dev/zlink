/* SPDX-License-Identifier: MPL-2.0 */

#pragma once

#include "../Support/resilience_request_support.hpp"

#include <chrono>
#include <iostream>
#include <string>

namespace zlink::framework::e2e::resilience_lifecycle::client
{

inline void run_registry_outage_scenario (zlink::framework::channel_client_t &channels)
{
    const auto before =
      request_profile (channels, "resilience.lifecycle.api.manual", "rl-c4-before-outage");
    ensure (before.value == "profile:rl-c4-before-outage",
            "RL-C4 request failed before registry outage");

    touch_file (env_or ("ZLINK_CPP_E2E_READY_FILE"));
    wait_for_file (env_or ("ZLINK_CPP_E2E_CONTINUE_FILE"));

    const auto during =
      request_profile (channels, "resilience.lifecycle.api.manual", "rl-c4-during-outage");
    ensure (during.value == "profile:rl-c4-during-outage",
            "RL-C4 established channel failed during registry outage");

    wait_provider_evidence_contains ("ProfileReq", "rl-c4-before-outage",
                                     std::chrono::seconds (10));
    wait_provider_evidence_contains ("ProfileReq", "rl-c4-during-outage",
                                     std::chrono::seconds (10));

    touch_file (env_or ("ZLINK_CPP_E2E_DRAINED_FILE"));
    std::cout << "scenario RL-C4 passed\n";
}

inline void run_registry_recovered_scenario (zlink::framework::channel_client_t &channels)
{
    const auto deadline = std::chrono::steady_clock::now () + std::chrono::seconds (20);
    while (std::chrono::steady_clock::now () < deadline) {
        auto task = channels.request (api_channel, profile_req_t{.value = "rl-c4-after-restart"})
                      .timeout (std::chrono::milliseconds (2000))
                      .async<profile_res_t> ();
        if (task.result ().has_value () && task.result ().value ().value == "profile:rl-c4-after-restart") {
            wait_provider_evidence_contains ("ProfileReq", "rl-c4-after-restart",
                                             std::chrono::seconds (10));
            std::cout << "scenario RL-C4 recovery passed\n";
            return;
        }
        std::this_thread::sleep_for (std::chrono::milliseconds (500));
    }
    throw std::runtime_error ("RL-C4 new discovery client did not recover after registry restart");
}

} // namespace zlink::framework::e2e::resilience_lifecycle::client
