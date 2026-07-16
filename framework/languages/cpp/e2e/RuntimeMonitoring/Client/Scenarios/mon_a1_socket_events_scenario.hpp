/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#pragma once

#include "../Support/client_support.hpp"
#include "../../Shared/runtime_monitoring_contracts.hpp"

#include <zlink/http_client.hpp>

#include <chrono>
#include <iostream>
#include <string>

namespace zlink::framework::e2e::runtime_monitoring::client
{

inline void run_mon_a1_socket_events_scenario (const client_options_t &options)
{
    auto reply = post_profile_request (options.trigger_url, "/profile/request/service-a",
                                       profile_req_t{.value = "monitor", .marker = "mon-a1"});
    ensure (reply.value == "profile:monitor" && reply.marker == "mon-a1",
            "MON-A1 reply payload mismatch");

    auto http = zlink::http_client::client_t::create ()
                  .base_url (options.service_url)
                  .timeout (std::chrono::milliseconds (1000))
                  .build ();
    auto service_entries = fetch_evidence (options.service_url);
    auto drained = http.post ("/admin/server-weight?weight=0").async_raw ().result ();
    ensure (drained && drained.value ().status < 400, "MON-A1 server weight admin call failed");
    wait_for_new_evidence (options.service_url, "kind=PeerAdmissionChanged",
                           service_entries.size ());

    const auto connected_entries = wait_evidence_contains (options.service_url, "kind=Connected",
                                                           std::chrono::milliseconds (10000));
    ensure (any_contains (connected_entries, "kind=Connected"),
            "MON-A1 connected evidence missing");
    ensure (any_contains (connected_entries, "remote=tcp://"),
            "MON-A1 connected remote address missing");

    const auto ready_entries = wait_evidence_contains (options.service_url, "kind=ConnectionReady",
                                                       std::chrono::milliseconds (10000));
    ensure (any_contains (ready_entries, "kind=ConnectionReady"),
            "MON-A1 socket event evidence missing");
    ensure (any_contains (ready_entries, "remote=tcp://"),
            "MON-A1 connection-ready remote address missing");

    const auto disconnected_entries = wait_evidence_contains (
      options.service_url, "kind=Disconnected", std::chrono::milliseconds (10000));
    ensure (any_contains (disconnected_entries, "kind=Disconnected"),
            "MON-A1 disconnected evidence missing");
    ensure (any_contains (disconnected_entries, "remote=tcp://"),
            "MON-A1 disconnected remote address missing");
    service_entries = fetch_evidence (options.service_url);
    auto restored = http.post ("/admin/server-weight?weight=100").async_raw ().result ();
    ensure (restored && restored.value ().status < 400, "MON-A1 server weight restore failed");
    wait_for_new_evidence (options.service_url, "kind=PeerAdmissionChanged",
                           service_entries.size ());
    std::cout << "scenario MON-A1 passed\n";
}

} // namespace zlink::framework::e2e::runtime_monitoring::client
