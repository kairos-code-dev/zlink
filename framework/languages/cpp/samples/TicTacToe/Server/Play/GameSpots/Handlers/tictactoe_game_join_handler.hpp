/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../tictactoe_match_room.hpp"
#include "../../../../Shared/Actors/player_actor.hpp"

namespace zlink::samples::tictactoe
{

class tictactoe_game_spot_t;

class tictactoe_game_join_handler_t
{
  public:
    using spot_type = tictactoe_game_spot_t;
    using actor_type = player_actor_t;
    using request_type = join_match_req_t;
    using reply_type = join_match_res_t;

    explicit tictactoe_game_join_handler_t (tictactoe_match_room_t &room) : _room (room) {}

    join_match_res_t handle (tictactoe_match_room_t &, const player_actor_t &, const join_match_req_t &request)
    {
        return _room.join (request);
    }

  private:
    tictactoe_match_room_t &_room;
};

} // namespace zlink::samples::tictactoe
