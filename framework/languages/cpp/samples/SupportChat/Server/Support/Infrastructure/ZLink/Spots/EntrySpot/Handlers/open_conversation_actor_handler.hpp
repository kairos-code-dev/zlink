/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../support_entry_spot.hpp"

namespace zlink::samples::supportchat
{

using namespace framework;

// Entry Spot actor handler: customer opens a conversation. Delegates the
// admission decision and orchestration to the SupportEntrySpot.
class open_conversation_actor_handler_t
{
  public:
    task_t<open_conversation_res_t> handle (support_entry_spot_t &entry_spot,
                                            const support_user_actor_t &actor,
                                            spot_actor_request_context_t &context,
                                            const open_conversation_req_t &request) const
    {
        return entry_spot.open_conversation (actor, context, request);
    }
};

} // namespace zlink::samples::supportchat
