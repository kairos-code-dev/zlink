/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../Handlers/bingo_room_directory.hpp"

#include <zlink/framework.hpp>

#include <utility>

namespace zlink::samples::bingo
{

class start_bingo_game_handler_t
{
public:
  explicit start_bingo_game_handler_t (
    bingo_room_directory_t &rooms,
    zlink::framework::logger_t<> logger = {})
    : _rooms (rooms), _logger (std::move (logger))
  {
  }

  start_bingo_game_res_t handle (const start_bingo_game_req_t &request)
  {
    auto state = _rooms.get (request.room_id).start ();
    _logger.info ("start bingo game", { { "room_id", request.room_id } });
    return { state };
  }

private:
  bingo_room_directory_t &_rooms;
  zlink::framework::logger_t<> _logger;
};

} // namespace zlink::samples::bingo
