/* SPDX-License-Identifier: FSL-1.1-ALv2 */
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
        /* 공통 sample spec §6.2: room route 레코드는 route channel, owner node rid, spot rid,
         * spot kind로 구성한다. 이 샘플은 별도 RouteMeshChannel을 만들지 않으므로
         * RouteChannelId에는 room Spot을 소유한 SpotNode의 channel 이름을 기록한다. */
        _routes.save (room_route_t{sample_names_t::game_spot_node,
                                   _topology.selected_play_node_rid (), room_id,
                                   sample_names_t::match_spot});
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
