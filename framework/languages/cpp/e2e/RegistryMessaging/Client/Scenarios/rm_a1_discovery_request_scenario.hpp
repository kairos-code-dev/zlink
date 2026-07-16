/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#pragma once

#include "../Support/client_support.hpp"

#include <iostream>
#include <set>

namespace zlink::framework::e2e::registry_messaging::client
{

inline void run_rm_a1_discovery_request_scenario (const client_options_t &options)
{
    const auto provider_a_url = options.http_a_endpoint;
    const auto provider_b_url = options.http_b_endpoint;
    const auto reply = post_json<profile_req_t, profile_res_t> (
      provider_a_url, "/profile/request", profile_req_t{.value = "rm-a1"});
    ensure (reply.value == "profile:rm-a1", "RM-A1 reply payload mismatch");
    ensure (reply.provider_rid == "api-a" || reply.provider_rid == "api-b",
            "RM-A1 provider rid was not api-a/api-b");

    auto location_client = zlink::http_client::client_t::create ()
                             .base_url (provider_a_url)
                             .timeout (std::chrono::milliseconds (1000))
                             .build ();
    int ready_providers = 0;
    for (int attempt = 0; attempt < 40 && ready_providers < 2; ++attempt) {
        ready_providers = 0;
        const auto peers = location_client.get ("/locations/peers").async<nlohmann::json> ().result ().value ().body;
        std::set<std::string> provider_rids;
        for (const auto &entry : peers) {
            if (entry.value ("mesh_name", "") == api_channel
                && entry.value ("role", "") == "router"
                && !entry.value ("endpoint", "").empty ()) {
                provider_rids.insert (entry.value ("node_rid", ""));
            }
        }
        for (const auto &rid : provider_rids) {
            if (rid == "api-a" || rid == "api-b") {
                ++ready_providers;
            }
        }
        if (ready_providers < 2) {
            std::this_thread::sleep_for (std::chrono::milliseconds (100));
        }
    }
    ensure (ready_providers >= 2, "RM-A1 expected two live api providers in location store");
    if (reply.provider_rid == "api-a") {
        wait_evidence_contains (provider_a_url, "ProfileReq", "rm-a1", std::chrono::seconds (10));
    } else {
        wait_evidence_contains (provider_b_url, "ProfileReq", "rm-a1", std::chrono::seconds (10));
    }
    std::cout << "scenario RM-A1 passed\n";
}

} // namespace zlink::framework::e2e::registry_messaging::client
