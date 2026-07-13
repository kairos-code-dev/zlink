/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include "../../Shared/spot_service_contracts.hpp"

#include <zlink/http_client.hpp>

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>

namespace zlink::framework::e2e::spot_service::client::scenarios
{

inline void run_sm_f4_scenario (const std::string &play_http_endpoint,
                                const std::string &remote_spot)
{
    auto api = zlink::http_client::client_t::create ()
                 .base_url (play_http_endpoint)
                 .timeout (std::chrono::milliseconds (3000))
                 .build ();
    auto missing_spot_route =
      api.post ("/spot/direct")
        .body (direct_spot_route_req_t{.target_node_rid = "play-b",
                                       .spot_rid = "user:play-b:missing-spot-route",
                                       .value = "missing-route",
                                       .source_actor_id = "external-client"})
        .submit_raw ()
        .result ();
    if (missing_spot_route && missing_spot_route.value ().status < 400) {
        throw std::runtime_error ("SM-F4 missing target spot route unexpectedly succeeded");
    }

    auto raw =
      api.post ("/spot/direct")
        .body (direct_spot_route_req_t{.target_node_rid = "play-b",
                                       .spot_rid = remote_spot,
                                       .value = "route-recovery",
                                       .source_actor_id = "external-client"})
        .submit_raw ()
        .result ();
    if (!raw || raw.value ().status >= 400) {
        throw std::runtime_error ("SM-F4 recovery spot route request failed");
    }
    const auto recovery_after_negative =
      nlohmann::json::parse (raw.value ().body).get<direct_spot_res_t> ();
    if (recovery_after_negative.value != "route-recovery:reply") {
        throw std::runtime_error ("SM-F4 recovery spot route request failed");
    }

    std::cout << "scenario SM-F4 passed\n";
}

} // namespace zlink::framework::e2e::spot_service::client::scenarios
