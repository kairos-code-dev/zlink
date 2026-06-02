/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../GameSpots/tictactoe_match_room.hpp"
#include "../../../../Shared/Actors/player_actor.hpp"

#include <zlink/framework.hpp>

namespace zlink::samples::tictactoe
{

struct entry_spot_t;

class join_match_handler_t
{
public:
  using spot_type = entry_spot_t;
  using actor_type = player_actor_t;
  using request_type = join_match_req_t;

  explicit join_match_handler_t (tictactoe_match_room_t &room) : _room (room)
  {
  }

  join_match_res_t handle (
    entry_spot_t &,
    const player_actor_t &actor,
    zlink::framework::spot_actor_request_context_t &,
    const join_match_req_t &request)
  {
    auto joined = request;
    joined.actor_id = actor.actor_id;
    return _room.join (joined);
  }

private:
  tictactoe_match_room_t &_room;
};

} // namespace zlink::samples::tictactoe
