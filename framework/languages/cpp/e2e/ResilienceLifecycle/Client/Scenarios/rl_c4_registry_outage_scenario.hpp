/* SPDX-License-Identifier: MPL-2.0 */

#pragma once

#include "../Support/resilience_request_support.hpp"

#include <chrono>
#include <iostream>
#include <string>

namespace zlink::framework::e2e::registry_messaging::client
{

inline void run_registry_outage_scenario (zlink::framework::channel_client_t &channels)
{
    const auto before =
      request_profile (channels, "registry.messaging.api.manual", "rl-c4-before-outage");
    ensure (before.value == "profile:rl-c4-before-outage",
            "RL-C4 request failed before registry outage");

    touch_file (env_or ("ZLINK_CPP_E2E_READY_FILE"));
    wait_for_file (env_or ("ZLINK_CPP_E2E_CONTINUE_FILE"));

    const auto during =
      request_profile (channels, "registry.messaging.api.manual", "rl-c4-during-outage");
    ensure (during.value == "profile:rl-c4-during-outage",
            "RL-C4 established channel failed during registry outage");

    wait_provider_evidence_contains ("ProfileRequest", "rl-c4-before-outage",
                                     std::chrono::seconds (10));
    wait_provider_evidence_contains ("ProfileRequest", "rl-c4-during-outage",
                                     std::chrono::seconds (10));

    touch_file (env_or ("ZLINK_CPP_E2E_DRAINED_FILE"));
    std::cout << "scenario RL-C4 passed\n";
}

} // namespace zlink::framework::e2e::registry_messaging::client
