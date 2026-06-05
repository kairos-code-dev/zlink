/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../game_notification_publisher.hpp"
#include "../tictactoe_match_room.hpp"
#include "../../../../Shared/Actors/player_actor.hpp"

#include <zlink/framework.hpp>

#include <utility>

namespace zlink::samples::tictactoe
{

class tictactoe_game_spot_t;

class tictactoe_game_spot_actor_left_handler_t
{
  public:
    using spot_type = tictactoe_game_spot_t;
    using actor_type = player_actor_t;

    explicit tictactoe_game_spot_actor_left_handler_t (game_notification_publisher_t &publisher) :
        _publisher (publisher)
    {
    }

    void handle (const tictactoe_match_room_t &spot,
                 const player_actor_t &,
                 const zlink::framework::spot_actor_change_result_t &)
    {
        const auto &state = spot.snapshot ();
        _publisher.publish_game_ended ({state.match_id, state.winner_actor_id, state.draw, state});
    }

  private:
    game_notification_publisher_t &_publisher;
};

} // namespace zlink::samples::tictactoe
