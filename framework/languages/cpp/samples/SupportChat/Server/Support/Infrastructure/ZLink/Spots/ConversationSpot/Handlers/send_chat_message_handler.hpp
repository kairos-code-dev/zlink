/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../conversation_spot.hpp"

namespace zlink::samples::supportchat
{

using namespace framework;

// Spot actor handler: send a chat message inside a conversation. Length, closed
// state, and participant checks are decided by the Conversation domain.
class send_chat_message_handler_t
{
  public:
    send_chat_message_res_t handle (conversation_spot_t &spot,
                                    const support_user_actor_t &actor,
                                    const spot_actor_request_context_t &context,
                                    const send_chat_message_req_t &request) const
    {
        return spot.send_message (actor, context, request);
    }
};

} // namespace zlink::samples::supportchat
