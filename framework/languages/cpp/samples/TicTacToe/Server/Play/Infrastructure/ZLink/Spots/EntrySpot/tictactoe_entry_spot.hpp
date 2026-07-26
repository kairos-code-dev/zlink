/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include "../../Actors/player_actor.hpp"
#include "../../../../../Configuration/sample_names.hpp"

#include <zlink/framework.hpp>

#include <algorithm>
#include <iostream>
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
                                       message_context_t &,
                                       const join_game_req_t &request);

    observe_milestone_res_t observe_milestone (const player_actor_t &actor,
                                               message_context_t &,
                                               const observe_milestone_req_t &);

    /* 공통 sample spec §13: 인증에서 받은 PlayerInfo가 actor 생성 payload로 들어오고,
     * actor는 그 값(display name/level/wins)을 그대로 보관한다. */
    void on_create_actor (const player_actor_t &actor, const message_t &create_request)
    {
        actor.apply_player (create_request.decode<player_info_t> ());
        created_actor_ids.push_back (actor.actor_id);
    }

    task_t<void> on_actor_joined (const player_actor_t &actor)
    {
        actor_ids.push_back (actor.actor_id);
        if (!actor.destroy_after_entry_spot_join) {
            co_return;
        }
        std::cout << "entry spot: actor destroy requested. actor=" << actor.actor_id << std::endl;
        co_await _context.destroy_actor (const_cast<player_actor_t &> (actor));
        std::cout << "entry spot: actor destroy completed. actor=" << actor.actor_id << std::endl;
    }

    task_t<void> on_leave_actor (const player_actor_t &actor)
    {
        actor_ids.erase (std::remove (actor_ids.begin (), actor_ids.end (), actor.actor_id),
                         actor_ids.end ());
        observers.erase (actor.actor_id);
        co_return;
    }

    task_t<void> on_disconnect_actor (const player_actor_t &actor)
    {
        actor.mark_disconnected ();
        observers.erase (actor.actor_id);
        co_return;
    }

    std::vector<std::string> created_actor_ids;
    std::vector<std::string> actor_ids;

  private:
    void on_player_win_milestone (const player_win_milestone_event_t &event);

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

#include "Handlers/play_actor_join_game_handler.hpp"
#include "Handlers/play_actor_observe_milestone_handler.hpp"
#include "Handlers/player_win_milestone_event_handler.hpp"
