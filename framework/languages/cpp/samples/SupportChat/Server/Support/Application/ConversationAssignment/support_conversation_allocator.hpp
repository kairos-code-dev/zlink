/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <string>

namespace zlink::samples::supportchat
{

// Produces a domain ConversationId for each new support ticket. The Support
// adapter turns this id into a Spot routing id when it creates the
// ConversationSpot. The allocator owns identity, not transport.
class support_conversation_allocator_t
{
  public:
    std::string allocate (const std::string &customer_actor_id,
                          const std::string &subject)
    {
        (void) customer_actor_id;
        (void) subject;
        return "conversation-" + std::to_string (_next++);
    }

  private:
    int _next = 1;
};

} // namespace zlink::samples::supportchat
