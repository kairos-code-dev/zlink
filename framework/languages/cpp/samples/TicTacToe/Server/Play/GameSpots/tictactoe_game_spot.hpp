/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../../Shared/Actors/player_actor.hpp"
#include "Handlers/place_mark_handler.hpp"
#include "Handlers/tictactoe_game_join_handler.hpp"
#include "Handlers/tictactoe_game_spot_actor_joined_handler.hpp"
#include "Handlers/tictactoe_game_spot_actor_left_handler.hpp"
#include "tictactoe_match_room.hpp"

#include <zlink/framework.hpp>

namespace zlink::samples::tictactoe
{

class tictactoe_game_spot_t : public tictactoe_match_room_t
{
  public:
    using tictactoe_match_room_t::tictactoe_match_room_t;

    void configure (zlink::framework::spot_context_t &context)
    {
        context.handlers ()
          .add_actor_join<tictactoe_game_join_handler_t> ()
          .add_actor_packet<place_mark_handler_t> ()
          .add_post_actor_joined<tictactoe_game_spot_actor_joined_handler_t> ()
          .add_actor_left<tictactoe_game_spot_actor_left_handler_t> ();
    }
};

} // namespace zlink::samples::tictactoe
