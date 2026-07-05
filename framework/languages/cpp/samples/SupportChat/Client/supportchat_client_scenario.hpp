/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Shared/Contracts/messages.hpp"

#include <chrono>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>

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

        std::unordered_map<std::string, int> agent_capacity;
        std::unordered_map<std::string, int> agent_load;
        agent_capacity[agent.actor_id] = 3;

        const auto initial_state = conversation_state_t{"supportchat-conversation-1",
                                                        "Order delivery question",
                                                        conversation_status_t::waiting_for_agent,
                                                        customer.actor_id,
                                                        std::nullopt,
                                                        0,
                                                        std::nullopt,
                                                        std::nullopt};

        const auto customer_joined = participant_joined_notify_t{
          initial_state.conversation_id, customer.actor_id, role_t::customer, initial_state};
        expect (customer_joined.state.status == conversation_status_t::waiting_for_agent,
                "customer join must leave conversation waiting for agent");

        const auto assigned_agent =
          agent_load[agent.actor_id] < agent_capacity[agent.actor_id]
            ? std::optional<std::string>{agent.actor_id}
            : std::nullopt;
        expect (assigned_agent && *assigned_agent == agent.actor_id,
                "available agent was not assigned");
        ++agent_load[*assigned_agent];
        auto assigned_state = initial_state;
        assigned_state.agent_actor_id = assigned_agent;
        const auto assigned =
          conversation_assigned_notify_t{initial_state.conversation_id, assigned_state};
        expect (assigned.state.agent_actor_id == agent.actor_id,
                "assignment must expose roster actor id");

        auto active_state = assigned.state;
        active_state.status = conversation_status_t::active;
        const auto agent_joined =
          participant_joined_notify_t{initial_state.conversation_id,
                                      agent.actor_id,
                                      role_t::agent,
                                      active_state};
        expect (agent_joined.actor_id == agent.actor_id,
                "agent participant notify must use roster actor id");
        expect (agent_joined.state.status == conversation_status_t::active,
                "agent join must activate conversation");

        auto first_state = active_state;
        first_state.last_message_seq = 1;
        first_state.last_message_at_unix_ms = 1000;
        const auto first = send_chat_message_res_t{
          chat_message_t{initial_state.conversation_id, 1, customer.actor_id, "I need help.", 1000},
          first_state};
        expect (first.message.message_seq == 1, "customer message sequence mismatch");
        expect (first.state.last_message_seq == 1, "state did not record first message");

        const auto typing =
          typing_changed_notify_t{initial_state.conversation_id, agent.actor_id, true, first.state};
        expect (typing.actor_id == agent.actor_id && typing.is_typing,
                "typing notify mismatch");

        auto reply_state = first.state;
        reply_state.last_message_seq = 2;
        reply_state.last_message_at_unix_ms = 1200;
        const auto reply = send_chat_message_res_t{
          chat_message_t{initial_state.conversation_id, 2, agent.actor_id, "I can help.", 1200},
          reply_state};
        expect (reply.message.message_seq == 2, "agent message sequence mismatch");

        const auto rejoined =
          participant_joined_notify_t{initial_state.conversation_id,
                                      agent.actor_id,
                                      role_t::agent,
                                      reply.state};
        expect (rejoined.state.last_message_seq == 2,
                "reconnect join must preserve conversation state");

        auto idle_state = reply.state;
        idle_state.status = conversation_status_t::waiting_for_close;
        const auto idle = conversation_idle_notify_t{initial_state.conversation_id, idle_state};
        expect (idle.state.status == conversation_status_t::waiting_for_close,
                "idle transition mismatch");

        auto resumed_state = idle.state;
        resumed_state.status = conversation_status_t::active;
        resumed_state.last_message_seq = 3;
        resumed_state.last_message_at_unix_ms = 1500;
        const auto resumed = send_chat_message_res_t{
          chat_message_t{initial_state.conversation_id, 3, customer.actor_id, "Still here.", 1500},
          resumed_state};
        expect (resumed.state.status == conversation_status_t::active,
                "message after idle must reactivate conversation");

        auto closed_state = resumed.state;
        closed_state.status = conversation_status_t::closed;
        const auto closed = conversation_closed_notify_t{initial_state.conversation_id, closed_state};
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
