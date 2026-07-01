/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Support/client_support.hpp"

#include <zlink/http_client.hpp>

#include <algorithm>
#include <iostream>
#include <vector>

namespace zlink::framework::e2e::registration_codec::client
{

inline void run_filter_ordering_scenario (zlink::framework::channel_client_t &channels,
                                          const std::string &http_endpoint)
{
    auto request =
      channels.request (api_channel, filter_order_req_t{.value = "filter-order"})
        .timeout (std::chrono::milliseconds (2000))
        .async<filter_order_res_t> ();
    ensure (request.result ().has_value (), "RC-A5 request failed");
    const auto reply = request.result ().value ();
    const std::vector<std::string> expected{
      "first-before", "second-before", "handler", "second-after", "first-after"};
    ensure (reply.value == "filter-order", "RC-A5 reply value mismatch");
    ensure (reply.order == std::vector<std::string>{"first-before", "second-before", "handler"},
            "RC-A5 handler-visible filter order mismatch");

    auto http = zlink::http_client::client_t::create ().base_url (http_endpoint);
    auto evidence = http.get ("/evidence").submit_raw ().result ();
    ensure (evidence.has_value (), "RC-A5 evidence request failed");
    const auto snapshot = nlohmann::json::parse (evidence.value ().body).get<evidence_snapshot_t> ();
    const auto expected_json = nlohmann::json (expected).dump ();
    const auto found =
      std::any_of (snapshot.entries.begin (), snapshot.entries.end (),
                   [&] (const evidence_entry_t &entry) {
                       return entry.marker == "RC-A5" && entry.value == expected_json;
                   });
    ensure (found, "RC-A5 filter order evidence missing");
    std::cout << "scenario RC-A5 passed\n";
}

} // namespace zlink::framework::e2e::registration_codec::client
