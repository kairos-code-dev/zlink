/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../../../Shared/Contracts/messages.hpp"

#include <string>

namespace zlink::samples::tictactoe
{

class create_game_room_handler_t
{
  public:
    using request_type = create_game_room_req_t;
    using reply_type = create_game_room_res_t;
    static constexpr const char *topic_name = "CreateGameRoom";

    create_game_room_res_t handle (const create_game_room_req_t &)
    {
        return {"room-" + std::to_string (_next++)};
    }

  private:
    int _next = 1;
};

} // namespace zlink::samples::tictactoe
