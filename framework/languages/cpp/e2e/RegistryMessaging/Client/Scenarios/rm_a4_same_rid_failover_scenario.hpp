/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#pragma once

#include "../Support/client_support.hpp"

#include <iostream>

namespace zlink::framework::e2e::registry_messaging::client
{

inline void run_rm_a4_same_rid_failover_scenario ()
{
    const auto provider_a_url = env_or ("ZLINK_CPP_E2E_HTTP_A_ENDPOINT");
    const auto replacement_provider_url =
      env_or ("ZLINK_CPP_E2E_HTTP_A2_ENDPOINT", provider_a_url);
    auto first = post_json<profile_req_t, profile_res_t> (
      provider_a_url, "/profile/request", profile_req_t{.value = "failover-before"});
    ensure (first.provider_rid == "api-a" && first.instance_id == "api-a-v1",
            "RM-A4 initial provider mismatch");

    touch_file (env_or ("ZLINK_CPP_E2E_READY_FILE"));
    wait_for_file (env_or ("ZLINK_CPP_E2E_CONTINUE_FILE"));

    for (int index = 0; index < 20; ++index) {
        auto reply = post_json<profile_req_t, profile_res_t> (
          replacement_provider_url, "/profile/request",
          profile_req_t{.value = "failover-after-" + std::to_string (index)});
        ensure (reply.provider_rid == "api-a" && reply.instance_id == "api-a-v2",
                "RM-A4 did not switch to replacement provider");
    }
    std::cout << "scenario RM-A4 passed\n";
}

} // namespace zlink::framework::e2e::registry_messaging::client
