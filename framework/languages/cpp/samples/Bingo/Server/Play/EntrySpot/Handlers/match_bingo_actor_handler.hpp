/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../Handlers/bingo_room_directory.hpp"

#include <string>

namespace zlink::samples::bingo
{

class match_bingo_actor_handler_t
{
public:
  explicit match_bingo_actor_handler_t (bingo_room_directory_t &rooms)
    : _rooms (rooms)
  {
  }

  match_bingo_res_t handle (const match_bingo_req_t &request,
                            const std::string &actor_id,
                            const std::string &display_name)
  {
    const auto room_id = _rooms.allocate (request.mode);
    auto &room = _rooms.get (room_id);
    room.join (actor_id, display_name);
    return { room_id, room.snapshot () };
  }

private:
  bingo_room_directory_t &_rooms;
};

} // namespace zlink::samples::bingo
