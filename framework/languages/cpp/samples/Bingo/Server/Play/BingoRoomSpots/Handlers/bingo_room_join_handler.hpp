/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../Handlers/bingo_room_directory.hpp"

#include <zlink/framework.hpp>

#include <utility>

namespace zlink::samples::bingo
{

class bingo_room_join_handler_t
{
public:
  explicit bingo_room_join_handler_t (
    bingo_room_directory_t &rooms,
    zlink::framework::logger_t<> logger = {})
    : _rooms (rooms), _logger (std::move (logger))
  {
  }

  bingo_room_join_res_t handle (const bingo_room_join_req_t &request)
  {
    auto &room = _rooms.get (request.room_id);
    room.join (request.actor_id, request.display_name);
    _logger.info ("join bingo room",
                  { { "room_id", request.room_id },
                    { "actor_id", request.actor_id } });
    return { room.snapshot () };
  }

private:
  bingo_room_directory_t &_rooms;
  zlink::framework::logger_t<> _logger;
};

} // namespace zlink::samples::bingo
