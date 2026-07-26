/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include "../tictactoe_game_spot.hpp"

namespace zlink::samples::tictactoe
{

inline place_mark_res_t
tictactoe_game_spot_t::place_mark (const player_actor_t &actor,
                                   const message_context_t &context,
                                   const place_mark_req_t &request)
{
    if (context.packet_name.empty ()) {
        throw std::runtime_error ("packet name is required");
    }
    auto state = match ().place (actor.actor_id, request);
    game_state_notify_t state_notify{state.room_id, state.next_turn, state};
    publisher.publish (state_notify, actor.actor_id);
    if (state.status == tictactoe_status_t::won || state.status == tictactoe_status_t::draw) {
        publish_win_milestone (actor, state);
    }
    return {state};
}

} // namespace zlink::samples::tictactoe
