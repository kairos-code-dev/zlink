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

inline void run_sm_f3_scenario (zlink::framework::route_client_t &routes,
                                const zlink::framework::spot_rid_t &remote_spot)
{
    auto normal_route_after_spot =
      routes
        .request (route_channel, zlink::routing_id_t::from (std::string ("play-b")),
                  ensure_actor_req_t{"route-mixed-f3", "Route Mixed"})
        .packet_name ("EnsureActor")
        .timeout (std::chrono::milliseconds (3000))
        .async<ensure_actor_res_t> ()
        .result ();
    if (!normal_route_after_spot.has_value ()) {
        throw std::runtime_error ("SM-F3 normal route packet failed after spot route");
    }

    auto spot_route_after_normal =
      routes
        .request (route_channel, zlink::routing_id_t::from (std::string ("play-b")), remote_spot,
                  direct_spot_req_t{"external-client", "route-mixed"})
        .packet_name ("DirectSpotReq")
        .timeout (std::chrono::milliseconds (3000))
        .async<direct_spot_res_t> ()
        .result ();
    if (!spot_route_after_normal.has_value ()
        || spot_route_after_normal.value ().value != "route-mixed:reply") {
        throw std::runtime_error ("SM-F3 spot route packet failed after normal route");
    }

    std::cout << "scenario SM-F3 passed\n";
}

} // namespace zlink::framework::e2e::spot_service::client::scenarios
