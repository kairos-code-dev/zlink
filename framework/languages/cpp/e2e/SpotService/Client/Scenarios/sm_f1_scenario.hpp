/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Support/spot_lifecycle_order_context.hpp"
#include "../../Shared/spot_service_contracts.hpp"

#include <zlink/framework.hpp>
#include <zlink/http_client.hpp>

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>

namespace zlink::framework::e2e::spot_service::client::scenarios
{

inline void run_sm_f1_scenario (const std::string &play_http_endpoint,
                                spot_lifecycle_order_context_t &context)
{
    if (play_http_endpoint.empty ()) {
        throw std::runtime_error ("ZLINK_CPP_E2E_PLAY_HTTP_ENDPOINT is required for SM-F1");
    }

    auto api = zlink::http_client::client_t::create ()
                 .base_url (play_http_endpoint)
                 .build ();
    auto raw =
      api.post ("/spot/state/request")
        .body (spot_state_route_req_t{.spot_rid = context.spot_rid,
                                      .state = state_req_t{.op = "add", .amount = 7}})
        .submit_raw ()
        .result ();
    if (!raw) {
        throw std::runtime_error (raw.error () ? raw.error ()->what ()
                                               : "SM-F1 state HTTP failed");
    }
    if (raw.value ().status >= 400) {
        throw std::runtime_error ("SM-F1 state HTTP status "
                                  + std::to_string (raw.value ().status) + ": "
                                  + raw.value ().body);
    }
    const auto state = nlohmann::json::parse (raw.value ().body).get<state_res_t> ();
    if (state.spot_rid != context.spot_rid || state.owner_node_rid != "play-a"
        || state.value != context.current_value + 7) {
        throw std::runtime_error ("SM-F1 state reply mismatch");
    }
    context.current_value = state.value;

    auto command =
      api.post ("/spot/state/command")
        .body (spot_state_command_route_req_t{.target_node_rid = "play-a",
                                              .spot_rid = context.spot_rid,
                                              .marker = "sm-f1-command"})
        .submit_raw ()
        .result ();
    if (!command) {
        throw std::runtime_error (command.error () ? command.error ()->what ()
                                                   : "SM-F1 command HTTP failed");
    }
    if (command.value ().status >= 400) {
        throw std::runtime_error ("SM-F1 command HTTP status "
                                  + std::to_string (command.value ().status) + ": "
                                  + command.value ().body);
    }
    const auto accepted =
      nlohmann::json::parse (command.value ().body).get<spot_state_command_route_res_t> ();
    if (!accepted.accepted) {
        throw std::runtime_error ("SM-F1 command was not accepted");
    }
}

inline void run_sm_f1_scenario (zlink::framework::route_client_t &routes,
                                const zlink::framework::spot_rid_t &remote_spot)
{
    auto direct_route_reply =
      routes
        .request (route_channel, zlink::routing_id_t::from (std::string ("play-b")), remote_spot,
                  direct_spot_req_t{"external-client", "route-direct"})
        .packet_name ("DirectSpotReq")
        .timeout (std::chrono::milliseconds (3000))
        .async<direct_spot_res_t> ()
        .result ();
    if (!direct_route_reply.has_value ()) {
        throw std::runtime_error ("SM-F1 direct spot request failed");
    }
    if (direct_route_reply.value ().owner_node_rid != "play-b"
        || direct_route_reply.value ().value != "route-direct:reply") {
        throw std::runtime_error ("SM-F1 direct spot reply mismatch");
    }

    auto direct_route_send =
      routes
        .send (route_channel, zlink::routing_id_t::from (std::string ("play-b")), remote_spot,
               direct_spot_msg_t{"external-client", "route-direct:command"})
        .packet_name ("DirectSpotMsg")
        .async ()
        .result ();
    if (!direct_route_send.has_value ()) {
        throw std::runtime_error ("SM-F1 direct spot send failed");
    }

    std::cout << "scenario SM-F1 passed\n";
}

} // namespace zlink::framework::e2e::spot_service::client::scenarios
