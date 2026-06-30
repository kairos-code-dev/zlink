/* SPDX-License-Identifier: MPL-2.0 */

#pragma once

#include "../Support/resilience_request_support.hpp"

#include <chrono>
#include <iostream>
#include <string>
#include <thread>

namespace zlink::framework::e2e::registry_messaging::client
{

inline void run_drain_restore_scenario (zlink::framework::channel_client_t &channels)
{
    auto inflight = channels
                      .request ("registry.messaging.api.manual.b",
                                profile_request_t{.value = "slow"})
                      .timeout (std::chrono::milliseconds (3000))
                      .async<profile_reply_t> ();

    const auto deadline = std::chrono::steady_clock::now () + std::chrono::seconds (5);
    while (std::chrono::steady_clock::now () < deadline) {
        if (evidence_contains (fetch_evidence (env_or ("ZLINK_CPP_E2E_HTTP_B_ENDPOINT")),
                               "ProfileRequest", "slow")) {
            break;
        }
        std::this_thread::sleep_for (std::chrono::milliseconds (50));
    }
    ensure (evidence_contains (fetch_evidence (env_or ("ZLINK_CPP_E2E_HTTP_B_ENDPOINT")),
                               "ProfileRequest", "slow"),
            "slow in-flight request did not reach provider before drain");
    touch_file (env_or ("ZLINK_CPP_E2E_READY_FILE"));
    wait_for_file (env_or ("ZLINK_CPP_E2E_CONTINUE_FILE"));

    ensure (inflight.result ().has_value (), "in-flight request failed during drain");
    ensure (inflight.result ().value ().provider_rid == "api-b",
            "in-flight request did not complete on drained provider");

    for (int index = 0; index < 20; ++index) {
        const auto reply =
          request_profile (channels, api_channel, "rl-b4-after-drain-" + std::to_string (index));
        ensure (reply.provider_rid == "api-a",
                "new request reached drained provider before restore");
    }

    touch_file (env_or ("ZLINK_CPP_E2E_DRAINED_FILE"));
    wait_for_file (env_or ("ZLINK_CPP_E2E_RESTORE_FILE"));

    bool restored = false;
    for (int index = 0; index < 80; ++index) {
        const auto reply =
          request_profile (channels, api_channel, "rl-b4-after-restore-" + std::to_string (index));
        if (reply.provider_rid == "api-b") {
            restored = true;
            break;
        }
    }
    ensure (restored, "restored provider did not receive traffic");
    std::cout << "scenario RL-B5 passed\n";
    std::cout << "scenario RL-B4 passed\n";
}

} // namespace zlink::framework::e2e::registry_messaging::client
