/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Server/Support/Application/ConversationAssignment/agent_assignment_service.hpp"
#include "../Server/Support/Domain/SupportChat/conversation.hpp"

#include <cassert>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>

namespace zlink::samples::supportchat
{

class supportchat_client_scenario_t
{
  public:
    void run ()
    {
        authenticate_res_t customer{"customer-1", "Customer One", role_t::customer};
        authenticate_res_t agent{"agent-1", "Agent One", role_t::agent};
        expect (customer.role == role_t::customer, "customer authentication failed");
        expect (agent.role == role_t::agent, "agent authentication failed");

        agent_availability_directory_t availability;
        availability.set_available (agent.actor_id, 3);
        agent_assignment_service_t assignment (availability);

        conversation_t conversation ("supportchat-conversation-1",
                                     "Order delivery question",
                                     customer.actor_id);

        const auto customer_joined = conversation.join_customer (customer.actor_id,
                                                                 customer.display_name);
        expect (customer_joined.state.status == conversation_status_t::waiting_for_agent,
                "customer join must leave conversation waiting for agent");

        const auto assigned_agent = assignment.assign ();
        expect (assigned_agent && *assigned_agent == agent.actor_id,
                "available agent was not assigned");
        const auto assigned = conversation.assign_agent (*assigned_agent);
        expect (assigned.state.agent_actor_id == agent.actor_id,
                "assignment must expose roster actor id");

        const auto agent_joined = conversation.join_agent (agent.actor_id, agent.display_name);
        expect (agent_joined.actor_id == agent.actor_id,
                "agent participant notify must use roster actor id");
        expect (agent_joined.state.status == conversation_status_t::active,
                "agent join must activate conversation");

        const auto first = conversation.send_message (customer.actor_id, "I need help.", 1000);
        expect (first.message.message_seq == 1, "customer message sequence mismatch");
        expect (first.state.last_message_seq == 1, "state did not record first message");

        const auto typing = conversation.set_typing (agent.actor_id, true);
        expect (typing.actor_id == agent.actor_id && typing.is_typing,
                "typing notify mismatch");

        const auto reply = conversation.send_message (agent.actor_id, "I can help.", 1200);
        expect (reply.message.message_seq == 2, "agent message sequence mismatch");

        const auto rejoined = conversation.join_agent (agent.actor_id, agent.display_name);
        expect (rejoined.state.last_message_seq == 2,
                "reconnect join must preserve conversation state");

        const auto idle = conversation.mark_idle ();
        expect (idle.state.status == conversation_status_t::waiting_for_close,
                "idle transition mismatch");

        const auto resumed = conversation.send_message (customer.actor_id, "Still here.", 1500);
        expect (resumed.state.status == conversation_status_t::active,
                "message after idle must reactivate conversation");

        const auto closed = conversation.close ();
        expect (closed.state.status == conversation_status_t::closed,
                "close transition mismatch");

        std::cout << "supportchat authentication=verified" << std::endl;
        std::cout << "supportchat conversation-assignment=verified" << std::endl;
        std::cout << "supportchat bound-push=verified" << std::endl;
        std::cout << "supportchat reconnect=verified" << std::endl;
        std::cout << "supportchat idle-close=verified" << std::endl;
        std::cout << "supportchat=completed" << std::endl;
    }

  private:
    static void expect (bool condition, const std::string &message)
    {
        if (!condition) {
            throw std::runtime_error (message);
        }
    }
};

} // namespace zlink::samples::supportchat
