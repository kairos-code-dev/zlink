/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../../Application/ConversationAssignment/agent_assignment_service.hpp"
#include "../../../../Configuration/sample_names.hpp"
#include "../../../../../Shared/Contracts/messages.hpp"
#include "../Actors/support_actor_directory.hpp"

#include <zlink/framework.hpp>

namespace zlink::samples::supportchat
{

// Support channel handler: the API server requests agent assignment. When no
// agent is available the conversation stays in WaitingForAgent (a wait state,
// not an error).
class assign_agent_handler_t
{
  public:
    using request_type = assign_agent_req_t;
    using reply_type = assign_agent_res_t;
    using dependency_types = zlink::framework::dependency_list_t<agent_assignment_service_t>;
    static constexpr const char *topic_name = "AssignAgent";

    explicit assign_agent_handler_t (agent_assignment_service_t &assignment) :
        _assignment (assignment)
    {
    }

    zlink::framework::task_t<assign_agent_res_t> handle (const assign_agent_req_t &request)
    {
        auto candidate = _assignment.assign_next_agent ();
        if (!candidate) {
            co_return assign_agent_res_t{request.conversation_id,
                                         conversation_statuses_t::waiting_for_agent, ""};
        }
        auto &actor = support_actor_directory_t::shared ().get (candidate->actor_id);
        const auto spot_rid = zlink::framework::spot_rid_t::from_string (
          std::string (sample_names_t::support_spot_node) + ":"
          + request.conversation_id);
        auto joined = co_await actor.context
                        .join_spot (spot_rid,
                                    join_conversation_req_t{request.conversation_id})
                        .async<join_conversation_res_t> ();
        co_return assign_agent_res_t{request.conversation_id, joined.reply.state.status,
                                     candidate->actor_id};
    }

  private:
    agent_assignment_service_t &_assignment;
};

} // namespace zlink::samples::supportchat
