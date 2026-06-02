/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "tictactoe_game_models.hpp"

namespace zlink::samples::tictactoe
{

struct tictactoe_game_contract_mapper_t
{
  static tictactoe_state_t to_contract (const tictactoe_state_t &state)
  {
    return state;
  }
};

} // namespace zlink::samples::tictactoe
