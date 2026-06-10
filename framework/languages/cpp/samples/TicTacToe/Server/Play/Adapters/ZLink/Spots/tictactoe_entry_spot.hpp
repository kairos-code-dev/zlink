/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Actors/player_actor.hpp"
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
    void configure (zlink::framework::spot_context_t &context)
    {
        context.handlers ().add_actor_packet<&entry_spot_t::join_game> ();
    }

    join_game_res_t join_game (const player_actor_t &actor,
                                 zlink::framework::spot_actor_request_context_t &,
                                 const join_game_req_t &request)
    {
        return room.join (actor.actor_id, request);
    }

    void on_post_actor_joined (const player_actor_t &actor)
    {
        actor_ids.push_back (actor.actor_id);
    }

    void on_actor_left (const player_actor_t &actor)
    {
        actor_ids.erase (std::remove (actor_ids.begin (), actor_ids.end (), actor.actor_id),
                         actor_ids.end ());
    }

    tictactoe_match_t room{"entry-match"};
    std::vector<std::string> actor_ids;
};

} // namespace zlink::samples::tictactoe
