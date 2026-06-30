/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../../Shared/spot_service_contracts.hpp"

#include <zlink/framework.hpp>

#include <chrono>

namespace e2e = zlink::framework::e2e::spot_service;

class channel_control_ping_route_handler_t
{
  public:
    using dependency_types =
      zlink::framework::dependency_list_t<zlink::framework::route_client_t>;

    explicit channel_control_ping_route_handler_t (zlink::framework::route_client_t &routes) :
        _routes (routes)
    {
    }

    zlink::framework::http_response_t handle (const zlink::framework::http_request_t &http)
    {
        const auto request =
          nlohmann::json::parse (http.body).get<e2e::channel_control_ping_req_t> ();
        auto reply =
          _routes
            .request (e2e::route_channel, zlink::routing_id_t::from (request.target_node_rid),
                      e2e::channel_echo_req_t{request.value})
            .packet_name ("ChannelEchoReq")
            .timeout (std::chrono::milliseconds (5000))
            .async<e2e::channel_echo_res_t> ()
            .result ();
        if (!reply) {
            throw zlink::framework::framework_exception_t (
              reply.error_kind (),
              reply.error () ? reply.error ()->what () : "ChannelEchoReq failed");
        }

        zlink::framework::http_response_t response;
        response.body = nlohmann::json (e2e::channel_control_ping_res_t{
                                         .node_rid = reply.value ().handled_by,
                                         .value = request.value})
                          .dump ();
        return response;
    }

  private:
    zlink::framework::route_client_t &_routes;
};
