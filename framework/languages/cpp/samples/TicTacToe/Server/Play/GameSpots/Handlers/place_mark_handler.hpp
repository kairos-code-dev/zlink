/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../tictactoe_match_room.hpp"

#include <zlink/framework.hpp>

#include <string>
#include <utility>

namespace zlink::samples::tictactoe
{

class place_mark_handler_t
{
public:
  explicit place_mark_handler_t (
    tictactoe_match_room_t &room,
    zlink::framework::logger_t<> logger = {})
    : _room (room), _logger (std::move (logger))
  {
  }

  place_mark_res_t handle (const place_mark_req_t &request)
  {
    auto state = _room.place (request);
    _logger.info ("place tictactoe mark",
                  { { "match_id", request.match_id },
                    { "actor_id", request.actor_id },
                    { "cell", std::to_string (request.cell) } });
    return { state };
  }

private:
  tictactoe_match_room_t &_room;
  zlink::framework::logger_t<> _logger;
};

} // namespace zlink::samples::tictactoe
