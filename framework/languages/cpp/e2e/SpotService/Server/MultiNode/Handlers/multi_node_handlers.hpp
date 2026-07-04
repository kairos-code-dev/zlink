/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Spots/multi_node_spots.hpp"

#include <zlink/framework.hpp>

#include <chrono>
#include <stdexcept>
#include <string>
#include <thread>

namespace e2e = zlink::framework::e2e::spot_service;

namespace
{

inline const char *multi_node_spot_name_for (const std::string &node_rid)
{
    return node_rid == multi_node_a_name ? e2e::multi_spot_a : e2e::multi_spot_b;
}

inline const char *multi_node_route_channel_for (const std::string &node_rid)
{
    return node_rid == multi_node_a_name ? e2e::multi_route_channel_a
                                         : e2e::multi_route_channel_b;
}

inline e2e::state_res_t request_multi_node_state (zlink::framework::route_client_t &routes,
                                                  const std::string &node_rid,
                                                  const std::string &spot_rid,
                                                  int delta)
{
    const auto deadline = std::chrono::steady_clock::now () + std::chrono::seconds (10);
    std::string last_error = "unknown route error";
    while (std::chrono::steady_clock::now () < deadline) {
        auto reply =
          routes
            .request_to_node (multi_node_route_channel_for (node_rid), zlink::routing_id_t::from (node_rid),
                      zlink::framework::spot_rid_t::from_string (spot_rid),
                      e2e::state_req_t{.op = "add", .amount = delta})
            .packet_name ("StateReq")
            .timeout (std::chrono::milliseconds (2000))
            .async<e2e::state_res_t> ()
            .result ();
        if (reply) {
            return reply.value ();
        }
        last_error = reply.error () ? reply.error ()->what () : "unknown route error";
        std::this_thread::sleep_for (std::chrono::milliseconds (100));
    }
    throw std::runtime_error ("timed out waiting for multi-node spot route '" + spot_rid
                              + "': " + last_error);
}

class multi_node_create_local_handler_t
{
  public:
    using dependency_types = zlink::framework::dependency_list_t<
      scenario_state_t, zlink::framework::spot_node_manager_t, zlink::framework::route_client_t>;
    using request_type = e2e::multi_node_create_spot_req_t;
    using reply_type = e2e::multi_node_create_spot_res_t;

    multi_node_create_local_handler_t (scenario_state_t &state,
                                       zlink::framework::spot_node_manager_t &spots,
                                       zlink::framework::route_client_t &routes) :
        _state (state), _spots (spots), _routes (routes)
    {
    }

    e2e::multi_node_create_spot_res_t handle (
      const e2e::multi_node_create_spot_req_t &request)
    {
        return create_spot (request);
    }

    e2e::multi_node_create_spot_res_t handle_route (
      const e2e::multi_node_create_spot_req_t &request,
      const zlink::framework::route_handler_context_t &)
    {
        return create_spot (request);
    }

  private:
    e2e::multi_node_create_spot_res_t create_spot (
      const e2e::multi_node_create_spot_req_t &request)
    {
        const auto rid = zlink::framework::spot_rid_t::from_string (request.spot_rid);
        const auto created =
          _spots.get_or_create_spot (multi_node_spot_name_for (_state.node_rid), rid, request);
        const auto state =
          created.state == zlink::framework::spot_create_state_t::created ? "created"
                                                                          : "existing";
        _state.record ("MultiCreateSpot", {}, request.spot_rid, state);
        return {.spot_rid = request.spot_rid,
                .node_rid = _state.node_rid,
                .state = state,
                .value = 0};
    }

    scenario_state_t &_state;
    zlink::framework::spot_node_manager_t &_spots;
    zlink::framework::route_client_t &_routes;
};

class multi_node_state_route_handler_t
{
  public:
    using dependency_types = zlink::framework::dependency_list_t<scenario_state_t,
                                                                zlink::framework::route_client_t>;
    using request_type = e2e::multi_node_state_route_req_t;
    using reply_type = e2e::state_res_t;

    multi_node_state_route_handler_t (scenario_state_t &state,
                                      zlink::framework::route_client_t &routes) :
        _state (state), _routes (routes)
    {
    }

    e2e::state_res_t handle (const e2e::multi_node_state_route_req_t &request)
    {
        return request_multi_node_state (_routes, _state.node_rid, request.spot_rid, request.delta);
    }

  private:
    scenario_state_t &_state;
    zlink::framework::route_client_t &_routes;
};

} // namespace
