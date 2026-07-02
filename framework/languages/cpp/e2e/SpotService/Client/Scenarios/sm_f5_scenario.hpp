/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../Shared/spot_service_contracts.hpp"

#include <zlink/http_client.hpp>

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>

namespace zlink::framework::e2e::spot_service::client::scenarios
{

inline void run_sm_f5_scenario (const std::string &play_http_endpoint,
                                const std::string &play_b_http_endpoint,
                                const std::string &remote_spot)
{
    auto play = zlink::http_client::client_t::create ()
                  .base_url (play_http_endpoint)
                  .timeout (std::chrono::milliseconds (3000))
                  .build ();
    auto normal_route_after_negative =
      play.post ("/channel/control-ping")
        .body (channel_control_ping_req_t{.target_node_rid = "play-b",
                                          .value = "route-survived-f5"})
        .submit_raw ()
        .result ();
    if (!normal_route_after_negative || normal_route_after_negative.value ().status >= 400) {
        throw std::runtime_error ("SM-F5 normal route packet failed after spot route negative");
    }

    auto play_b = zlink::http_client::client_t::create ()
                    .base_url (play_http_endpoint)
                    .timeout (std::chrono::milliseconds (3000))
                    .build ();
    auto raw =
      play_b.post ("/spot/direct")
        .body (direct_spot_route_req_t{.target_node_rid = "play-b",
                                       .spot_rid = remote_spot,
                                       .value = "route-survived-f5",
                                       .source_actor_id = "external-client"})
        .submit_raw ()
        .result ();
    if (!raw || raw.value ().status >= 400) {
        throw std::runtime_error ("SM-F5 spot route packet failed after route negative");
    }
    const auto spot_route_after_negative =
      nlohmann::json::parse (raw.value ().body).get<direct_spot_res_t> ();
    if (spot_route_after_negative.value != "route-survived-f5:reply") {
        throw std::runtime_error ("SM-F5 spot route packet failed after route negative");
    }

    std::cout << "scenario SM-F5 passed\n";
}

} // namespace zlink::framework::e2e::spot_service::client::scenarios
