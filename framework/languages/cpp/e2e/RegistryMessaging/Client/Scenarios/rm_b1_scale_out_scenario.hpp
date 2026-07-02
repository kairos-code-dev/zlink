/* SPDX-License-Identifier: MPL-2.0 */

#pragma once

#include "../Support/client_support.hpp"

#include <iostream>
#include <set>

namespace zlink::framework::e2e::registry_messaging::client
{

inline void run_rm_b1_scale_out_scenario ()
{
    const auto provider_a_url = env_or ("ZLINK_CPP_E2E_HTTP_A_ENDPOINT");
    for (int index = 0; index < 5; ++index) {
        auto reply = post_json<profile_req_t, profile_res_t> (
          provider_a_url, "/profile/request",
          profile_req_t{.value = "scale-out-before-" + std::to_string (index)});
        ensure (reply.provider_rid == "api-a",
                "RM-B1 initial traffic should only use api-a");
    }

    touch_file (env_or ("ZLINK_CPP_E2E_READY_FILE"));
    wait_for_file (env_or ("ZLINK_CPP_E2E_CONTINUE_FILE"));

    std::set<std::string> providers;
    for (int index = 0; index < 80 && providers.size () < 2; ++index) {
        auto reply = post_json<profile_req_t, profile_res_t> (
          provider_a_url, "/profile/request",
          profile_req_t{.value = "scale-out-after-" + std::to_string (index)});
        providers.insert (reply.provider_rid);
    }
    ensure (providers.contains ("api-a") && providers.contains ("api-b"),
            "RM-B1 did not route to both providers after scale-out");
    std::cout << "scenario RM-B1 passed\n";
}

} // namespace zlink::framework::e2e::registry_messaging::client
