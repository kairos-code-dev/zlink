/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Actors/support_user_actor.hpp"
#include "../Actors/support_actor_directory.hpp"
#include "../../../../Configuration/sample_names.hpp"
#include "../../../Application/ConversationAssignment/agent_availability_directory.hpp"

#include <zlink/framework.hpp>

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <string>
#include <vector>

namespace zlink::samples::supportchat
{

// SupportEntrySpot is the admission point before an actor enters a conversation.
// Customer actors ask the API channel to allocate and assign the conversation,
// then join the created ConversationSpot.
class support_entry_spot_t : public zlink::framework::entry_spot_t
{
  public:
    void configure (zlink::framework::entry_spot_context_t &context)
    {
        _context = context;
        context.handlers ().add_actor_packet<&support_entry_spot_t::open_conversation> ();
        context.handlers ().add_actor_packet<&support_entry_spot_t::set_agent_available> ();
    }

    void configure (zlink::framework::spot_context_t &context)
    {
        zlink::framework::entry_spot_context_t entry_context (context);
        configure (entry_context);
    }

    zlink::framework::task_t<open_conversation_res_t>
    open_conversation (const support_user_actor_t &actor,
                       zlink::framework::spot_actor_request_context_t &,
                       const open_conversation_req_t &request)
    {
        if (actor.role != support_chat_roles_t::customer) {
            throw zlink::framework::framework_exception_t (
              zlink::framework::framework_error_kind_t::request_failed,
              "Only customer actors can open a conversation.");
        }

        auto opened =
          co_await _context.outbound ()
            .request_to_channel (
              sample_names_t::api_channel,
              open_conversation_api_req_t{actor.actor_id (), actor.display_name, request.subject})
            .async<open_conversation_api_res_t> ();
        const auto spot_rid = zlink::framework::spot_rid_t::from_string (
          std::string (sample_names_t::conversation_spot_node) + ":"
          + opened.conversation_id);
        auto joined = co_await actor.context
                        .join_spot (spot_rid,
                                    join_conversation_req_t{opened.conversation_id})
                        .async<join_conversation_res_t> ();
        if (joined.result_code != 0) {
            throw zlink::framework::framework_exception_t (
              zlink::framework::framework_error_kind_t::request_failed,
              "Conversation join was rejected.");
        }

        co_return open_conversation_res_t{joined.reply.state.conversation_id,
                                          joined.reply.state};
    }

    set_agent_available_res_t
    set_agent_available (const support_user_actor_t &actor,
                         zlink::framework::spot_actor_request_context_t &,
                         const set_agent_available_req_t &request)
    {
        if (actor.role != support_chat_roles_t::agent) {
            throw zlink::framework::framework_exception_t (
              zlink::framework::framework_error_kind_t::request_failed,
              "Only agent actors can set availability.");
        }
        support_actor_directory_t::shared ().add_or_update (const_cast<support_user_actor_t &> (actor));
        agent_availability_directory_t::shared ().set_available (
          actor.actor_id (), actor.display_name, request.is_available);
        return set_agent_available_res_t{request.is_available};
    }

    void onCreateActor (const support_user_actor_t &actor)
    {
        created_actor_ids.push_back (actor.actor_id ());
    }

    void on_actor_joined (const support_user_actor_t &actor)
    {
        joined_actor_ids.push_back (actor.actor_id ());
        if (!actor.destroy_after_entry_spot_join) {
            return;
        }
        (void) _context.destroyActor (actor_ref_for (actor),
                                      const_cast<support_user_actor_t &> (actor));
    }

    void onLeaveActor (const support_user_actor_t &actor)
    {
        joined_actor_ids.erase (std::remove (joined_actor_ids.begin (), joined_actor_ids.end (),
                                             actor.actor_id ()),
                                joined_actor_ids.end ());
    }

    void onDisconnectActor (const support_user_actor_t &actor) { actor.mark_disconnected (); }

    std::vector<std::string> created_actor_ids;
    std::vector<std::string> joined_actor_ids;

  private:
    static long long now_unix_ms ()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds> (
                 std::chrono::system_clock::now ().time_since_epoch ())
          .count ();
    }

    static zlink::framework::actor_ref_t actor_ref_for (const support_user_actor_t &actor)
    {
        return zlink::framework::actor_ref_t (
          zlink::framework::node_rid_t::from_string (sample_names_t::support_spot_node),
          sample_names_t::support_actor_type, actor.actor_id (), actor.actor.generation);
    }

    zlink::framework::entry_spot_context_t _context;
};

} // namespace zlink::samples::supportchat
