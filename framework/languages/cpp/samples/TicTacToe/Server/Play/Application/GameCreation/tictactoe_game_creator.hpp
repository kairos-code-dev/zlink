/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../../Configuration/sample_names.hpp"
#include "../../../Configuration/redis_room_route_store.hpp"
#include "../../../../Shared/Contracts/messages.hpp"

#include <zlink/framework.hpp>

#include <string>

namespace zlink::samples::tictactoe
{

using namespace framework;

class tictactoe_game_creator_t
{
  public:
    using dependency_types = dependency_list_t<sample_topology_t, redis_room_route_store_t>;

    tictactoe_game_creator_t (sample_topology_t &topology, redis_room_route_store_t &routes) :
        _topology (topology), _routes (routes)
    {
    }

    std::string create_room_id () { return "room-" + std::to_string (_next++); }

    create_game_res_t create (std::string game_name)
    {
        auto room_id = create_room_id ();
        _routes.save (room_route_t{room_id, _topology.selected_stream_endpoint (),
                                   _topology.selected_play_node_rid ()});
        return {room_id,
                std::move (game_name),
                _topology.selected_stream_endpoint (),
                {_topology.play_a_stream_endpoint, _topology.play_b_stream_endpoint},
                {{_topology.play_a_stream_endpoint, _topology.play_a_node_rid},
                 {_topology.play_b_stream_endpoint, _topology.play_b_node_rid}},
                sample_names_t::required_level};
    }

  private:
    sample_topology_t &_topology;
    redis_room_route_store_t &_routes;
    int _next = 1;
};

} // namespace zlink::samples::tictactoe
