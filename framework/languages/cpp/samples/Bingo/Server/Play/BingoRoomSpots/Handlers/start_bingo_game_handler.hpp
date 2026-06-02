/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../Handlers/bingo_room_directory.hpp"
#include "../bingo_room.hpp"
#include "../../Actors/player_actor.hpp"

#include <zlink/framework.hpp>

#include <stdexcept>
#include <utility>

namespace zlink::samples::bingo
{

class bingo_room_spot_t;

class start_bingo_game_handler_t
{
public:
  using spot_type = bingo_room_spot_t;
  using actor_type = player_actor_t;
  using request_type = start_bingo_game_req_t;

  explicit start_bingo_game_handler_t (
    bingo_room_directory_t &rooms,
    zlink::framework::logger_t<> logger = {})
    : _rooms (rooms), _logger (std::move (logger))
  {
  }

  start_bingo_game_res_t handle (
    bingo_room_t &,
    const player_actor_t &,
    const zlink::framework::spot_actor_request_context_t &context,
    const start_bingo_game_req_t &request)
  {
    if (context.packet_name.empty ()) {
      throw std::runtime_error ("packet name is required");
    }
    auto state = _rooms.get (request.room_id).start ();
    _logger.info ("start bingo game", { { "room_id", request.room_id } });
    return { state };
  }

private:
  bingo_room_directory_t &_rooms;
  zlink::framework::logger_t<> _logger;
};

} // namespace zlink::samples::bingo
