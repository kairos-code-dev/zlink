/* SPDX-License-Identifier: MPL-2.0 */

#pragma once

#include "../Support/client_support.hpp"

#include <iostream>

namespace zlink::framework::e2e::registry_messaging::client
{

inline void run_rl_a2_provider_endpoint_remap_scenario (
  zlink::framework::channel_client_t &channels)
{
    auto first = channels.request (api_channel, profile_req_t{.value = "rl-a2-before-remap"})
                   .timeout (std::chrono::milliseconds (2000))
                   .async<profile_res_t> ();
    ensure (first.result ().has_value (), "RL-A2 initial request failed");
    ensure (first.result ().value ().provider_rid == "api-a"
              && first.result ().value ().instance_id == "api-a-v1",
            "RL-A2 initial provider mismatch");

    touch_file (env_or ("ZLINK_CPP_E2E_READY_FILE"));
    wait_for_file (env_or ("ZLINK_CPP_E2E_CONTINUE_FILE"));

    for (int index = 0; index < 20; ++index) {
        auto task = channels
                      .request (api_channel,
                                profile_req_t{.value = "rl-a2-after-remap-"
                                                           + std::to_string (index)})
                      .timeout (std::chrono::milliseconds (2000))
                      .async<profile_res_t> ();
        ensure (task.result ().has_value (), "RL-A2 post-remap request failed");
        ensure (task.result ().value ().provider_rid == "api-a"
                  && task.result ().value ().instance_id == "api-a-v2",
                "RL-A2 did not switch to remapped provider endpoint");
    }
    std::cout << "scenario RL-A2 client passed\n";
}

} // namespace zlink::framework::e2e::registry_messaging::client
