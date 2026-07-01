/* SPDX-License-Identifier: MPL-2.0 */

#pragma once

#include "../Support/client_support.hpp"

#include <iostream>

namespace zlink::framework::e2e::resilience_lifecycle::client
{

inline void run_rl_a1_provider_restart_scenario (zlink::framework::channel_client_t &channels)
{
    auto first = channels.request (api_channel, profile_req_t{.value = "rl-a1-before-restart"})
                   .timeout (std::chrono::milliseconds (2000))
                   .async<profile_res_t> ();
    ensure (first.result ().has_value (), "RL-A1 initial request failed");
    ensure (first.result ().value ().provider_rid == "api-a"
              && first.result ().value ().instance_id == "api-a-v1",
            "RL-A1 initial provider mismatch");

    touch_file (env_or ("ZLINK_CPP_E2E_READY_FILE"));
    wait_for_file (env_or ("ZLINK_CPP_E2E_CONTINUE_FILE"));

    for (int index = 0; index < 20; ++index) {
        auto task = channels
                      .request (api_channel,
                                profile_req_t{.value = "rl-a1-after-restart-"
                                                           + std::to_string (index)})
                      .timeout (std::chrono::milliseconds (2000))
                      .async<profile_res_t> ();
        ensure (task.result ().has_value (), "RL-A1 post-restart request failed");
        ensure (task.result ().value ().provider_rid == "api-a"
                  && task.result ().value ().instance_id == "api-a-v2",
                "RL-A1 did not switch to replacement provider");
    }
    std::cout << "scenario RL-A1 client passed\n";
}

} // namespace zlink::framework::e2e::resilience_lifecycle::client
