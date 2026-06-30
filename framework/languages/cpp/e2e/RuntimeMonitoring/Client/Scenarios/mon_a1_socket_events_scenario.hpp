/* SPDX-License-Identifier: MPL-2.0 */

#pragma once

#include "../Support/client_support.hpp"
#include "../../Shared/runtime_monitoring_contracts.hpp"

#include <zlink/framework.hpp>
#include <zlink/http_client.hpp>

#include <chrono>
#include <iostream>
#include <string>

namespace zlink::framework::e2e::runtime_monitoring::client
{

inline void run_mon_a1_socket_events_scenario (zlink::framework::channel_client_t &channels)
{
    profile_reply_t reply;
    if (const auto trigger_url = env_or ("ZLINK_CPP_E2E_TRIGGER_URL"); !trigger_url.empty ()) {
        reply = post_profile_request (trigger_url, "/profile/request/service-a",
                                      profile_request_t{.value = "monitor", .marker = "mon-a1"});
    } else {
        auto request = channels.request (profile_channel,
                                         profile_request_t{.value = "monitor", .marker = "mon-a1"})
                         .timeout (std::chrono::milliseconds (3000))
                         .async<profile_reply_t> ();
        const auto &result = request.result ();
        ensure (result.has_value (),
                "MON-A1 request failed: "
                  + std::string (result.error () ? result.error ()->what () : "unknown"));
        reply = result.value ();
    }
    ensure (reply.value == "profile:monitor" && reply.marker == "mon-a1",
            "MON-A1 reply payload mismatch");

    auto http = zlink::http_client::client_t::create ()
                  .base_url (env_or ("ZLINK_CPP_E2E_SERVICE_URL"))
                  .timeout (std::chrono::milliseconds (1000))
                  .build ();
    auto drained = http.post ("/admin/server-weight?weight=0").submit_raw ().result ();
    ensure (drained && drained.value ().status < 400, "MON-A1 server weight admin call failed");

    const auto connected_entries = wait_evidence_contains (
      env_or ("ZLINK_CPP_E2E_SERVICE_URL"), "kind=Connected", std::chrono::milliseconds (10000));
    ensure (any_contains (connected_entries, "kind=Connected"),
            "MON-A1 connected evidence missing");
    ensure (any_contains (connected_entries, "remote=tcp://"),
            "MON-A1 connected remote address missing");

    const auto ready_entries = wait_evidence_contains (
      env_or ("ZLINK_CPP_E2E_SERVICE_URL"), "kind=ConnectionReady",
      std::chrono::milliseconds (10000));
    ensure (any_contains (ready_entries, "kind=ConnectionReady"),
            "MON-A1 socket event evidence missing");
    ensure (any_contains (ready_entries, "remote=tcp://"),
            "MON-A1 connection-ready remote address missing");

    const auto disconnected_entries = wait_evidence_contains (
      env_or ("ZLINK_CPP_E2E_SERVICE_URL"), "kind=Disconnected",
      std::chrono::milliseconds (10000));
    ensure (any_contains (disconnected_entries, "kind=Disconnected"),
            "MON-A1 disconnected evidence missing");
    ensure (any_contains (disconnected_entries, "remote=tcp://"),
            "MON-A1 disconnected remote address missing");
    auto restored = http.post ("/admin/server-weight?weight=100").submit_raw ().result ();
    ensure (restored && restored.value ().status < 400, "MON-A1 server weight restore failed");
    std::cout << "scenario MON-A1 passed\n";
}

} // namespace zlink::framework::e2e::runtime_monitoring::client
