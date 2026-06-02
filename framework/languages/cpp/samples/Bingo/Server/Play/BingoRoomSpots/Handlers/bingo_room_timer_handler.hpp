/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../bingo_room_handlers.hpp"

namespace zlink::samples::bingo
{

struct bingo_room_timer_handler_t
{
  number_drawn_notify_t handle (bingo_room_t &room, int number) const
  {
    return room.draw (number);
  }
};

} // namespace zlink::samples::bingo
