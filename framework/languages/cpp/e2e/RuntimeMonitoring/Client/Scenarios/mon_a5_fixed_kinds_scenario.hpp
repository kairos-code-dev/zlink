/* SPDX-License-Identifier: MPL-2.0 */

#pragma once

#include "../Support/client_support.hpp"

#include <zlink/http_client.hpp>

#include <chrono>
#include <iostream>

namespace zlink::framework::e2e::runtime_monitoring::client
{

inline void run_mon_a5_fixed_kinds_scenario ()
{
    if (const auto trigger_url = env_or ("ZLINK_CPP_E2E_TRIGGER_URL"); !trigger_url.empty ()) {
        auto trigger = zlink::http_client::client_t::create ()
                         .base_url (trigger_url)
                         .timeout (std::chrono::milliseconds (3000))
                         .build ();
        auto handshake = trigger.post ("/socket/handshake-failure").submit_raw ().result ();
        ensure (handshake && handshake.value ().status < 400,
                "MON-A5 handshake failure trigger failed");
    }

    const auto socket_entries = wait_evidence_contains (
      env_or ("ZLINK_CPP_E2E_SERVICE_URL"),
      "monitor-socket|source=monitor.profile|kind=HandshakeFailed",
      std::chrono::milliseconds (10000));
    ensure (any_contains (socket_entries, "kind=HandshakeFailed"),
            "MON-A5 handshake failure evidence missing");

    const auto registry_entries = wait_evidence_contains (
      env_or ("ZLINK_CPP_E2E_REGISTRY_URL"),
      "monitor-registry|source=registry|kind=StatusChanged",
      std::chrono::milliseconds (10000));
    ensure (any_contains (registry_entries, "kind=StatusChanged"),
            "MON-A5 registry status evidence missing");

    const auto spot_status_entries = wait_evidence_contains (
      env_or ("ZLINK_CPP_E2E_SERVICE_URL"),
      "monitor-spot|source=monitor.spot|node=monitor.spot|kind=StatusChanged",
      std::chrono::milliseconds (10000));
    ensure (any_contains (spot_status_entries, "kind=StatusChanged"),
            "MON-A5 spot status evidence missing");

    const auto service_entries = wait_evidence_contains (
      env_or ("ZLINK_CPP_E2E_SERVICE_URL"),
      "monitor-spot|source=monitor.spot|node=monitor.spot|kind=TimerStoppedAfterUnhandledException",
      std::chrono::milliseconds (10000));
    ensure (any_contains (service_entries, "timer=stopping"),
            "MON-A5 stopped timer evidence missing");
    std::cout << "scenario MON-A5 passed\n";
}

} // namespace zlink::framework::e2e::runtime_monitoring::client
