/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include "../tictactoe_entry_spot.hpp"

namespace zlink::samples::tictactoe
{

inline task_t<join_game_res_t>
tictactoe_entry_spot_t::join_game (player_actor_t &actor,
                                   message_context_t &,
                                   const join_game_req_t &request)
{
    /* 공통 sample spec §13: JoinSpot payload에는 인증 때 actor에 설정한 PlayerInfo가 들어가고,
     * owner room Spot이 level 조건을 확인한다. */
    const auto payload = tictactoe_game_join_req_t{request.room_id, actor.require_player ()};
    actor.context ().join_spot (request.room_id, payload).defer ();
    co_return join_game_res_t{
      tictactoe_state_t{.room_id = request.room_id}};
}

} // namespace zlink::samples::tictactoe
