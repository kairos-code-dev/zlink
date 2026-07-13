/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#pragma once

#include "../Support/resilience_request_support.hpp"

#include <chrono>
#include <iostream>
#include <string>

namespace zlink::framework::e2e::resilience_lifecycle::client
{

inline void run_resilience_stress_scenario ()
{
    for (int index = 0; index < 40; ++index) {
        const auto reply = request_profile ("rl-d1-request-" + std::to_string (index));
        ensure (!reply.provider_rid.empty (), "stress request returned empty provider");
        post_consumer_command ("rl-d5-command-" + std::to_string (index));
    }

    const auto missing = post_consumer_missing ("rl-d4-missing");
    ensure (missing.failed, "missing request handler unexpectedly returned a typed reply");
    std::cout << "scenario RL-D1 passed\n";
    std::cout << "scenario RL-D4 passed\n";
    std::cout << "scenario RL-D5 passed\n";
}

} // namespace zlink::framework::e2e::resilience_lifecycle::client
