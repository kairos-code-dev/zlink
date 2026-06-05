/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../../Shared/Configuration/sample_names.hpp"
#include "../../../Shared/Configuration/sample_topology.hpp"
#include "../../../Shared/sample_log.hpp"
#include "../../Play/Handlers/create_match_room_handler.hpp"

#include <zlink/framework.hpp>

namespace zlink::samples::tictactoe
{

class create_match_handler_t
{
  public:
    using request_type = create_match_req_t;
    using reply_type = create_match_res_t;
    using dependency_types = zlink::framework::dependency_list_t<create_match_room_handler_t, sample_topology_t>;
    static constexpr const char *topic_name = "CreateMatch";

    explicit create_match_handler_t (create_match_room_handler_t &rooms, sample_topology_t &topology) :
        _rooms (rooms), _topology (topology)
    {
    }

    create_match_res_t handle (const create_match_req_t &request)
    {
        append_sample_log_line ("http POST /games");
        append_sample_log_line (std::string ("recv ") + create_match_req_t::packet_name);
        const auto room = _rooms.handle ({});
        const auto owner =
          request.owner_actor_id.empty () ? std::string (sample_names_t::x_actor_id) : request.owner_actor_id;
        append_sample_log_line (std::string ("reply ") + create_match_req_t::packet_name);
        return {room.match_id, owner, _topology.stream_endpoint};
    }

  private:
    create_match_room_handler_t &_rooms;
    sample_topology_t &_topology;
};

} // namespace zlink::samples::tictactoe
