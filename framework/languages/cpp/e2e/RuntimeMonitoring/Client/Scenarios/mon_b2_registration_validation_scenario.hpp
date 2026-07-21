/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#pragma once

#include "../Support/client_support.hpp"

#include <zlink/http_client.hpp>

#include <chrono>
#include <iostream>
#include <string>

namespace zlink::framework::e2e::runtime_monitoring::client
{

inline void run_mon_b2_registration_validation_scenario (
  const client_options_t &options)
{
    auto http = zlink::http_client::client_t::create ()
                  .base_url (options.service_url)
                  .timeout (std::chrono::seconds (40))
                  .build ();
    const auto observing = http.post ("/runtime/observe").async_raw ().result ();
    ensure (observing && observing.value ().status < 400,
            "MON-B2 public runtime observer did not start");
    const auto fast =
      http.post ("/admin/subject/create?spotRid=mon-b2-fast")
        .async_raw ()
        .result ();
    const auto slow =
      http.post ("/admin/slow-subject/create?spotRid=mon-b2-slow")
        .async_raw ()
        .result ();
    ensure (fast && fast.value ().status < 400 && slow
              && slow.value ().status < 400,
            "MON-B2 local subjects were not created");

    try {
        const auto baseline = fetch_evidence (options.service_url).size ();
        const auto raw =
          http.post ("/spot/local-drop")
            .body (multicast_publish_req_t{
              .marker = "mon-b2-local-drop",
              .payload_bytes = 1024 * 1024,
              .max_attempts = 100,
              .expected_local_dropped = 1})
            .async_raw ()
            .result ()
            .value ();
        ensure (raw.status < 400, "MON-B2 local-drop endpoint failed");
        const auto result =
          nlohmann::json::parse (raw.body).get<multicast_publish_res_t> ();
        ensure (result.snapshot_local == 2 && result.admitted_local == 1
                  && result.dropped_local == 1,
                "MON-B2 local target result was not 2/1/1");
        ensure (result.dropped_total > 0,
                "MON-B2 snapshot did not retain the dropped-target count");
        wait_for_new_evidence (
          options.service_url,
          "identifier=zlink.runtime.mesh_node.multicast_dropped",
          baseline, std::chrono::seconds (30));
        const auto evidence = fetch_evidence (options.service_url);
        bool exact = false;
        for (auto index = baseline; index < evidence.size (); ++index) {
            const auto &line = evidence[index];
            if (contains (
                  line,
                  "identifier=zlink.runtime.mesh_node.multicast_dropped")
                && contains (line, "local-snapshot=2")
                && contains (line, "local-admitted=1")
                && contains (line, "local-dropped=1")) {
                exact = true;
                break;
            }
        }
        ensure (exact,
                "MON-B2 typed runtime event did not carry local 2/1/1");
    }
    catch (...) {
        (void) http.post ("/admin/subject/close?spotRid=mon-b2-fast")
          .async_raw ()
          .result ();
        (void) http.post ("/admin/subject/close?spotRid=mon-b2-slow")
          .async_raw ()
          .result ();
        throw;
    }

    (void) http.post ("/admin/subject/close?spotRid=mon-b2-fast")
      .async_raw ()
      .result ();
    (void) http.post ("/admin/subject/close?spotRid=mon-b2-slow")
      .async_raw ()
      .result ();
    std::cout << "scenario MON-B2 passed\n";
}

} // namespace zlink::framework::e2e::runtime_monitoring::client
