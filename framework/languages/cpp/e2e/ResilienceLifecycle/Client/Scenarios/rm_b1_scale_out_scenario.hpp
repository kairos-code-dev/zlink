/* SPDX-License-Identifier: MPL-2.0 */

#pragma once

#include "../Support/client_support.hpp"

#include <iostream>
#include <set>

namespace zlink::framework::e2e::registry_messaging::client
{

inline void run_rm_b1_scale_out_scenario (zlink::framework::channel_client_t &channels)
{
    for (int index = 0; index < 5; ++index) {
        auto task = channels
                      .request (api_channel,
                                profile_req_t{.value = "scale-out-before-"
                                                           + std::to_string (index)})
                      .timeout (std::chrono::milliseconds (2000))
                      .async<profile_res_t> ();
        ensure (task.result ().has_value (), "RM-B1 initial request failed");
        ensure (task.result ().value ().provider_rid == "api-a",
                "RM-B1 initial traffic should only use api-a");
    }

    touch_file (env_or ("ZLINK_CPP_E2E_READY_FILE"));
    wait_for_file (env_or ("ZLINK_CPP_E2E_CONTINUE_FILE"));

    std::set<std::string> providers;
    for (int index = 0; index < 80 && providers.size () < 2; ++index) {
        auto task = channels
                      .request (api_channel,
                                profile_req_t{.value = "scale-out-after-"
                                                           + std::to_string (index)})
                      .timeout (std::chrono::milliseconds (2000))
                      .async<profile_res_t> ();
        ensure (task.result ().has_value (), "RM-B1 post-scale request failed");
        providers.insert (task.result ().value ().provider_rid);
    }
    ensure (providers.contains ("api-a") && providers.contains ("api-b"),
            "RM-B1 did not route to both providers after scale-out");
    std::cout << "scenario RM-B1 passed\n";
}

} // namespace zlink::framework::e2e::registry_messaging::client
