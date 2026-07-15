/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#pragma once

#include "../Support/client_support.hpp"
#include "mon_a1_socket_events_scenario.hpp"

#include <zlink/http_client.hpp>

#include <chrono>
#include <iostream>

namespace zlink::framework::e2e::runtime_monitoring::client
{

inline std::size_t mon_a4_find_after (const std::vector<std::string> &entries,
                                      const std::string &needle,
                                      std::size_t start)
{
    for (auto index = start; index < entries.size (); ++index) {
        if (contains (entries[index], needle)) {
            return index;
        }
    }
    throw std::runtime_error ("MON-A4 ordered evidence missing: " + needle);
}

inline void run_mon_a4_availability_transition_scenario (const client_options_t &options)
{
    ensure (!options.old_service_channel_endpoint.empty ()
              && !options.new_service_channel_endpoint.empty (),
            "MON-A4 runner did not provide failover endpoints");

    const auto trigger_entries = wait_evidence_contains (
      options.trigger_url, "kind=ConnectionReady|remote=" + options.new_service_channel_endpoint,
      std::chrono::milliseconds (10000));
    const auto disconnected = mon_a4_find_after (
      trigger_entries, "kind=Disconnected|remote=" + options.old_service_channel_endpoint, 0);
    const auto connected = mon_a4_find_after (
      trigger_entries, "kind=Connected|remote=" + options.new_service_channel_endpoint,
      disconnected + 1);
    mon_a4_find_after (trigger_entries,
                       "kind=ConnectionReady|remote=" + options.new_service_channel_endpoint,
                       connected + 1);

    const auto location_entries = wait_evidence_contains (
      options.filtered_service_url, "svc-a@" + options.new_service_channel_endpoint,
      std::chrono::milliseconds (10000));
    const auto old_route =
      mon_a4_find_after (location_entries, "svc-a@" + options.old_service_channel_endpoint, 0);
    mon_a4_find_after (location_entries, "svc-a@" + options.new_service_channel_endpoint,
                       old_route + 1);

    auto http = zlink::http_client::client_t::create ()
                  .base_url (options.service_url)
                  .timeout (std::chrono::milliseconds (1000))
                  .build ();

    auto service_entries = fetch_evidence (options.service_url);
    auto drained = http.post ("/admin/server-weight?weight=0").submit_raw ().result ();
    ensure (drained && drained.value ().status < 400, "MON-A4 drain admin call failed");
    wait_for_new_evidence (options.service_url, "kind=PeerAdmissionChanged",
                           service_entries.size ());

    service_entries = fetch_evidence (options.service_url);
    auto restored = http.post ("/admin/server-weight?weight=100").submit_raw ().result ();
    ensure (restored && restored.value ().status < 400, "MON-A4 restore admin call failed");
    wait_for_new_evidence (options.service_url, "kind=PeerAdmissionChanged",
                           service_entries.size ());
    std::cout << "scenario MON-A4 passed\n";
}

} // namespace zlink::framework::e2e::runtime_monitoring::client
