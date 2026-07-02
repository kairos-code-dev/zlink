/* SPDX-License-Identifier: MPL-2.0 */

#pragma once

#include "../Support/client_support.hpp"

#include <iostream>
#include <map>

namespace zlink::framework::e2e::registry_messaging::client
{

inline void run_rm_c7_weighted_provider_scenario ()
{
    const auto provider_a_url = env_or ("ZLINK_CPP_E2E_HTTP_A_ENDPOINT");
    std::map<std::string, int> counts;
    constexpr int request_count = 100;
    for (int index = 0; index < request_count; ++index) {
        auto reply = post_json<profile_req_t, profile_res_t> (
          provider_a_url, "/profile/request",
          profile_req_t{.value = "weighted-" + std::to_string (index)});
        ++counts[reply.provider_rid];
    }
    ensure (counts["api-a"] > 0 && counts["api-b"] > 0,
            "RM-C7 did not use both weighted providers");
    ensure (counts["api-a"] + counts["api-b"] == request_count, "RM-C7 count mismatch");
    std::cout << "scenario RM-C7 counts api-a=" << counts["api-a"]
              << " api-b=" << counts["api-b"] << "\n";
    ensure (counts["api-a"] >= counts["api-b"] + (request_count / 10),
            "RM-C7 did not clearly prefer the higher weight provider");
    std::cout << "scenario RM-C7 passed\n";
}

} // namespace zlink::framework::e2e::registry_messaging::client
