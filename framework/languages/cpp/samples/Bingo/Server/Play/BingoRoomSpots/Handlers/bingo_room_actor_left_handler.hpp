/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../Handlers/bingo_room_directory.hpp"

#include <string>

namespace zlink::samples::bingo
{

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

} // namespace zlink::samples::bingo
