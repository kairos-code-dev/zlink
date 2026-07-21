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
    auto http = zlink::http_client::client_t::create ()
                  .base_url (options.service_url)
                  .timeout (std::chrono::seconds (40))
                  .build ();
    const auto baseline = fetch_evidence (options.service_url).size ();
    const auto result =
      http.post ("/spot/publish-until")
        .body (multicast_publish_req_t{
          .marker = "mon-b1-remote-backpressure",
          .payload_bytes = 1024 * 1024,
          .max_attempts = 100,
          .expected_remote_dropped = 1})
        .async<multicast_publish_res_t> ()
        .result ()
        .value ()
        .body;

    ensure (result.status == "Backpressured",
            "MON-B1 publish status was not Backpressured");
    ensure (result.snapshot_remote == 2 && result.admitted_remote == 1
              && result.dropped_remote == 1,
            "MON-B1 remote target result was not 2/1/1");
    wait_for_new_evidence (
      options.service_url,
      "identifier=zlink.runtime.mesh_node.multicast_backpressured",
      baseline, std::chrono::seconds (30));
    const auto evidence = fetch_evidence (options.service_url);
    bool exact = false;
    for (auto index = baseline; index < evidence.size (); ++index) {
        const auto &line = evidence[index];
        if (contains (
              line,
              "identifier=zlink.runtime.mesh_node.multicast_backpressured")
            && contains (line, "remote-snapshot=2")
            && contains (line, "remote-admitted=1")
            && contains (line, "remote-dropped=1")) {
            exact = true;
            break;
        }
    }
    ensure (exact,
            "MON-B1 typed runtime event did not carry remote 2/1/1");
    const auto snapshot = runtime_snapshot (options.service_url);
    ensure (
      snapshot.at ("multicast").at ("backpressured").get<std::uint64_t> () > 0
        && snapshot.at ("multicast")
             .at ("remoteSnapshotCount")
             .get<std::uint64_t> ()
          == 2
        && snapshot.at ("multicast")
             .at ("remoteAdmittedCount")
             .get<std::uint64_t> ()
             == 1
        && snapshot.at ("multicast")
             .at ("remoteDroppedCount")
             .get<std::uint64_t> ()
             == 1,
      "MON-B1 follow-up snapshot lost remote 2/1/1");
    std::cout << "scenario MON-B1 passed\n";
}

} // namespace zlink::framework::e2e::runtime_monitoring::client
