/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include "../../../Configuration/sample_names.hpp"
#include "../../../../Shared/Contracts/messages.hpp"

#include <zlink/framework.hpp>

#include <string>

namespace zlink::samples::tictactoe
{

using namespace framework;

class tictactoe_game_creator_t
{
  public:
    using dependency_types = dependency_list_t<sample_topology_t>;

    explicit tictactoe_game_creator_t (sample_topology_t &topology) :
        _topology (topology)
    {
    }

    std::string create_room_id () { return "room-" + std::to_string (_next++); }

    create_game_res_t create (std::string game_name)
    {
        auto room_id = create_room_id ();
        return {room_id,
                std::move (game_name),
                _topology.selected_stream_endpoint (),
                {_topology.play_a_stream_endpoint, _topology.play_b_stream_endpoint},
                {{_topology.play_a_stream_endpoint},
                 {_topology.play_b_stream_endpoint}},
                sample_names_t::required_level};
    }

  private:
    sample_topology_t &_topology;
    int _next = 1;
};

} // namespace zlink::samples::tictactoe
