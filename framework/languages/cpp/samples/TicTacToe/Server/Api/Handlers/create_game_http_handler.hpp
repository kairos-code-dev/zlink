/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include "../../Configuration/sample_names.hpp"

#include <zlink/framework.hpp>

namespace zlink::samples::tictactoe
{

using namespace framework;

class create_game_http_handler_t
{
  public:
    using request_type = create_game_http_req_t;
    using reply_type = create_game_http_res_t;
    using dependency_types =
      dependency_list_t<channel_client_t, logger_t<create_game_http_handler_t>>;
    static constexpr const char *topic_name = "CreateGame";

    explicit create_game_http_handler_t (channel_client_t &client,
                                         logger_t<create_game_http_handler_t> &logger) :
        _client (client), _logger (logger)
    {
    }

    task_t<create_game_http_res_t> handle (const create_game_http_req_t &request)
    {
        const auto game_name =
          request.game_name.empty () ? std::string ("tictactoe-game") : request.game_name;
        _logger.info ("http POST /games");
        _logger.info (std::string ("recv ") + create_game_http_req_t::packet_name);
        const auto create_request = create_game_req_t{game_name};
        auto room = co_await _client.request (sample_names_t::play_channel, create_request)
                      .submit<create_game_res_t> ();
        _logger.info (std::string ("reply ") + create_game_http_res_t::packet_name);
        co_return create_game_http_res_t{
          room.room_id,        room.game_name,  room.owner_play_endpoint,
          room.play_endpoints, room.play_nodes, room.required_level};
    }

  private:
    channel_client_t &_client;
    logger_t<create_game_http_handler_t> _logger;
};

} // namespace zlink::samples::tictactoe
