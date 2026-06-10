/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../../Shared/Contracts/messages.hpp"

#include <string>

namespace zlink::samples::tictactoe
{

class authenticate_player_handler_t
{
  public:
    using request_type = authenticate_player_req_t;
    using reply_type = authenticate_player_res_t;
    static constexpr const char *topic_name = "AuthenticatePlayer";

    authenticate_player_res_t handle (const authenticate_player_req_t &request)
    {
        if (request.access_token.empty ()) {
            return {false, "", "actor id must not be empty"};
        }
        return {true, request.access_token, ""};
    }
};

} // namespace zlink::samples::tictactoe
