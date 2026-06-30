/* SPDX-License-Identifier: MPL-2.0 */

#pragma once

#include "../Support/client_support.hpp"

#include <iostream>

namespace zlink::framework::e2e::registry_messaging::client
{

inline void run_rm_a2_manual_endpoint_scenario (zlink::framework::channel_client_t &channels)
{
    auto request = channels
                     .request ("registry.messaging.api.manual",
                               profile_req_t{.value = "manual"})
                     .timeout (std::chrono::milliseconds (2000))
                     .async<profile_res_t> ();
    ensure (request.result ().has_value (), "RM-A2 request failed");
    ensure (request.result ().value ().provider_rid == "api-a",
            "RM-A2 did not use the requested provider endpoint");
    std::cout << "scenario RM-A2 passed\n";
}

} // namespace zlink::framework::e2e::registry_messaging::client
