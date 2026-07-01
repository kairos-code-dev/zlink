/* SPDX-License-Identifier: MPL-2.0 */

#pragma once

#include "../Support/resilience_request_support.hpp"

#include <chrono>
#include <iostream>
#include <string>

namespace zlink::framework::e2e::resilience_lifecycle::client
{

inline void run_resilience_stress_scenario (zlink::framework::channel_client_t &channels)
{
    for (int index = 0; index < 40; ++index) {
        const auto reply =
          request_profile (channels, api_channel, "rl-d1-request-" + std::to_string (index));
        ensure (!reply.provider_rid.empty (), "stress request returned empty provider");
        channels
          .send (api_channel,
                 profile_msg_t{.command_id = "rl-d5-command-" + std::to_string (index)})
          .submit ();
    }

    auto missing = channels.request (api_channel, profile_req_t{.value = "rl-d4-missing"})
                     .packet_name ("MissingProfileReq")
                     .timeout (std::chrono::milliseconds (1000))
                     .async<profile_res_t> ();
    ensure (!missing.result ().has_value (),
            "missing request handler unexpectedly returned a typed reply");
    std::cout << "scenario RL-D1 passed\n";
    std::cout << "scenario RL-D4 passed\n";
    std::cout << "scenario RL-D5 passed\n";
}

} // namespace zlink::framework::e2e::resilience_lifecycle::client
