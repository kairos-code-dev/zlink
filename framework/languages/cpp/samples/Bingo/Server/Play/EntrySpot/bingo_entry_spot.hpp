/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Actors/player_actor.hpp"
#include "../Handlers/bingo_room_directory.hpp"

#include <zlink/framework.hpp>

#include <algorithm>
#include <string>
#include <vector>

namespace zlink::samples::bingo
{

class bingo_entry_spot_t : public zlink::framework::entry_spot_t
{
  public:
    void configure (zlink::framework::spot_context_t &context)
    {
        context.handlers ().add_actor_packet<&bingo_entry_spot_t::match_bingo> ();
    }

    match_bingo_res_t match_bingo (const player_actor_t &actor,
                                   zlink::framework::spot_actor_request_context_t &,
                                   const match_bingo_req_t &request)
    {
        const auto room_id = rooms.allocate (request.mode);
        auto &room = rooms.get (room_id);
        const auto display_name = actor.display_name.empty () ? actor.actor.actor_id : actor.display_name;
        room.join (actor.actor.actor_id, display_name);
        return {room_id, room.snapshot ()};
    }

    void on_post_actor_joined (const player_actor_t &actor)
    {
        joined_actor_ids.push_back (actor.actor.actor_id);
    }

    void on_actor_left (const player_actor_t &actor)
    {
        joined_actor_ids.erase (std::remove (joined_actor_ids.begin (), joined_actor_ids.end (), actor.actor.actor_id),
                                joined_actor_ids.end ());
    }

    bingo_room_directory_t rooms;
    std::vector<std::string> joined_actor_ids;
};

} // namespace zlink::samples::bingo
