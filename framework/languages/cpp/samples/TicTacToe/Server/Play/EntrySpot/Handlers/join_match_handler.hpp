/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../GameSpots/tictactoe_match_room.hpp"

namespace zlink::samples::tictactoe
{

class join_match_handler_t
{
public:
  explicit join_match_handler_t (tictactoe_match_room_t &room) : _room (room)
  {
  }

  join_match_res_t handle (const join_match_req_t &request)
  {
    return _room.join (request);
  }

private:
  tictactoe_match_room_t &_room;
};

} // namespace zlink::samples::tictactoe
