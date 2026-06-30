/* SPDX-License-Identifier: MPL-2.0 */

#pragma once

#include "../Support/client_support.hpp"

#include <iostream>

namespace zlink::framework::e2e::registry_messaging::client
{

inline void run_rm_c1_request_send_scenario (zlink::framework::channel_client_t &channels)
{
    auto request = channels.request (api_channel, profile_req_t{.value = "c1"})
                     .timeout (std::chrono::milliseconds (2000))
                     .async<profile_res_t> ();
    ensure (request.result ().has_value (), "RM-C1 request failed");
    ensure (request.result ().value ().value == "profile:c1", "RM-C1 reply mismatch");

    auto send = channels.send (api_channel, profile_msg_t{.command_id = "cmd-c1"}).async ();
    ensure (send.result ().has_value (), "RM-C1 send failed");
    std::this_thread::sleep_for (std::chrono::milliseconds (100));

    const auto evidence_a = fetch_evidence (env_or ("ZLINK_CPP_E2E_HTTP_A_ENDPOINT"));
    const auto evidence_b = fetch_evidence (env_or ("ZLINK_CPP_E2E_HTTP_B_ENDPOINT"));
    bool command_recorded = false;
    for (const auto &snapshot : {evidence_a, evidence_b}) {
        for (const auto &entry : snapshot.entries) {
            if (entry.marker == "ProfileMsg" && entry.value == "cmd-c1") {
                command_recorded = true;
            }
        }
    }
    ensure (command_recorded, "RM-C1 send handler evidence was not recorded");
    std::cout << "scenario RM-C1 passed\n";
}

} // namespace zlink::framework::e2e::registry_messaging::client
