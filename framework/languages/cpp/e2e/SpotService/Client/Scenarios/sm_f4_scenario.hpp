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

inline void run_sm_f4_scenario (zlink::framework::route_client_t &routes,
                                const zlink::framework::spot_rid_t &remote_spot)
{
    const auto missing_spot =
      zlink::framework::spot_rid_t::from_string ("user:play-b:missing-spot-route");
    auto missing_spot_route =
      routes
        .request (route_channel, zlink::routing_id_t::from (std::string ("play-b")), missing_spot,
                  direct_spot_req_t{"external-client", "missing-route"})
        .packet_name ("DirectSpotReq")
        .timeout (std::chrono::milliseconds (300))
        .async<direct_spot_res_t> ()
        .result ();
    if (missing_spot_route.has_value ()) {
        throw std::runtime_error ("SM-F4 missing target spot route unexpectedly succeeded");
    }

    auto recovery_after_negative =
      routes
        .request (route_channel, zlink::routing_id_t::from (std::string ("play-b")), remote_spot,
                  direct_spot_req_t{"external-client", "route-recovery"})
        .packet_name ("DirectSpotReq")
        .timeout (std::chrono::milliseconds (3000))
        .async<direct_spot_res_t> ()
        .result ();
    if (!recovery_after_negative.has_value ()
        || recovery_after_negative.value ().value != "route-recovery:reply") {
        throw std::runtime_error ("SM-F4 recovery spot route request failed");
    }

    std::cout << "scenario SM-F4 passed\n";
}

} // namespace zlink::framework::e2e::spot_service::client::scenarios
