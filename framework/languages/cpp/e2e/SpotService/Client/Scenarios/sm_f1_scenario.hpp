/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../Shared/spot_service_contracts.hpp"

#include <zlink/framework.hpp>

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>

namespace zlink::framework::e2e::spot_service::client::scenarios
{

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
               direct_spot_command_t{"external-client", "route-direct:command"})
        .packet_name ("DirectSpotCommand")
        .async ()
        .result ();
    if (!direct_route_send.has_value ()) {
        throw std::runtime_error ("SM-F1 direct spot send failed");
    }

    std::cout << "scenario SM-F1 passed\n";
}

} // namespace zlink::framework::e2e::spot_service::client::scenarios
