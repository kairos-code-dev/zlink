/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include "../tictactoe_game_spot.hpp"

namespace zlink::samples::tictactoe
{

class tictactoe_game_spot_created_handler_t
{
  public:
    tictactoe_state_t handle (const tictactoe_game_spot_t &spot) const { return spot.snapshot (); }
};

} // namespace zlink::samples::tictactoe
