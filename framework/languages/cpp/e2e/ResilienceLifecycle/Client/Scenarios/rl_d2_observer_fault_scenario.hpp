/* SPDX-License-Identifier: MPL-2.0 */

#pragma once

#include "../Support/client_support.hpp"
#include "../Support/resilience_request_support.hpp"

#include <chrono>
#include <iostream>
#include <thread>

namespace zlink::framework::e2e::resilience_lifecycle::client
{

inline void set_observer_fault_mode (const std::string &path)
{
    post_provider_admin (env_or ("ZLINK_CPP_E2E_HTTP_A_ENDPOINT"), path);
    post_provider_admin (env_or ("ZLINK_CPP_E2E_HTTP_B_ENDPOINT"), path);
}

inline void run_observer_fault_scenario (zlink::framework::channel_client_t &channels)
{
    set_observer_fault_mode ("/admin/fault/observer-throws");

    auto missing = channels.request (api_channel, profile_req_t{.value = "rl-d2-error"})
                     .packet_name ("MissingProfileReq")
                     .timeout (std::chrono::milliseconds (2000))
                     .async<profile_res_t> ();
    ensure (!missing.result ().has_value (), "RL-D2 missing handler request should fail");

    std::this_thread::sleep_for (std::chrono::milliseconds (200));
    ensure (any_provider_evidence_contains ("DispatchError", "handler_missing:reply_error"),
            "RL-D2 dispatch error evidence was not recorded");

    const auto follow_up = request_profile (channels, api_channel, "rl-d2-after");
    ensure (follow_up.value == "profile:rl-d2-after",
            "RL-D2 messaging did not continue after observer failure");

    set_observer_fault_mode ("/admin/fault/none");
    wait_provider_evidence_contains ("ProfileReq", "rl-d2-after",
                                     std::chrono::milliseconds (5000));
    std::cout << "scenario RL-D2 passed\n";
}

} // namespace zlink::framework::e2e::resilience_lifecycle::client
