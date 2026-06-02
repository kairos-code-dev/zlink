/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../Handlers/bingo_room_directory.hpp"
#include "../bingo_room.hpp"
#include "../../Actors/player_actor.hpp"

#include <zlink/framework.hpp>

#include <utility>

namespace zlink::samples::bingo
{

class bingo_room_spot_t;

class bingo_room_join_handler_t
{
public:
  using spot_type = bingo_room_spot_t;
  using actor_type = player_actor_t;
  using request_type = bingo_room_join_req_t;
  using reply_type = bingo_room_join_res_t;

  explicit bingo_room_join_handler_t (
    bingo_room_directory_t &rooms,
    zlink::framework::logger_t<> logger = {})
    : _rooms (rooms), _logger (std::move (logger))
  {
  }

  bingo_room_join_res_t handle (bingo_room_t &,
                                const player_actor_t &actor,
                                const bingo_room_join_req_t &request)
  {
    auto &room = _rooms.get (request.room_id);
    const auto actor_id = actor.actor.actor_id.empty ()
                            ? request.actor_id
                            : actor.actor.actor_id;
    room.join (actor_id, request.display_name);
    _logger.info ("join bingo room",
                  { { "room_id", request.room_id },
                    { "actor_id", actor_id } });
    return { room.snapshot () };
  }

private:
  bingo_room_directory_t &_rooms;
  zlink::framework::logger_t<> _logger;
};

} // namespace zlink::samples::bingo
