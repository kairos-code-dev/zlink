/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#pragma once

#include "../Support/client_support.hpp"

#include <algorithm>
#include <chrono>
#include <iostream>

namespace zlink::framework::e2e::runtime_monitoring::client
{

inline void run_mon_a3_spot_events_scenario (const client_options_t &options)
{
    auto http = zlink::http_client::client_t::create ()
                  .base_url (options.service_url)
                  .timeout (std::chrono::milliseconds (1000))
                  .build ();

    auto created = http.post ("/spot/create").async_raw ().result ();
    ensure (created && created.value ().status < 400, "MON-A3 spot create call failed");

    const auto peer_entries = wait_evidence_contains (
      options.service_url,
      "monitor-spot|source=monitor.spot|node=monitor.spot|kind=PeersChanged",
      std::chrono::milliseconds (10000));
    ensure (std::any_of (peer_entries.begin (), peer_entries.end (), [] (const auto &entry) {
                return contains (entry, "kind=PeersChanged") && !contains (entry, "peers=0");
            }),
            "MON-A3 spot peer count evidence missing");

    auto timer_entries =
      wait_evidence_contains (options.service_url,
                              "monitor-spot|source=monitor.spot|node=monitor.spot|kind=TimerHandlerFailed",
                              std::chrono::milliseconds (10000));
    ensure (any_contains (
              timer_entries,
              "monitor-spot|source=monitor.spot|node=monitor.spot|kind=SubjectsChanged"),
            "MON-A3 spot subject evidence missing");
    ensure (any_contains (timer_entries, "subjects=1"),
            "MON-A3 spot subject count evidence missing");
    ensure (any_contains (timer_entries, "timer=failing"),
            "MON-A3 spot timer failure evidence missing");
    std::cout << "scenario MON-A3 passed\n";
}

} // namespace zlink::framework::e2e::runtime_monitoring::client
