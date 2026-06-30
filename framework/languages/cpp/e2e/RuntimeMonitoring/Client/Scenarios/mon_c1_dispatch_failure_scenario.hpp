/* SPDX-License-Identifier: MPL-2.0 */

#pragma once

#include "../Support/client_support.hpp"
#include "../../Shared/runtime_monitoring_contracts.hpp"
#include "mon_a1_socket_events_scenario.hpp"

#include <zlink/framework.hpp>
#include <zlink/http_client.hpp>

#include <chrono>
#include <iostream>

namespace zlink::framework::e2e::runtime_monitoring::client
{

inline void run_mon_c1_dispatch_failure_scenario (zlink::framework::channel_client_t &channels)
{
    auto throwing_http = zlink::http_client::client_t::create ()
                           .base_url (env_or ("ZLINK_CPP_E2E_THROW_SERVICE_URL"))
                           .timeout (std::chrono::milliseconds (1000))
                           .build ();
    auto changed = throwing_http.post ("/admin/server-weight?weight=0").submit_raw ().result ();
    ensure (changed && changed.value ().status < 400, "MON-C1 throwing service admin call failed");

    const auto evidence = wait_evidence_contains (
      env_or ("ZLINK_CPP_E2E_THROW_SERVICE_URL"), "monitor-throw|",
      std::chrono::milliseconds (10000));
    ensure (any_contains (evidence, "monitor-socket|"), "MON-C1 socket evidence missing");
    ensure (any_contains (evidence, "monitor-throw|"), "MON-C1 throwing monitor evidence missing");
    if (const auto trigger_url = env_or ("ZLINK_CPP_E2E_TRIGGER_URL"); !trigger_url.empty ()) {
        const auto log_lines = wait_log_contains (
          trigger_url, "/logs/throw-stderr/wait", "monitoring-event-dispatch",
          std::chrono::milliseconds (10000));
        ensure (any_contains (log_lines, "monitoring dispatch failure for e2e"),
                "MON-C1 throwing stderr marker missing");
    }

    profile_reply_t reply;
    if (const auto trigger_url = env_or ("ZLINK_CPP_E2E_TRIGGER_URL"); !trigger_url.empty ()) {
        reply = post_profile_request (
          trigger_url, "/profile/request",
          profile_request_t{.value = "throw", .marker = "mon-c1-recovery"});
    } else {
        auto request =
          channels.request (profile_channel,
                            profile_request_t{.value = "throw", .marker = "mon-c1-recovery"})
            .timeout (std::chrono::milliseconds (3000))
            .async<profile_reply_t> ();
        const auto &result = request.result ();
        ensure (result.has_value (),
                "MON-C1 recovery request failed: "
                  + std::string (result.error () ? result.error ()->what () : "unknown"));
        reply = result.value ();
    }
    ensure (reply.value == "profile:throw", "MON-C1 messaging did not recover");
    std::cout << "scenario MON-C1 passed\n";
}

} // namespace zlink::framework::e2e::runtime_monitoring::client
