/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Support/client_support.hpp"

#include <iostream>
#include <vector>

namespace zlink::framework::e2e::registration_codec::client
{

inline void run_filter_ordering_scenario (zlink::framework::channel_client_t &channels)
{
    auto request =
      channels.request (api_channel, filter_order_req_t{.value = "filter-order"})
        .timeout (std::chrono::milliseconds (2000))
        .async<filter_order_reply_t> ();
    ensure (request.result ().has_value (), "RC-A5 request failed");
    const auto reply = request.result ().value ();
    const std::vector<std::string> expected{
      "first-before", "second-before", "handler", "second-after", "first-after"};
    ensure (reply.value == "filter-order", "RC-A5 reply value mismatch");
    ensure (reply.order == expected, "RC-A5 filter order mismatch");
    std::cout << "scenario RC-A5 passed\n";
}

} // namespace zlink::framework::e2e::registration_codec::client
