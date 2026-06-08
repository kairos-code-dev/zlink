/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../../Shared/Configuration/sample_names.hpp"
#include "../../../Shared/Configuration/sample_topology.hpp"
#include "../../Play/Application/GameCreation/create_game_room_handler.hpp"

#include <zlink/framework.hpp>

namespace zlink::samples::tictactoe
{

class create_game_handler_t
{
  public:
    using request_type = create_game_req_t;
    using reply_type = create_game_res_t;
    using dependency_types =
      zlink::framework::dependency_list_t<create_game_room_handler_t,
                                          sample_topology_t,
                                          zlink::framework::logger_t<create_game_handler_t>>;
    static constexpr const char *topic_name = "CreateGame";

    explicit create_game_handler_t (create_game_room_handler_t &rooms,
                                     sample_topology_t &topology,
                                     zlink::framework::logger_t<create_game_handler_t> &logger) :
        _rooms (rooms), _topology (topology), _logger (logger)
    {
    }

    create_game_res_t handle (const create_game_req_t &request)
    {
        _logger.info ("http POST /games");
        _logger.info (std::string ("recv ") + create_game_req_t::packet_name);
        const auto room = _rooms.handle ({});
        const auto owner = request.owner_actor_id.empty ()
                             ? std::string (sample_names_t::x_actor_id)
                             : request.owner_actor_id;
        _logger.info (std::string ("reply ") + create_game_req_t::packet_name);
        return {room.room_id, owner, _topology.stream_endpoint};
    }

  private:
    create_game_room_handler_t &_rooms;
    sample_topology_t &_topology;
    zlink::framework::logger_t<create_game_handler_t> _logger;
};

} // namespace zlink::samples::tictactoe
