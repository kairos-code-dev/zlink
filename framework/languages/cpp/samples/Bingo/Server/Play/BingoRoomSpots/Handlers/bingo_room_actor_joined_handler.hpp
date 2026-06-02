/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../bingo_notification_publisher.hpp"
#include "../bingo_room.hpp"
#include "../../Actors/player_actor.hpp"

#include <zlink/framework.hpp>

#include <utility>

namespace zlink::samples::bingo
{

class bingo_room_spot_t;

class bingo_room_actor_joined_handler_t
{
public:
  using spot_type = bingo_room_spot_t;
  using actor_type = player_actor_t;

  explicit bingo_room_actor_joined_handler_t (
    bingo_notification_publisher_t &publisher)
    : _publisher (publisher)
  {
  }

  void handle (const bingo_room_t &spot,
               const player_actor_t &actor,
               const zlink::framework::spot_actor_change_result_t &)
  {
    const auto &state = spot.snapshot ();
    _publisher.publish_joined ({ state.room_id,
                                 actor.actor.actor_id,
                                 actor.actor.actor_id,
                                 0,
                                 actor.actor.actor_id == state.host_actor_id,
                                 state });
  }

private:
  bingo_notification_publisher_t &_publisher;
};

} // namespace zlink::samples::bingo
