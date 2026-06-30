/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Support/client_support.hpp"

#include <iostream>

namespace zlink::framework::e2e::registration_codec::client
{

inline void run_manual_registration_scenario (zlink::framework::route_client_t &routes)
{
    auto request =
      routes
        .request (route_channel, zlink::routing_id_t::from (std::string ("rc-server")),
                  echo_manual_req_t{.value = "manual"})
        .packet_name ("EchoManual")
        .timeout (std::chrono::milliseconds (2000))
        .async<echo_manual_res_t> ();
    ensure (request.result ().has_value (), "RC-A3 route request failed");
    ensure (request.result ().value ().value == "manual:manual", "RC-A3 reply mismatch");
    ensure (request.result ().value ().packet_name == "EchoManual",
            "RC-A3 packet name mismatch");
    ensure (request.result ().value ().content_type == "application/json",
            "RC-A3 content type mismatch");
    std::cout << "scenario RC-A3 passed\n";
}

} // namespace zlink::framework::e2e::registration_codec::client
