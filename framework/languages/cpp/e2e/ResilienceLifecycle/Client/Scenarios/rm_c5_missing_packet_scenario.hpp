/* SPDX-License-Identifier: MPL-2.0 */

#pragma once

#include "../Support/client_support.hpp"

#include <iostream>

namespace zlink::framework::e2e::registry_messaging::client
{

inline void run_rm_c5_missing_packet_scenario (zlink::framework::channel_client_t &channels)
{
    auto missing = channels.request (api_channel, profile_req_t{.value = "missing"})
                     .packet_name ("MissingProfileReq")
                     .timeout (std::chrono::milliseconds (2000))
                     .async<profile_res_t> ();
    ensure (!missing.result ().has_value (), "RM-C5 missing request unexpectedly succeeded");
    auto dropped = channels.send (api_channel, profile_msg_t{.command_id = "missing-send"})
                     .packet_name ("MissingProfileMsg")
                     .async ();
    ensure (dropped.result ().has_value (), "RM-C5 missing send should complete as drop");
    auto normal = channels.request (api_channel, profile_req_t{.value = "normal"})
                    .timeout (std::chrono::milliseconds (2000))
                    .async<profile_res_t> ();
    ensure (normal.result ().has_value (), "RM-C5 normal request after missing packet failed");

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
    ensure (reply_error_recorded, "RM-C5 missing request dispatch evidence was not recorded");
    ensure (drop_recorded, "RM-C5 missing send dispatch evidence was not recorded");
    std::cout << "scenario RM-C5 passed\n";
}

} // namespace zlink::framework::e2e::registry_messaging::client
