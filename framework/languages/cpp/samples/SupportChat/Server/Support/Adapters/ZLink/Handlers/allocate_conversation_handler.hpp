/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../../Application/ConversationAssignment/support_conversation_allocator.hpp"
#include "../../../../Configuration/sample_names.hpp"
#include "../../../../../Shared/Contracts/messages.hpp"

#include <zlink/framework.hpp>

namespace zlink::samples::supportchat
{

// Support channel handler: the API server requests conversation allocation. The
// allocator owns ConversationId creation; the spot mesh owns spot creation.
class allocate_conversation_handler_t
{
  public:
    using request_type = allocate_conversation_req_t;
    using reply_type = allocate_conversation_res_t;
    using dependency_types =
      zlink::framework::dependency_list_t<support_conversation_allocator_t>;
    static constexpr const char *topic_name = "AllocateConversation";

    explicit allocate_conversation_handler_t (support_conversation_allocator_t &allocator) :
        _allocator (allocator)
    {
    }

    allocate_conversation_res_t handle (const allocate_conversation_req_t &request)
    {
        const auto conversation_id =
          _allocator.allocate (request.customer_actor_id, request.subject);
        return allocate_conversation_res_t{conversation_id,
                                           conversation_statuses_t::waiting_for_agent};
    }

  private:
    support_conversation_allocator_t &_allocator;
};

} // namespace zlink::samples::supportchat
