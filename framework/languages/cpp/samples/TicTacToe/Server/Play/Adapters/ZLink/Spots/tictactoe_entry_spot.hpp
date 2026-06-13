/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Actors/player_actor.hpp"
#include "../../../../Configuration/sample_names.hpp"
#include "../../../Domain/TicTacToe/tictactoe_match.hpp"

#include <zlink/framework.hpp>

#include <algorithm>
#include <string>
#include <vector>

namespace zlink::samples::tictactoe
{

class entry_spot_t : public zlink::framework::entry_spot_t
{
  public:
    void configure (zlink::framework::entry_spot_context_t &context)
    {
        _context = context;
        context.handlers ().add_actor_packet<&entry_spot_t::join_game> ();
    }

    void configure (zlink::framework::spot_context_t &context)
    {
        zlink::framework::entry_spot_context_t entry_context (context);
        configure (entry_context);
    }

    zlink::framework::task_t<join_game_res_t>
    join_game (const player_actor_t &actor,
               zlink::framework::spot_actor_request_context_t &,
               const join_game_req_t &request)
    {
        const auto spot_rid = zlink::framework::spot_rid_t::from_string (
          std::string (sample_names_t::spot_node) + ":" + request.room_id);
        auto joined =
          co_await actor.context.join_spot (spot_rid, to_stream_payload (request)).async ();
        join_game_res_t reply;
        from_stream_payload (joined.reply, reply);
        co_return reply;
    }

    void onCreateActor (const player_actor_t &actor)
    {
        created_actor_ids.push_back (actor.actor_id);
    }

    void onJoinActor (const player_actor_t &actor)
    {
        actor_ids.push_back (actor.actor_id);
        if (!actor.destroy_after_entry_spot_join) {
            return;
        }
        (void) _context.destroyActor (actor_ref_for (actor), const_cast<player_actor_t &> (actor));
    }

    void onLeaveActor (const player_actor_t &actor)
    {
        actor_ids.erase (std::remove (actor_ids.begin (), actor_ids.end (), actor.actor_id),
                         actor_ids.end ());
    }

    void onDisconnectActor (const player_actor_t &actor) { actor.mark_disconnected (); }

    tictactoe_match_t room{"entry-match"};
    std::vector<std::string> created_actor_ids;
    std::vector<std::string> actor_ids;

  private:
    static zlink::framework::actor_ref_t actor_ref_for (const player_actor_t &actor)
    {
        return zlink::framework::actor_ref_t (
          zlink::framework::node_rid_t::from_string (sample_names_t::spot_node),
          sample_names_t::actor_type, actor.actor_id, actor.generation);
    }

    zlink::framework::entry_spot_context_t _context;
};

} // namespace zlink::samples::tictactoe
