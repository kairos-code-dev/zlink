/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <string>

namespace zlink::samples::tictactoe
{

class tictactoe_game_creator_t
{
  public:
    std::string create_room_id () { return "room-" + std::to_string (_next++); }

  private:
    int _next = 1;
};

} // namespace zlink::samples::tictactoe
