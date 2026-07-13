/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include "../bingo_room_spot.hpp"

namespace zlink::samples::bingo
{

inline stop_observing_bingo_events_res_t
bingo_room_spot_t::stop_observing_events (
  const player_actor_t &actor,
  const spot_actor_request_context_t &,
  const stop_observing_bingo_events_req_t &)
{
    observers.erase (actor.actor.actor_id);
    (void) _context.leave_actor (
      actor_ref_for (actor), const_cast<player_actor_t &> (actor));
    return {true, std::string (actor.actor.node_rid.value ())};
}

} // namespace zlink::samples::bingo
