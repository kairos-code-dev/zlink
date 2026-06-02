/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../Actors/player_actor.hpp"
#include "../../Handlers/bingo_room_directory.hpp"

#include <zlink/framework.hpp>

#include <string>

namespace zlink::samples::bingo
{

struct bingo_entry_spot_t;

class match_bingo_actor_handler_t
{
public:
  using spot_type = bingo_entry_spot_t;
  using actor_type = player_actor_t;
  using request_type = match_bingo_req_t;

  explicit match_bingo_actor_handler_t (bingo_room_directory_t &rooms)
    : _rooms (rooms)
  {
  }

  match_bingo_res_t handle (
    bingo_entry_spot_t &,
    const player_actor_t &actor,
    zlink::framework::spot_actor_request_context_t &,
    const match_bingo_req_t &request)
  {
    const auto room_id = _rooms.allocate (request.mode);
    auto &room = _rooms.get (room_id);
    const auto display_name =
      actor.display_name.empty () ? actor.actor.actor_id : actor.display_name;
    room.join (actor.actor.actor_id, display_name);
    return { room_id, room.snapshot () };
  }

private:
  bingo_room_directory_t &_rooms;
};

} // namespace zlink::samples::bingo
