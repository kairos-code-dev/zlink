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
      zlink::framework::dependency_list_t<support_conversation_allocator_t,
                                          zlink::framework::spot_node_manager_t>;
    static constexpr const char *topic_name = "AllocateConversation";

    explicit allocate_conversation_handler_t (support_conversation_allocator_t &allocator) :
        _allocator (allocator), _spots (nullptr)
    {
    }

    allocate_conversation_handler_t (support_conversation_allocator_t &allocator,
                                              zlink::framework::spot_node_manager_t &spots) :
        _allocator (allocator), _spots (&spots)
    {
    }

    allocate_conversation_res_t handle (const allocate_conversation_req_t &request)
    {
        const auto conversation_id =
          _allocator.allocate (request.customer_actor_id, request.subject);
        const auto spot_rid = zlink::framework::spot_rid_t::from_string (
          std::string (sample_names_t::conversation_spot_node) + ":"
          + conversation_id);
        if (_spots != nullptr) {
            (void) _spots->get_or_create_spot (
              sample_names_t::conversation_spot,
              spot_rid,
              conversation_create_request_t{request.customer_actor_id,
                                            request.customer_display_name,
                                            request.subject,
                                            static_cast<long long> (0)});
        }
        return allocate_conversation_res_t{conversation_id,
                                           conversation_statuses_t::waiting_for_agent};
    }

  private:
    support_conversation_allocator_t &_allocator;
    zlink::framework::spot_node_manager_t *_spots;
};

} // namespace zlink::samples::supportchat
