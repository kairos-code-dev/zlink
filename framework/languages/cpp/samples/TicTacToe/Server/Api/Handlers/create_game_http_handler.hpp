/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../Configuration/sample_names.hpp"
#include "../../Configuration/sample_topology.hpp"

#include <zlink/framework.hpp>

namespace zlink::samples::tictactoe
{

class create_game_http_handler_t
{
  public:
    using request_type = create_game_http_req_t;
    using reply_type = create_game_http_res_t;
    using dependency_types =
      zlink::framework::dependency_list_t<zlink::framework::channel_client_t,
                                          sample_topology_t,
                                          zlink::framework::logger_t<create_game_http_handler_t>>;
    static constexpr const char *topic_name = "CreateGame";

    explicit create_game_http_handler_t (
      zlink::framework::channel_client_t &client,
      sample_topology_t &topology,
      zlink::framework::logger_t<create_game_http_handler_t> &logger) :
        _client (client), _topology (topology), _logger (logger)
    {
    }

    zlink::framework::task_t<create_game_http_res_t> handle (const create_game_http_req_t &request)
    {
        const auto game_name =
          request.game_name.empty () ? std::string ("tictactoe-game") : request.game_name;
        _logger.info ("http POST /games");
        _logger.info (std::string ("recv ") + create_game_http_req_t::packet_name);
        auto room = co_await _client
                      .request (sample_names_t::play_channel, create_game_req_t{game_name})
                      .async<create_game_res_t> ();
        _logger.info (std::string ("reply ") + create_game_http_res_t::packet_name);
        co_return create_game_http_res_t{room.room_id,
                                         room.game_name,
                                         room.owner_play_endpoint,
                                         room.play_endpoints,
                                         room.play_nodes,
                                         room.required_level};
    }

  private:
    zlink::framework::channel_client_t &_client;
    sample_topology_t &_topology;
    zlink::framework::logger_t<create_game_http_handler_t> _logger;
};

} // namespace zlink::samples::tictactoe
