/* SPDX-License-Identifier: MPL-2.0 */

#pragma once

#include "../Support/client_support.hpp"

#include <iostream>
#include <map>

namespace zlink::framework::e2e::registry_messaging::client
{

inline void run_rm_c7_weighted_provider_scenario (zlink::framework::channel_client_t &channels)
{
    std::map<std::string, int> counts;
    constexpr int request_count = 100;
    for (int index = 0; index < request_count; ++index) {
        auto task = channels
                      .request (api_channel,
                                profile_request_t{.value = "weighted-" + std::to_string (index)})
                      .timeout (std::chrono::milliseconds (750))
                      .async<profile_reply_t> ();
        ensure (task.result ().has_value (), "RM-C7 weighted request failed");
        ++counts[task.result ().value ().provider_rid];
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
