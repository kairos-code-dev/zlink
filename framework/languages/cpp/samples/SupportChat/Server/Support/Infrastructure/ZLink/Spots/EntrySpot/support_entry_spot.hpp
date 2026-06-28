/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../Actors/support_user_actor.hpp"
#include "../../Actors/support_actor_directory.hpp"
#include "../../../../../Configuration/sample_names.hpp"
#include "../../../../Application/ConversationAssignment/agent_availability_directory.hpp"

#include <zlink/framework.hpp>

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <string>
#include <vector>

namespace zlink::samples::supportchat
{

using namespace framework;
using framework::actor_ref_t;
using framework::message_t;

// SupportEntrySpot is the admission point before an actor enters a conversation.
// Customer actors ask the API channel to allocate and assign the conversation,
// then join the created ConversationSpot.
class support_entry_spot_t : public entry_spot_t
{
  public:
    void configure (entry_spot_context_t &context)
    {
        _context = context;
        context.handlers ().add_actor_packet<&support_entry_spot_t::open_conversation> ();
        context.handlers ().add_actor_packet<&support_entry_spot_t::set_agent_available> ();
    }

    void configure (spot_context_t &context)
    {
        entry_spot_context_t entry_context (context);
        configure (entry_context);
    }

    task_t<open_conversation_res_t> open_conversation (const support_user_actor_t &actor,
                                                       spot_actor_request_context_t &,
                                                       const open_conversation_req_t &request)
    {
        if (actor.role != support_chat_roles_t::customer) {
            throw framework_exception_t (framework_error_kind_t::request_failed,
                                         "Only customer actors can open a conversation.");
        }

        const auto open_request = open_conversation_api_req_t{
            actor.actor_id (), actor.display_name, request.subject
        };
        auto opened = co_await _context.outbound ()
            .request_to_channel (sample_names_t::api_channel, open_request).async<open_conversation_api_res_t> ();
        const auto spot_rid = spot_rid_t::from_string (
          std::string (sample_names_t::support_spot_node) + ":" + opened.conversation_id);
        const auto join_request = join_conversation_req_t{opened.conversation_id};
        auto joined = co_await actor.context.join_spot (spot_rid, join_request).async<join_conversation_res_t> ();
        if (joined.result_code != 0) {
            throw framework_exception_t (framework_error_kind_t::request_failed,
                                         "Conversation join was rejected.");
        }

        co_return open_conversation_res_t{joined.reply.state.conversation_id, joined.reply.state};
    }

    set_agent_available_res_t set_agent_available (const support_user_actor_t &actor,
                                                   spot_actor_request_context_t &,
                                                   const set_agent_available_req_t &request)
    {
        if (actor.role != support_chat_roles_t::agent) {
            throw framework_exception_t (framework_error_kind_t::request_failed,
                                         "Only agent actors can set availability.");
        }
        support_actor_directory_t::shared ().add_or_update (
          const_cast<support_user_actor_t &> (actor));
        agent_availability_directory_t::shared ().set_available (
          actor.actor_id (), actor.display_name, request.is_available);
        return set_agent_available_res_t{request.is_available};
    }

    void onCreateActor (support_user_actor_t &actor, const message_t &create_request)
    {
        const auto request = create_request.decode<ensure_support_user_actor_req_t> ();
        actor.set_identity (request.display_name, request.role);
        support_actor_directory_t::shared ().add_or_update (actor);
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
        joined_actor_ids.erase (
          std::remove (joined_actor_ids.begin (), joined_actor_ids.end (), actor.actor_id ()),
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

    static actor_ref_t actor_ref_for (const support_user_actor_t &actor)
    {
        return actor_ref_t (node_rid_t::from_string (sample_names_t::support_spot_node),
                            sample_names_t::support_actor_type, actor.actor_id (),
                            actor.actor.generation);
    }

    entry_spot_context_t _context;
};

} // namespace zlink::samples::supportchat
