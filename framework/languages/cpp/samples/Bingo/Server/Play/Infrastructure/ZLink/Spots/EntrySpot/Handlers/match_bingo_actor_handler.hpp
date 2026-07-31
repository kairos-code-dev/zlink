/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include "../bingo_entry_spot.hpp"

namespace zlink::samples::bingo
{

inline task_t<match_bingo_res_t>
bingo_entry_spot_t::match_bingo (player_actor_t &actor,
                                 message_context_t &,
                                 const match_bingo_req_t &request)
{
    const auto display_name =
      actor.display_name.empty () ? actor.actor_id : actor.display_name;
    const auto match_request = match_bingo_api_req_t{
      actor.actor_id, display_name, request.mode};
    auto matched = co_await _context.outbound ()
                     .request (sample_names_t::api_channel, match_request)
                     .submit<match_bingo_api_res_t> ();
    const auto join_request = bingo_room_join_req_t{
      matched.room_id, actor.actor_id, display_name};
    actor.context ().join_spot (matched.room_id, join_request).defer ();
    co_return match_bingo_res_t{
      matched.room_id,
      bingo_room_state_t{.room_id = matched.room_id}};
}

} // namespace zlink::samples::bingo
