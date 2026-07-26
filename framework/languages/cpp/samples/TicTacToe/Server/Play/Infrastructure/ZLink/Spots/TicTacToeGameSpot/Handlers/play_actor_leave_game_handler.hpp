/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include "../tictactoe_game_spot.hpp"

namespace zlink::samples::tictactoe
{

inline void tictactoe_game_spot_t::leave_game (const player_actor_t &actor,
                                               const message_context_t &,
                                               const leave_game_req_t &request)
{
    if (request.room_id != match ().snapshot ().room_id) {
        throw std::runtime_error ("LeaveGameReq room id does not match the joined room.");
    }
    match ().ensure_can_leave (actor.actor_id);
    std::cout << "actor: LeaveGameReq received. actor=" << actor.actor_id
              << ", roomId=" << request.room_id << std::endl;
    actor.mark_for_destroy_after_room_leave ();
    (void) _context.leave_actor (actor_ref_for (actor), const_cast<player_actor_t &> (actor));
    std::cout << "actor: LeaveGameReq completed. actor=" << actor.actor_id
              << ", roomId=" << request.room_id << std::endl;
}

} // namespace zlink::samples::tictactoe
