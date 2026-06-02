/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../../Shared/Contracts/messages.hpp"

#include <string>

namespace zlink::samples::tictactoe
{

class authenticate_actor_handler_t
{
public:
  authenticate_actor_res_t handle (const authenticate_actor_req_t &request)
  {
    if (request.actor_id.empty ()) {
      return { false, "", "actor id must not be empty" };
    }
    return { true, request.actor_id, "" };
  }
};

} // namespace zlink::samples::tictactoe
