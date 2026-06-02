/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../../Shared/Configuration/sample_names.hpp"
#include "../../Play/Handlers/create_match_room_handler.hpp"

#include <zlink/framework.hpp>

namespace zlink::samples::tictactoe
{

class create_match_handler_t
{
public:
  using request_type = create_match_req_t;
  using reply_type = create_match_res_t;
  using dependency_types =
    zlink::framework::dependency_list_t<create_match_room_handler_t>;
  static constexpr const char *topic_name = "CreateMatch";

  explicit create_match_handler_t (create_match_room_handler_t &rooms)
    : _rooms (rooms)
  {
  }

  create_match_res_t handle (const create_match_req_t &request)
  {
    const auto room = _rooms.handle ({});
    const auto owner = request.owner_actor_id.empty ()
                         ? std::string (sample_names_t::x_actor_id)
                         : request.owner_actor_id;
    return { room.match_id, owner };
  }

private:
  create_match_room_handler_t &_rooms;
};

} // namespace zlink::samples::tictactoe
