/* SPDX-License-Identifier: MPL-2.0 */

#pragma once

#include "../Support/client_support.hpp"

#include <iostream>

namespace zlink::framework::e2e::resilience_lifecycle::client
{

inline void run_rl_d3_dispatch_error_evidence_scenario (
  zlink::framework::channel_client_t &channels)
{
    auto missing = channels.request (api_channel, profile_req_t{.value = "rl-d3-missing"})
                     .packet_name ("MissingProfileReq")
                     .timeout (std::chrono::milliseconds (2000))
                     .async<profile_res_t> ();
    ensure (!missing.result ().has_value (), "RL-D3 missing request unexpectedly succeeded");
    channels.send (api_channel, profile_msg_t{.command_id = "rl-d3-missing-send"})
      .packet_name ("MissingProfileMsg")
      .submit ();
    auto normal = channels.request (api_channel, profile_req_t{.value = "rl-d3-normal"})
                    .timeout (std::chrono::milliseconds (2000))
                    .async<profile_res_t> ();
    ensure (normal.result ().has_value (), "RL-D3 normal request after missing packet failed");

    std::this_thread::sleep_for (std::chrono::milliseconds (200));
    const auto evidence_a = fetch_evidence (env_or ("ZLINK_CPP_E2E_HTTP_A_ENDPOINT"));
    const auto evidence_b = fetch_evidence (env_or ("ZLINK_CPP_E2E_HTTP_B_ENDPOINT"));
    bool reply_error_recorded = false;
    bool drop_recorded = false;
    for (const auto &snapshot : {evidence_a, evidence_b}) {
        for (const auto &entry : snapshot.entries) {
            if (entry.marker != "DispatchError") {
                continue;
            }
            if (entry.value == "handler_missing:reply_error") {
                reply_error_recorded = true;
            }
            if (entry.value == "handler_missing:drop") {
                drop_recorded = true;
            }
        }
    }
    ensure (reply_error_recorded, "RL-D3 missing request dispatch evidence was not recorded");
    ensure (drop_recorded, "RL-D3 missing send dispatch evidence was not recorded");
    std::cout << "scenario RL-D3 client passed\n";
}

} // namespace zlink::framework::e2e::resilience_lifecycle::client
