/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../conversation_spot.hpp"

namespace zlink::samples::supportchat
{

using namespace framework;

// Spot actor handler: close a conversation explicitly.
class close_conversation_handler_t
{
  public:
    close_conversation_res_t handle (conversation_spot_t &spot,
                                     const support_user_actor_t &actor,
                                     const spot_actor_request_context_t &context,
                                     const close_conversation_req_t &request) const
    {
        return spot.close (actor, context, request);
    }
};

} // namespace zlink::samples::supportchat
