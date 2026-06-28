/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../support_entry_spot.hpp"

namespace zlink::samples::supportchat
{

using namespace framework;

// Entry Spot actor handler: agent registers availability. Customer actors that
// send this request receive an error from the entry spot.
class set_agent_available_handler_t
{
  public:
    set_agent_available_res_t handle (support_entry_spot_t &entry_spot,
                                      const support_user_actor_t &actor,
                                      spot_actor_request_context_t &context,
                                      const set_agent_available_req_t &request) const
    {
        return entry_spot.set_agent_available (actor, context, request);
    }
};

} // namespace zlink::samples::supportchat
