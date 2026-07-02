/* SPDX-License-Identifier: MPL-2.0 */

#pragma once

#include "../Support/client_support.hpp"

#include <iostream>
#include <set>

namespace zlink::framework::e2e::resilience_lifecycle::client
{

inline void run_rl_b3_graceful_shutdown_scenario ()
{
    std::set<std::string> before;
    for (int index = 0; index < 80 && before.size () < 2; ++index) {
        const auto reply = post_consumer_profile ("rl-b3-before-shutdown-" + std::to_string (index));
        before.insert (reply.provider_rid);
    }
    ensure (before.contains ("api-a") && before.contains ("api-b"),
            "RL-B3 did not start with both providers");

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
