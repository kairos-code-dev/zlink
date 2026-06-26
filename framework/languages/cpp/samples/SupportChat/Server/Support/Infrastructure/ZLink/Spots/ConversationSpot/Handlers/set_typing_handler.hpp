/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../conversation_spot.hpp"

namespace zlink::samples::supportchat
{

// Spot actor handler: change typing state inside a conversation.
class set_typing_handler_t
{
  public:
    set_typing_res_t handle (conversation_spot_t &spot, const support_user_actor_t &actor,
                             const zlink::framework::spot_actor_request_context_t &context,
                             const set_typing_req_t &request) const
    {
        return spot.set_typing (actor, context, request);
    }
};

} // namespace zlink::samples::supportchat
