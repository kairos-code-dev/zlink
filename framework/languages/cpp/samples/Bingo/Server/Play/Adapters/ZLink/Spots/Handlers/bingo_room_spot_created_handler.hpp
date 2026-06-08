/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../bingo_room_spot.hpp"

namespace zlink::samples::bingo
{

class bingo_room_spot_created_handler_t
{
  public:
    bingo_room_state_t handle (const bingo_room_spot_t &spot) const { return spot.snapshot (); }
};

} // namespace zlink::samples::bingo
