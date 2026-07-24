/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include "../bingo_entry_spot.hpp"

namespace zlink::samples::bingo
{

inline task_t<observe_bingo_events_res_t>
bingo_entry_spot_t::observe_bingo_events (
  const player_actor_t &actor,
  spot_actor_request_context_t &,
  const observe_bingo_events_req_t &request)
{
    const auto display_name =
      actor.display_name.empty () ? actor.actor.actor_id : actor.display_name;
    const auto observer_rid = observer_room_rid (request.room_id, _context.node_rid ());
    const bingo_room_settings_payload_t payload{
      "Bingo Observer " + std::string (_context.node_rid ().value ()),
      bingo_sample_modes_t::two_player, 0, 75, "Observer", request.room_id};
    _context.manager ().get_or_create_spot (
      sample_names_t::room_spot, observer_rid, payload);
    const auto join_request = bingo_room_join_req_t{
      request.room_id, actor.actor.actor_id, display_name, true};
    auto joined = co_await actor.context.join_spot (observer_rid, join_request)
                    .submit<bingo_room_join_res_t> ();
    const auto &joined_accepted =
      std::get<framework::actor_join_accepted_t<bingo_room_join_res_t>> (joined);
    co_return observe_bingo_events_res_t{
      true, std::string (joined_accepted.actor.node_rid ().value ())};
}

} // namespace zlink::samples::bingo
