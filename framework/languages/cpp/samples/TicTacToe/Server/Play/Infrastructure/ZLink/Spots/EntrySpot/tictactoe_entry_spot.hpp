/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../Actors/player_actor.hpp"
#include "../../../../../Configuration/sample_names.hpp"
#include "../../../../Domain/TicTacToe/tictactoe_match.hpp"

#include <zlink/framework.hpp>

#include <algorithm>
#include <map>
#include <string>
#include <vector>

namespace zlink::samples::tictactoe
{

using namespace framework;
using framework::actor_ref_t;
using framework::message_t;

class tictactoe_entry_spot_t : public entry_spot_t
{
  public:
    void configure (entry_spot_context_t &context)
    {
        _context = context;
        context.handlers ().add_actor_request<&tictactoe_entry_spot_t::join_game> ();
        context.handlers ().add_actor_request<&tictactoe_entry_spot_t::observe_milestone> ();
        context.handlers ().add_subscribe<&tictactoe_entry_spot_t::on_player_win_milestone> (
          sample_names_t::player_milestone_topic);
    }

    void configure (spot_context_t &context)
    {
        entry_spot_context_t entry_context (context);
        configure (entry_context);
    }

    task_t<join_game_res_t> join_game (const player_actor_t &actor,
                                       spot_actor_request_context_t &,
                                       const join_game_req_t &request)
    {
        const auto spot_rid =
          spot_rid_t::from_string (std::string (sample_names_t::spot_node) + ":" + request.room_id);
        auto payload = tictactoe_game_join_req_t{request.room_id, request.player};
        if (payload.player.actor_id.empty ()) {
            payload.player = {actor.actor_id, actor.actor_id, sample_names_t::required_level, 0};
        }
        auto joined =
          co_await actor.context.join_spot (spot_rid, payload).async<join_game_res_t> ();
        co_return joined.reply;
    }

    observe_milestone_res_t observe_milestone (const player_actor_t &actor,
                                               spot_actor_request_context_t &,
                                               const observe_milestone_req_t &)
    {
        observers[actor.actor_id] = const_cast<player_actor_t *> (&actor);
        return {true};
    }

    void onCreateActor (const player_actor_t &actor, const message_t &create_request)
    {
        const auto request = create_request.decode<ensure_player_actor_req_t> ();
        actor.apply_player (
          player_info_t{request.actor_id, request.actor_id, sample_names_t::required_level, 0});
        created_actor_ids.push_back (actor.actor_id);
    }

    void on_actor_joined (const player_actor_t &actor)
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
        observers.erase (actor.actor_id);
    }

    void onDisconnectActor (const player_actor_t &actor) { actor.mark_disconnected (); }

    tictactoe_match_t room{"entry-match"};
    std::vector<std::string> created_actor_ids;
    std::vector<std::string> actor_ids;

  private:
    void on_player_win_milestone (const player_win_milestone_msg_t &event)
    {
        for (auto &[_, actor] : observers) {
            const auto notify =
              win_milestone_notify_t{event.room_id, event.actor_id, event.display_name, event.wins,
                                     std::string (_context.node_rid ().value ())};
            (void) actor->push (notify);
        }
    }

    static actor_ref_t actor_ref_for (const player_actor_t &actor)
    {
        return actor_ref_t (node_rid_t::from_string (actor.node_rid.empty ()
                                                       ? std::string (sample_names_t::spot_node)
                                                       : actor.node_rid),
                            sample_names_t::actor_type, actor.actor_id, actor.generation);
    }

    entry_spot_context_t _context;
    std::map<std::string, player_actor_t *> observers;
};

} // namespace zlink::samples::tictactoe
