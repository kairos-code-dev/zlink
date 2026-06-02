/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../Handlers/bingo_room_directory.hpp"
#include "../bingo_room.hpp"
#include "../../Actors/player_actor.hpp"

#include <zlink/framework.hpp>

#include <string>

namespace zlink::samples::bingo
{

class bingo_room_spot_t;

class leave_room_handler_t
{
public:
  explicit leave_room_handler_t (bingo_room_directory_t &rooms)
    : _rooms (rooms)
  {
  }

  leave_room_res_t handle (const leave_room_req_t &request,
                           const std::string &actor_id)
  {
    return { _rooms.get (request.room_id).leave (actor_id) };
  }

private:
  bingo_room_directory_t &_rooms;
};

class bingo_room_actor_left_handler_t
{
public:
  using spot_type = bingo_room_spot_t;
  using actor_type = player_actor_t;

  void handle (const bingo_room_t &,
               const player_actor_t &,
               const zlink::framework::spot_actor_change_result_t &) const
  {
  }
};

} // namespace zlink::samples::bingo
