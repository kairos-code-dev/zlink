/* SPDX-License-Identifier: MPL-2.0 */

#pragma once

#include "../Support/client_support.hpp"

#include <iostream>
#include <set>

namespace zlink::framework::e2e::registry_messaging::client
{

inline void run_rm_b2_scale_in_scenario ()
{
    const auto provider_a_url = env_or ("ZLINK_CPP_E2E_HTTP_A_ENDPOINT");
    std::set<std::string> before;
    for (int index = 0; index < 80 && before.size () < 2; ++index) {
        auto reply = post_json<profile_req_t, profile_res_t> (
          provider_a_url, "/profile/request",
          profile_req_t{.value = "scale-in-before-" + std::to_string (index)});
        before.insert (reply.provider_rid);
    }
    ensure (before.contains ("api-a") && before.contains ("api-b"),
            "RM-B2 did not start with both providers");

    touch_file (env_or ("ZLINK_CPP_E2E_READY_FILE"));
    wait_for_file (env_or ("ZLINK_CPP_E2E_CONTINUE_FILE"));

    for (int index = 0; index < 20; ++index) {
        auto reply = post_json<profile_req_t, profile_res_t> (
          provider_a_url, "/profile/request",
          profile_req_t{.value = "scale-in-after-" + std::to_string (index)});
        ensure (reply.provider_rid == "api-a",
                "RM-B2 routed to removed provider after scale-in");
    }
    std::cout << "scenario RM-B2 passed\n";
}

} // namespace zlink::framework::e2e::registry_messaging::client
