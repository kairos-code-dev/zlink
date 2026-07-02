/* SPDX-License-Identifier: MPL-2.0 */

#pragma once

#include "../Support/client_support.hpp"

#include <iostream>

namespace zlink::framework::e2e::registry_messaging::client
{

inline void run_rm_a2_manual_endpoint_scenario ()
{
    const auto provider_a_url = env_or ("ZLINK_CPP_E2E_HTTP_A_ENDPOINT");
    auto reply = post_json<profile_req_t, profile_res_t> (
      provider_a_url, "/profile/manual", profile_req_t{.value = "rm-a2"});
    ensure (reply.value == "profile:rm-a2", "RM-A2 reply payload mismatch");
    ensure (reply.provider_rid == "api-a",
            "RM-A2 did not use the requested provider endpoint");
    wait_evidence_contains (provider_a_url, "ProfileReq", "rm-a2", std::chrono::seconds (10));
    std::cout << "scenario RM-A2 passed\n";
}

} // namespace zlink::framework::e2e::registry_messaging::client
