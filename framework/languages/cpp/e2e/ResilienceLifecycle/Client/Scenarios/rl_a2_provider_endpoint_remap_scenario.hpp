/* SPDX-License-Identifier: MPL-2.0 */

#pragma once

#include "../Support/client_support.hpp"

#include <iostream>

namespace zlink::framework::e2e::resilience_lifecycle::client
{

inline void run_rl_a2_provider_endpoint_remap_scenario ()
{
    const auto first = post_consumer_profile ("rl-a2-before-remap");
    ensure (first.provider_rid == "api-a" && first.instance_id == "api-a-v1",
            "RL-A2 initial provider mismatch");

    touch_file (env_or ("ZLINK_CPP_E2E_READY_FILE"));
    wait_for_file (env_or ("ZLINK_CPP_E2E_CONTINUE_FILE"));

    for (int index = 0; index < 20; ++index) {
        const auto reply = post_consumer_profile ("rl-a2-after-remap-" + std::to_string (index));
        ensure (reply.provider_rid == "api-a" && reply.instance_id == "api-a-v2",
                "RL-A2 did not switch to remapped provider endpoint");
    }
    std::cout << "scenario RL-A2 client passed\n";
}

} // namespace zlink::framework::e2e::resilience_lifecycle::client
