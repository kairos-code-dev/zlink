/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../game_quest_server_role.hpp"

#include <zlink/framework.hpp>

namespace zlink::samples::gamequest
{

using namespace framework;

class complete_mission_handler_t
{
  public:
    using request_type = complete_mission_req_t;
    using reply_type = event_res_t;
    using dependency_types = dependency_list_t<game_quest_server_role_t>;
    static constexpr const char *topic_name = "CompleteMissionReq";

    explicit complete_mission_handler_t (game_quest_server_role_t &server) : _server (server) {}

    event_res_t handle (const complete_mission_req_t &request)
    {
        return _server.complete_mission (request);
    }

  private:
    game_quest_server_role_t &_server;
};

} // namespace zlink::samples::gamequest
