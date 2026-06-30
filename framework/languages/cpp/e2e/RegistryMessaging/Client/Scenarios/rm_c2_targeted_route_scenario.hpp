/* SPDX-License-Identifier: MPL-2.0 */

#pragma once

#include "../Support/client_support.hpp"

#include <iostream>

namespace zlink::framework::e2e::registry_messaging::client
{

inline void run_rm_c2_targeted_route_scenario (zlink::framework::route_client_t &routes)
{
    auto to_b = routes
                  .request (route_channel, zlink::routing_id_t::from (std::string ("api-b")),
                            route_ping_t{.value = "target-b"})
                  .packet_name ("ScenarioRoutePing")
                  .timeout (std::chrono::milliseconds (2000))
                  .async<route_pong_t> ();
    ensure (to_b.result ().has_value (), "RM-C2 target request failed");
    ensure (to_b.result ().value ().target_rid == "api-b", "RM-C2 target rid mismatch");

    const auto evidence_a = fetch_evidence (env_or ("ZLINK_CPP_E2E_HTTP_A_ENDPOINT"));
    const auto evidence_b = fetch_evidence (env_or ("ZLINK_CPP_E2E_HTTP_B_ENDPOINT"));
    bool found_on_a = false;
    bool found_on_b = false;
    for (const auto &entry : evidence_a.entries) {
        if (entry.marker == "ScenarioRoutePing" && entry.value == "target-b") {
            found_on_a = true;
        }
    }
    for (const auto &entry : evidence_b.entries) {
        if (entry.marker == "ScenarioRoutePing" && entry.value == "target-b") {
            found_on_b = true;
        }
    }
    ensure (!found_on_a, "RM-C2 target request reached the wrong provider");
    ensure (found_on_b, "RM-C2 target provider evidence was not recorded");

    auto missing = routes
                     .request (route_channel,
                               zlink::routing_id_t::from (std::string ("api-missing")),
                               route_ping_t{.value = "missing"})
                     .packet_name ("ScenarioRoutePing")
                     .timeout (std::chrono::milliseconds (500))
                     .async<route_pong_t> ();
    ensure (!missing.result ().has_value (), "RM-C2 missing rid unexpectedly succeeded");
    std::cout << "scenario RM-C2 passed\n";
}

} // namespace zlink::framework::e2e::registry_messaging::client
