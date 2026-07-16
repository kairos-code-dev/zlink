/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#pragma once

#include "../Support/client_support.hpp"
#include "mon_a1_socket_events_scenario.hpp"

#include <zlink/http_client.hpp>

#include <chrono>
#include <iostream>

namespace zlink::framework::e2e::runtime_monitoring::client
{

inline void run_mon_b1_kind_filter_scenario (const client_options_t &options)
{
    if (!options.trigger_url.empty ()) {
        auto trigger = zlink::http_client::client_t::create ()
                         .base_url (options.trigger_url)
                         .timeout (std::chrono::milliseconds (3000))
                         .build ();
        auto request =
          trigger.post ("/profile/request/service-b")
            .body (profile_req_t{.value = "filter", .marker = "mon-b1"})
            .async<profile_res_t> ()
            .result ();
        ensure (request && request.value ().status < 400,
                "MON-B1 filtered service trigger request failed");
    }

    const auto entries = wait_evidence_contains (
      options.filtered_service_url,
      "monitor-socket|source=monitor.profile|kind=ConnectionReady",
      std::chrono::milliseconds (10000));
    ensure (any_contains (entries, "kind=ConnectionReady"),
            "MON-B1 filtered socket evidence missing");
    for (const auto &entry : entries) {
        if (contains (entry, "monitor-socket|")) {
            ensure (contains (entry, "kind=ConnectionReady"),
                    "MON-B1 returned a socket event outside the configured kind filter");
        }
    }
    std::cout << "scenario MON-B1 passed\n";
}

} // namespace zlink::framework::e2e::runtime_monitoring::client
