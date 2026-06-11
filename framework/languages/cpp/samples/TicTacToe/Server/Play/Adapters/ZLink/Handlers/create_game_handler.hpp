/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../../../Configuration/sample_topology.hpp"
#include "../../../../../Shared/Contracts/messages.hpp"
#include "../../../Application/GameCreation/tictactoe_game_creator.hpp"

#include <zlink/framework.hpp>

namespace zlink::samples::tictactoe
{

class create_game_handler_t
{
  public:
    using request_type = create_game_req_t;
    using reply_type = create_game_res_t;
    using dependency_types =
      zlink::framework::dependency_list_t<tictactoe_game_creator_t, sample_topology_t>;
    static constexpr const char *topic_name = "CreateGame";

    create_game_handler_t (tictactoe_game_creator_t &creator, sample_topology_t &topology) :
        _creator (creator), _topology (topology)
    {
    }

    create_game_res_t handle (const create_game_req_t &request)
    {
        return {_creator.create_room_id (), _topology.stream_endpoint, request.game_name};
    }

  private:
    tictactoe_game_creator_t &_creator;
    sample_topology_t &_topology;
};

} // namespace zlink::samples::tictactoe
