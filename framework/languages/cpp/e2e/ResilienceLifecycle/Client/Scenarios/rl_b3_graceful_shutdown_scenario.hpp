/* SPDX-License-Identifier: MPL-2.0 */

#pragma once

#include "../Support/client_support.hpp"

#include <iostream>
#include <set>

namespace zlink::framework::e2e::registry_messaging::client
{

inline void run_rl_b3_graceful_shutdown_scenario (zlink::framework::channel_client_t &channels)
{
    std::set<std::string> before;
    for (int index = 0; index < 80 && before.size () < 2; ++index) {
        auto task = channels
                      .request (api_channel,
                                profile_req_t{.value = "rl-b3-before-shutdown-"
                                                           + std::to_string (index)})
                      .timeout (std::chrono::milliseconds (2000))
                      .async<profile_res_t> ();
        ensure (task.result ().has_value (), "RL-B3 initial request failed");
        before.insert (task.result ().value ().provider_rid);
    }
    ensure (before.contains ("api-a") && before.contains ("api-b"),
            "RL-B3 did not start with both providers");

    touch_file (env_or ("ZLINK_CPP_E2E_READY_FILE"));
    wait_for_file (env_or ("ZLINK_CPP_E2E_CONTINUE_FILE"));

    for (int index = 0; index < 20; ++index) {
        auto task = channels
                      .request (api_channel,
                                profile_req_t{.value = "rl-b3-after-shutdown-"
                                                           + std::to_string (index)})
                      .timeout (std::chrono::milliseconds (2000))
                      .async<profile_res_t> ();
        ensure (task.result ().has_value (), "RL-B3 post-shutdown request failed");
        ensure (task.result ().value ().provider_rid == "api-a",
                "RL-B3 routed to stopped provider after graceful shutdown");
    }
    std::cout << "scenario RL-B3 client passed\n";
}

} // namespace zlink::framework::e2e::registry_messaging::client
