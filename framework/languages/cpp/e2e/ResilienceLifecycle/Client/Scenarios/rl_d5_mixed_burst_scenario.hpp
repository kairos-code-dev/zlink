/* SPDX-License-Identifier: MPL-2.0 */

#pragma once

#include "../Support/resilience_request_support.hpp"

#include <zlink/http_client.hpp>

#include <chrono>
#include <iostream>
#include <string>

namespace zlink::framework::e2e::resilience_lifecycle::client
{

inline void run_rl_d5_mixed_burst_scenario ()
{
    auto consumer = zlink::http_client::client_t::create ()
                      .base_url (env_or ("ZLINK_CPP_E2E_HTTP_CONSUMER_ENDPOINT"))
                      .timeout (std::chrono::milliseconds (10000))
                      .build ();

    for (int index = 0; index < 60; ++index) {
        const auto marker = "rl-d5-req-" + std::to_string (index);
        const auto reply = consumer.post ("/profile/request")
                             .body (profile_req_t{.value = "fast", .marker = marker})
                             .fetch<profile_res_t> ();
        ensure (reply.value == "profile:fast", "RL-D5 request burst reply was invalid");
    }
    for (int index = 0; index < 60; ++index) {
        const auto marker = "rl-d5-cmd-" + std::to_string (index);
        const auto reply = consumer.post ("/profile/command")
                             .body (profile_msg_t{.command_id = marker, .marker = marker})
                             .fetch<operation_status_t> ();
        ensure (reply.status == "sent", "RL-D5 command burst reply was invalid");
    }

    wait_provider_evidence_contains ("ProfileReq", "rl-d5-req-0",
                                     std::chrono::milliseconds (5000));
    wait_provider_evidence_contains ("ProfileMsg", "rl-d5-cmd-0",
                                     std::chrono::milliseconds (5000));
    std::cout << "scenario RL-D5 passed\n";
}

} // namespace zlink::framework::e2e::resilience_lifecycle::client
