/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../Play/Handlers/bingo_room_directory.hpp"

#include <zlink/framework.hpp>

#include <utility>

namespace zlink::samples::bingo
{

class match_bingo_api_handler_t
{
public:
  explicit match_bingo_api_handler_t (
    bingo_room_directory_t &rooms,
    zlink::framework::logger_t<> logger = {})
    : _rooms (rooms), _logger (std::move (logger))
  {
  }

  match_bingo_api_res_t handle (const match_bingo_api_req_t &request)
  {
    const auto room_id = _rooms.allocate (request.mode);
    _logger.info ("match bingo room",
                  { { "actor_id", request.actor_id },
                    { "room_id", room_id },
                    { "mode", request.mode } });
    return { room_id };
  }

private:
  bingo_room_directory_t &_rooms;
  zlink::framework::logger_t<> _logger;
};

} // namespace zlink::samples::bingo
