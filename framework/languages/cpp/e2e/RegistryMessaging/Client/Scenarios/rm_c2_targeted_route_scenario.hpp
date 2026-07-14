/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#pragma once

#include "../Support/client_support.hpp"

#include <iostream>

namespace zlink::framework::e2e::registry_messaging::client
{

inline void run_rm_c2_targeted_route_scenario ()
{
    const auto provider_a_url = env_or ("ZLINK_CPP_E2E_HTTP_A_ENDPOINT");
    auto to_b = post_json<scenario_route_req_t, scenario_route_res_t> (
      provider_a_url, "/profile/route/request", scenario_route_req_t{.value = "target-b"});
    ensure (to_b.target_rid == "api-b", "RM-C2 target rid mismatch");

    const auto evidence_a = fetch_evidence (env_or ("ZLINK_CPP_E2E_HTTP_A_ENDPOINT"));
    const auto evidence_b = fetch_evidence (env_or ("ZLINK_CPP_E2E_HTTP_B_ENDPOINT"));
    bool found_on_a = false;
    bool found_on_b = false;
    for (const auto &entry : evidence_a.entries) {
        if (entry.marker == "ScenarioRouteReq" && entry.value == "target-b") {
            found_on_a = true;
        }
    }
    for (const auto &entry : evidence_b.entries) {
        if (entry.marker == "ScenarioRouteReq" && entry.value == "target-b") {
            found_on_b = true;
        }
    }
    ensure (!found_on_a, "RM-C2 target request reached the wrong provider");
    ensure (found_on_b, "RM-C2 target provider evidence was not recorded");

    auto missing = post_json<scenario_route_req_t, request_failure_res_t> (
      provider_a_url, "/profile/route/missing", scenario_route_req_t{.value = "missing"});
    ensure (missing.failed, "RM-C2 missing rid unexpectedly succeeded");
    ensure (missing.error_type == "RouteNotConnected",
            "RM-C2 missing rid error type mismatch: " + missing.error_type);
    std::cout << "scenario RM-C2 passed\n";
}

} // namespace zlink::framework::e2e::registry_messaging::client
