/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../conversation_spot.hpp"

namespace zlink::samples::supportchat
{

// Spot timer handler: forwards a time signal to the ConversationSpot. The
// idle/close decision is made by the Conversation domain, not here.
struct conversation_idle_timer_handler_t
{
    void handle (conversation_spot_t &spot, long long now_unix_ms) const
    {
        spot.check_idle (now_unix_ms);
    }
};

} // namespace zlink::samples::supportchat
