/* SPDX-License-Identifier: MPL-2.0 */

#pragma once

#include "../Support/client_support.hpp"

#include <iostream>

namespace zlink::framework::e2e::resilience_lifecycle::client
{

inline void run_rl_b1_cancellation_cleanup_scenario (
  zlink::framework::channel_client_t &channels)
{
    auto slow = channels.request (api_channel, profile_req_t{.value = "slow"})
                  .timeout (std::chrono::milliseconds (100))
                  .async<profile_res_t> ();
    ensure (!slow.result ().has_value (), "RL-B1 slow request unexpectedly succeeded");
    ensure (slow.result ().error_kind () == zlink::framework::framework_error_kind_t::timeout,
            "RL-B1 slow request failed with unexpected public error kind");
    auto after = channels.request (api_channel, profile_req_t{.value = "rl-b1-after-timeout"})
                   .timeout (std::chrono::milliseconds (2000))
                   .async<profile_res_t> ();
    ensure (after.result ().has_value (), "RL-B1 post-timeout request failed");
    std::this_thread::sleep_for (std::chrono::milliseconds (1100));
    auto later = channels.request (api_channel, profile_req_t{.value = "rl-b1-later"})
                   .timeout (std::chrono::milliseconds (2000))
                   .async<profile_res_t> ();
    ensure (later.result ().has_value (), "RL-B1 later request failed");
    wait_provider_evidence_contains ("ProfileReq", "rl-b1-after-timeout",
                                     std::chrono::seconds (10));
    wait_provider_evidence_contains ("ProfileReq", "rl-b1-later", std::chrono::seconds (10));
    std::cout << "scenario RL-B1 client passed\n";
}

} // namespace zlink::framework::e2e::resilience_lifecycle::client
