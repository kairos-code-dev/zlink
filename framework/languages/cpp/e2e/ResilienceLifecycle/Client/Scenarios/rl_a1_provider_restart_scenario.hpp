/* SPDX-License-Identifier: MPL-2.0 */

#pragma once

#include "../Support/client_support.hpp"

#include <iostream>

namespace zlink::framework::e2e::resilience_lifecycle::client
{

inline void run_rl_a1_provider_restart_scenario ()
{
    const auto first = post_consumer_profile ("rl-a1-before-restart");
    ensure (first.provider_rid == "api-a" && first.instance_id == "api-a-v1",
            "RL-A1 initial provider mismatch");

    touch_file (env_or ("ZLINK_CPP_E2E_READY_FILE"));
    wait_for_file (env_or ("ZLINK_CPP_E2E_CONTINUE_FILE"));

    for (int index = 0; index < 20; ++index) {
        const auto reply = post_consumer_profile ("rl-a1-after-restart-" + std::to_string (index));
        ensure (reply.provider_rid == "api-a" && reply.instance_id == "api-a-v2",
                "RL-A1 did not switch to replacement provider");
    }
    std::cout << "scenario RL-A1 client passed\n";
}

} // namespace zlink::framework::e2e::resilience_lifecycle::client
