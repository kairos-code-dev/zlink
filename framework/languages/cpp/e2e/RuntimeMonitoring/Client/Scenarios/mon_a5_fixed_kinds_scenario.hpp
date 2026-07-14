/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#pragma once

#include "../Support/client_support.hpp"

#include <zlink/http_client.hpp>

#include <chrono>
#include <iostream>

namespace zlink::framework::e2e::runtime_monitoring::client
{

inline void run_mon_a5_fixed_kinds_scenario (const client_options_t &options)
{
    auto service = zlink::http_client::client_t::create ()
                     .base_url (options.service_url)
                     .timeout (std::chrono::milliseconds (1000))
                     .build ();
    // The stopping timer belongs to a user Spot, so this scenario creates the
    // public application resource whose failure event it observes.
    auto created = service.post ("/spot/create").submit_raw ().result ();
    ensure (created && created.value ().status < 400, "MON-A5 spot create call failed");

    if (!options.trigger_url.empty ()) {
        auto trigger = zlink::http_client::client_t::create ()
                         .base_url (options.trigger_url)
                         .timeout (std::chrono::milliseconds (3000))
                         .build ();
        auto handshake = trigger.post ("/socket/handshake-failure").submit_raw ().result ();
        ensure (handshake && handshake.value ().status < 400,
                "MON-A5 handshake failure trigger failed");
    }

    const auto socket_entries = wait_evidence_contains (
      options.service_url,
      "monitor-socket|source=monitor.profile|kind=HandshakeFailed",
      std::chrono::milliseconds (10000));
    ensure (any_contains (socket_entries, "kind=HandshakeFailed"),
            "MON-A5 handshake failure evidence missing");

    const auto location_entries = wait_evidence_contains (
      options.service_url,
      "monitor-location|source=location-runtime|kind=StatusChanged",
      std::chrono::milliseconds (10000));
    ensure (any_contains (location_entries, "kind=StatusChanged"),
            "MON-A5 location status evidence missing");

    const auto spot_status_entries = wait_evidence_contains (
      options.service_url,
      "monitor-spot|source=monitor.spot|node=monitor.spot|kind=StatusChanged",
      std::chrono::milliseconds (10000));
    ensure (any_contains (spot_status_entries, "kind=StatusChanged"),
            "MON-A5 spot status evidence missing");

    const auto service_entries = wait_evidence_contains (
      options.service_url,
      "monitor-spot|source=monitor.spot|node=monitor.spot|kind=TimerStoppedAfterUnhandledException",
      std::chrono::milliseconds (10000));
    ensure (any_contains (service_entries, "timer=stopping"),
            "MON-A5 stopped timer evidence missing");
    std::cout << "scenario MON-A5 passed\n";
}

} // namespace zlink::framework::e2e::runtime_monitoring::client
