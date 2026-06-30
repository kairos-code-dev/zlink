/* SPDX-License-Identifier: MPL-2.0 */

#pragma once

#include "../Support/client_support.hpp"

#include <iostream>
#include <map>

namespace zlink::framework::e2e::registry_messaging::client
{

inline void run_rm_c3_multi_provider_distribution_scenario (
  zlink::framework::channel_client_t &channels)
{
    std::map<std::string, int> counts;
    for (int index = 0; index < 60; ++index) {
        auto task = channels
                      .request ("registry.messaging.api.manual.multi",
                                profile_request_t{.value = "multi-" + std::to_string (index)})
                      .timeout (std::chrono::milliseconds (2000))
                      .async<profile_reply_t> ();
        ensure (task.result ().has_value (), "RM-C3 request failed");
        ++counts[task.result ().value ().provider_rid];
    }
    ensure (counts["api-a"] > 0 && counts["api-b"] > 0,
            "RM-C3 did not distribute to both providers");
    ensure (counts["api-a"] + counts["api-b"] == 60, "RM-C3 count mismatch");
    std::cout << "scenario RM-C3 passed\n";
}

} // namespace zlink::framework::e2e::registry_messaging::client
