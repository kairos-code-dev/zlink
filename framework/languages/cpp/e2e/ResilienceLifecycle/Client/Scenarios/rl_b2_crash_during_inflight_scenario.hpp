/* SPDX-License-Identifier: MPL-2.0 */

#pragma once

#include "../Support/resilience_request_support.hpp"

#include <chrono>
#include <iostream>

namespace zlink::framework::e2e::registry_messaging::client
{

inline void run_inflight_crash_scenario (zlink::framework::channel_client_t &channels)
{
    auto pending = channels
                     .request ("registry.messaging.api.manual.b",
                               profile_req_t{.value = "very-slow"})
                     .timeout (std::chrono::milliseconds (2500))
                     .async<profile_res_t> ();
    touch_file (env_or ("ZLINK_CPP_E2E_READY_FILE"));
    wait_for_file (env_or ("ZLINK_CPP_E2E_CONTINUE_FILE"));

    ensure (!pending.result ().has_value (),
            "in-flight request unexpectedly succeeded after provider crash");
    const auto follow_up =
      request_profile (channels, "registry.messaging.api.manual", "rl-b2-follow-up");
    ensure (follow_up.provider_rid == "api-a",
            "follow-up request did not use surviving provider");
    std::cout << "scenario RL-B2 passed\n";
}

} // namespace zlink::framework::e2e::registry_messaging::client
