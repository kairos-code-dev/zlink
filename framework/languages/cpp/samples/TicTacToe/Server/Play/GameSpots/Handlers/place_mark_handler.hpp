/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../tictactoe_match_room.hpp"
#include "../../../../Shared/Actors/player_actor.hpp"

#include <zlink/framework.hpp>

#include <stdexcept>
#include <string>
#include <utility>

namespace zlink::samples::tictactoe
{

class tictactoe_game_spot_t;

class place_mark_handler_t
{
  public:
    using spot_type = tictactoe_game_spot_t;
    using actor_type = player_actor_t;
    using request_type = place_mark_req_t;

    explicit place_mark_handler_t (tictactoe_match_room_t &room, zlink::framework::logger_t<> logger = {}) :
        _room (room), _logger (std::move (logger))
    {
    }

    place_mark_res_t handle (tictactoe_match_room_t &,
                             const player_actor_t &,
                             const zlink::framework::spot_actor_request_context_t &context,
                             const place_mark_req_t &request)
    {
        if (context.packet_name.empty ()) {
            throw std::runtime_error ("packet name is required");
        }
        auto state = _room.place (request);
        _logger.info (
          "place tictactoe mark",
          {{"match_id", request.match_id}, {"actor_id", request.actor_id}, {"cell", std::to_string (request.cell)}});
        return {state};
    }

  private:
    tictactoe_match_room_t &_room;
    zlink::framework::logger_t<> _logger;
};

} // namespace zlink::samples::tictactoe
