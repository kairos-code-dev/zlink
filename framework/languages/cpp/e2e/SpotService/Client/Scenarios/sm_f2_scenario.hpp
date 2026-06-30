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

inline void run_sm_f2_scenario (zlink::framework::route_client_t &routes,
                                const zlink::framework::spot_rid_t &remote_spot)
{
    auto state_route_reply =
      routes
        .request (route_channel, zlink::routing_id_t::from (std::string ("play-b")), remote_spot,
                  direct_spot_req_t{"external-client", "route-direct-f2"})
        .packet_name ("DirectSpotReq")
        .timeout (std::chrono::milliseconds (3000))
        .async<direct_spot_res_t> ()
        .result ();
    if (!state_route_reply.has_value ()) {
        throw std::runtime_error ("SM-F2 direct spot request failed");
    }
    if (state_route_reply.value ().owner_node_rid != "play-b"
        || state_route_reply.value ().value != "route-direct-f2:reply") {
        throw std::runtime_error ("SM-F2 direct spot reply mismatch");
    }

    auto command_route =
      routes
        .send (route_channel, zlink::routing_id_t::from (std::string ("play-b")), remote_spot,
               direct_spot_command_t{"external-client", "route-direct-f2:command"})
        .packet_name ("DirectSpotCommand")
        .async ()
        .result ();
    if (!command_route.has_value ()) {
        throw std::runtime_error ("SM-F2 direct spot command failed");
    }

    std::cout << "scenario SM-F2 passed\n";
}

} // namespace zlink::framework::e2e::spot_service::client::scenarios
