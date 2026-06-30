/* SPDX-License-Identifier: MPL-2.0 */

#pragma once

#include "../Support/client_support.hpp"

#include <iostream>

namespace zlink::framework::e2e::registry_messaging::client
{

inline void run_rm_a4_same_rid_failover_scenario (zlink::framework::channel_client_t &channels)
{
    auto first = channels.request (api_channel, profile_request_t{.value = "failover-before"})
                   .timeout (std::chrono::milliseconds (2000))
                   .async<profile_reply_t> ();
    ensure (first.result ().has_value (), "RM-A4 initial request failed");
    ensure (first.result ().value ().provider_rid == "api-a"
              && first.result ().value ().instance_id == "api-a-v1",
            "RM-A4 initial provider mismatch");

    touch_file (env_or ("ZLINK_CPP_E2E_READY_FILE"));
    wait_for_file (env_or ("ZLINK_CPP_E2E_CONTINUE_FILE"));

    for (int index = 0; index < 20; ++index) {
        auto task = channels
                      .request (api_channel,
                                profile_request_t{.value = "failover-after-"
                                                           + std::to_string (index)})
                      .timeout (std::chrono::milliseconds (2000))
                      .async<profile_reply_t> ();
        ensure (task.result ().has_value (), "RM-A4 post-failover request failed");
        ensure (task.result ().value ().provider_rid == "api-a"
                  && task.result ().value ().instance_id == "api-a-v2",
                "RM-A4 did not switch to replacement provider");
    }
    std::cout << "scenario RM-A4 passed\n";
}

} // namespace zlink::framework::e2e::registry_messaging::client
