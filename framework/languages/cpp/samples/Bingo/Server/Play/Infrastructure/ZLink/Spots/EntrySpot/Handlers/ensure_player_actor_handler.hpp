/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include "../bingo_entry_spot.hpp"

#include <stdexcept>

namespace zlink::samples::bingo
{

using namespace framework;

inline task_t<ensure_player_actor_res_t>
bingo_entry_spot_t::ensure_player_actor (const ensure_player_actor_req_t &request)
{
    const auto node_rid = _topology.selected_play_node_rid ();
    if (!request.preferred_actor_node_rid.empty ()
        && request.preferred_actor_node_rid != node_rid) {
        throw std::runtime_error ("EnsurePlayerActor reached the wrong Play node.");
    }
    auto actor = _services.get_required<session_actor_manager_t> ()
                   .get_or_create (sample_names_t::player_actor_type, request.actor_id, request)
                   .value ();
    auto joined = co_await actor.context ()
                    .join_entry_spot (node_rid_t::from_string (node_rid), request)
                    .yield ();
    const auto *joined_accepted =
      std::get_if<framework::actor_join_accepted_t<framework::message_t>> (&joined);
    if (joined_accepted == nullptr) {
        throw std::runtime_error ("Player actor entry spot join was rejected.");
    }
    co_return ensure_player_actor_res_t{
      request.actor_id, sample_names_t::player_actor_type,
      actor_ref_snapshot_t::from (joined_accepted->actor)};
}

} // namespace zlink::samples::bingo
