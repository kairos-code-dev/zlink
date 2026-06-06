/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../../Shared/Actors/player_actor.hpp"
#include "../GameSpots/tictactoe_match_room.hpp"

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
        context.handlers ().add_actor_packet<&entry_spot_t::join_match> ();
    }

    join_match_res_t join_match (const player_actor_t &actor,
                                 zlink::framework::spot_actor_request_context_t &,
                                 const join_match_req_t &request)
    {
        auto joined = request;
        joined.actor_id = actor.actor_id;
        return room.join (joined);
    }

    void on_post_actor_joined (const player_actor_t &actor)
    {
        actor_ids.push_back (actor.actor_id);
    }

    void on_actor_left (const player_actor_t &actor)
    {
        actor_ids.erase (std::remove (actor_ids.begin (), actor_ids.end (), actor.actor_id), actor_ids.end ());
    }

    tictactoe_match_room_t room{"entry-match"};
    std::vector<std::string> actor_ids;
};

} // namespace zlink::samples::tictactoe
