/* SPDX-License-Identifier: MPL-2.0 */

#pragma once

#include "../Support/client_support.hpp"

#include <iostream>

namespace zlink::framework::e2e::resilience_lifecycle::client
{

inline void run_rl_b1_cancellation_cleanup_scenario ()
{
    const auto slow = post_consumer_profile_raw ("slow", "slow", "/profile/request/timeout/100");
    ensure (slow.status >= 400 || slow.body.find ("failed") != std::string::npos,
            "RL-B1 slow request unexpectedly succeeded");
    const auto after = post_consumer_profile ("rl-b1-after-timeout");
    ensure (after.value == "profile:rl-b1-after-timeout", "RL-B1 post-timeout request failed");
    std::this_thread::sleep_for (std::chrono::milliseconds (1100));
    const auto later = post_consumer_profile ("rl-b1-later");
    ensure (later.value == "profile:rl-b1-later", "RL-B1 later request failed");
    wait_provider_evidence_contains ("ProfileReq", "rl-b1-after-timeout",
                                     std::chrono::seconds (10));
    wait_provider_evidence_contains ("ProfileReq", "rl-b1-later", std::chrono::seconds (10));
    std::cout << "scenario RL-B1 client passed\n";
}

} // namespace zlink::framework::e2e::resilience_lifecycle::client
