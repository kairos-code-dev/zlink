/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include "../bingo_entry_spot.hpp"

namespace zlink::samples::bingo
{

inline task_t<match_bingo_res_t>
bingo_entry_spot_t::match_bingo (const player_actor_t &actor,
                                 spot_actor_request_context_t &,
                                 const match_bingo_req_t &request)
{
    const auto display_name =
      actor.display_name.empty () ? actor.actor.actor_id : actor.display_name;
    const auto match_request = match_bingo_api_req_t{
      actor.actor.actor_id, display_name, request.mode,
      std::string (_context.node_rid ().value ())};
    auto matched = co_await _context.outbound ()
                     .request (sample_names_t::api_channel, match_request)
                     .submit<match_bingo_api_res_t> ();
    const auto spot_rid = spot_rid_t::from_string (matched.room_id);
    const auto join_request = bingo_room_join_req_t{
      matched.room_id, actor.actor.actor_id, display_name};
    auto joined = co_await actor.context.join_spot (spot_rid, join_request)
                    .submit<bingo_room_join_res_t> ();
    const auto *accepted =
      std::get_if<actor_join_accepted_t<bingo_room_join_res_t>> (&joined);
    if (accepted == nullptr) {
        throw framework_exception_t (framework_error_kind_t::request_failed,
                                     "Bingo room join was rejected");
    }
    co_return match_bingo_res_t{matched.room_id, accepted->reply.state,
                                matched.room_owner_node_rid};
}

} // namespace zlink::samples::bingo
