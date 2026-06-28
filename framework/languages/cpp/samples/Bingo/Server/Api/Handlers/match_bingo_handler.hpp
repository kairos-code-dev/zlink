/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../Configuration/sample_names.hpp"
#include "../../../Shared/Contracts/messages.hpp"

#include <zlink/framework.hpp>

#include <utility>

namespace zlink::samples::bingo
{

using namespace framework;

class match_bingo_api_handler_t
{
  public:
    using request_type = match_bingo_api_req_t;
    using reply_type = match_bingo_api_res_t;
    using dependency_types = dependency_list_t<route_client_t>;
    static constexpr const char *topic_name = "MatchBingo";

    explicit match_bingo_api_handler_t (route_client_t &routes, logger_t<> logger = {}) :
        _routes (routes), _logger (std::move (logger))
    {
    }

    task_t<match_bingo_api_res_t> handle (const match_bingo_api_req_t &request)
    {
        const auto allocate_request =
          allocate_bingo_room_req_t{request.mode, request.actor_id, request.actor_node_rid};
        auto allocated =
          co_await _routes
            .request (sample_names_t::play_channel,
                      zlink::routing_id_t::from (request.actor_node_rid), allocate_request)
            .async<allocate_bingo_room_res_t> ();
        _logger.info (
          "match bingo room",
          {{"actor_id", request.actor_id}, {"room_id", allocated.room_id}, {"mode", request.mode}});
        co_return match_bingo_api_res_t{allocated.room_id, allocated.room_owner_node_rid};
    }

  private:
    route_client_t &_routes;
    logger_t<> _logger;
};

} // namespace zlink::samples::bingo
