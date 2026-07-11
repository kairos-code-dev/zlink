/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../bingo_room_spot.hpp"

namespace zlink::samples::bingo
{

inline observe_bingo_events_res_t bingo_room_spot_t::observe_events (
  const player_actor_t &actor,
  const spot_actor_request_context_t &,
  const observe_bingo_events_req_t &request)
{
    _game.set_room_id_if_empty (request.room_id);
    observers[actor.actor.actor_id] = const_cast<player_actor_t *> (&actor);
    return {true, std::string (actor.actor.node_rid.value ())};
}

} // namespace zlink::samples::bingo
