/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include "../tictactoe_entry_spot.hpp"

namespace zlink::samples::tictactoe
{

inline task_t<join_game_res_t>
tictactoe_entry_spot_t::join_game (const player_actor_t &actor,
                                   spot_actor_request_context_t &,
                                   const join_game_req_t &request)
{
    const auto spot_rid = spot_rid_t::from_string (request.room_id);
    auto payload = tictactoe_game_join_req_t{request.room_id, request.player};
    if (payload.player.actor_id.empty ()) {
        payload.player = {actor.actor_id, actor.actor_id, sample_names_t::required_level, 0};
    }
    auto joined = co_await actor.context.join_spot (spot_rid, payload).async<join_game_res_t> ();
    co_return std::visit ([] (const auto &value) { return value.reply; }, joined);
}

} // namespace zlink::samples::tictactoe
