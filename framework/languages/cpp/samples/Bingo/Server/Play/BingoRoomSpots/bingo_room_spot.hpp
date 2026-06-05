/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Actors/player_actor.hpp"
#include "Handlers/bingo_room_actor_joined_handler.hpp"
#include "Handlers/bingo_room_actor_left_handler.hpp"
#include "Handlers/bingo_room_join_handler.hpp"
#include "Handlers/start_bingo_game_handler.hpp"
#include "bingo_room.hpp"

#include <zlink/framework.hpp>

namespace zlink::samples::bingo
{

class bingo_room_spot_t : public bingo_room_t
{
  public:
    using bingo_room_t::bingo_room_t;

    void configure (zlink::framework::spot_context_t &context)
    {
        context.handlers ()
          .add_actor_join<bingo_room_join_handler_t> ()
          .add_actor_packet<start_bingo_game_handler_t> ()
          .add_post_actor_joined<bingo_room_actor_joined_handler_t> ()
          .add_actor_left<bingo_room_actor_left_handler_t> ();
    }
};

} // namespace zlink::samples::bingo
