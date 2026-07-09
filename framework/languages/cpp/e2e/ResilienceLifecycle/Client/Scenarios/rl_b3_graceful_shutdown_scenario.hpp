/* SPDX-License-Identifier: MPL-2.0 */

#pragma once

#include "../Support/client_support.hpp"

#include <iostream>

namespace zlink::framework::e2e::resilience_lifecycle::client
{

inline void run_rl_b3_graceful_shutdown_scenario ()
{
    const auto before = post_consumer_profile ("fast", "rl-b3-before-shutdown");
    ensure (before.provider_rid == "api-a" || before.provider_rid == "api-b",
            "RL-B3 pre-shutdown request failed");

    touch_file (env_or ("ZLINK_CPP_E2E_READY_FILE"));
    wait_for_file (env_or ("ZLINK_CPP_E2E_CONTINUE_FILE"));

    for (int index = 0; index < 20; ++index) {
        const auto reply = post_consumer_profile ("rl-b3-after-shutdown-" + std::to_string (index));
        ensure (reply.provider_rid == "api-a",
                "RL-B3 routed to stopped provider after graceful shutdown");
    }
    std::cout << "scenario RL-B3 client passed\n";
}

} // namespace zlink::framework::e2e::resilience_lifecycle::client
