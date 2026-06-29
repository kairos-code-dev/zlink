/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "support_chat_client_options.hpp"
#include "../Shared/Contracts/messages.hpp"

#include <zlink/stream_e2e_client/codecs/auto_codec.hpp>

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>

#define ensure(condition) ensure ((condition), #condition)

namespace zlink::samples::supportchat
{

// Reads like a scenario test: each request is verified immediately after its
// response, and each server push is awaited at the step where it is expected.
class support_chat_client_scenario_t
{
  public:
    using connector_t = stream_e2e_client::coroutine_connector_t;

    bool run (connector_t &customer,
              connector_t &agent,
              connector_t &reconnecting_customer,
              connector_t &waiting_customer,
              connector_t &closing_customer,
              connector_t &closing_agent)
    {
        auto result = run_async (customer, agent, reconnecting_customer, waiting_customer,
                                 closing_customer, closing_agent)
                        .result ();
        return result && result.value ();
    }

#undef ensure
  private:
    static stream_e2e_client::task_t<bool> run_async (connector_t &customer,
                                                      connector_t &agent,
                                                      connector_t &reconnecting_customer,
                                                      connector_t &waiting_customer,
                                                      connector_t &closing_customer,
                                                      connector_t &closing_agent)
    {
        try {
            // 1-3. agent connect, authenticate, register availability.
            trace ("agent connect");
            co_await agent.connect ().async ();
            const auto agent_auth_request = authenticate_req_t{support_chat_tokens_t::agent1};
            auto agent_auth =
              co_await stream_e2e_client::codecs::request (agent, agent_auth_request)
                .async<authenticate_res_t> ();
            ensure (agent_auth.actor_id == support_chat_tokens_t::agent1);
            ensure (agent_auth.role == support_chat_roles_t::agent);

            const auto set_agent_available_request = set_agent_available_req_t{true};
            auto available =
              co_await stream_e2e_client::codecs::request (agent, set_agent_available_request)
                .async<set_agent_available_res_t> ();
            ensure (available.is_available);

            // 4-6. customer connect, authenticate, open conversation.
            trace ("customer connect");
            co_await customer.connect ().async ();
            const auto customer_auth_request = authenticate_req_t{support_chat_tokens_t::customer1};
            auto customer_auth =
              co_await stream_e2e_client::codecs::request (customer, customer_auth_request)
                .async<authenticate_res_t> ();
            ensure (customer_auth.actor_id == support_chat_tokens_t::customer1);
            ensure (customer_auth.role == support_chat_roles_t::customer);

            auto assigned_for_agent = agent.wait_for<conversation_assigned_notify_t> ().to_future (
              "agent conversation assigned notify wait failed");
            auto joined_for_customer = customer.wait_for<participant_joined_notify_t> ().to_future (
              "customer participant joined notify wait failed");
            const auto open_request = open_conversation_req_t{"checkout payment failed"};
            auto opened = co_await stream_e2e_client::codecs::request (customer, open_request)
                            .async<open_conversation_res_t> ();
            ensure (opened.state.customer_actor_id == customer_auth.actor_id);
            ensure (opened.state.status == conversation_statuses_t::waiting_for_agent
                    || opened.state.status == conversation_statuses_t::active);

            // 7-8. customer sees ParticipantJoined, agent sees ConversationAssigned.
            auto joined = joined_for_customer.get ();
            ensure (joined.actor_id == agent_auth.actor_id);
            ensure (joined.conversation_id == opened.conversation_id);
            ensure (joined.role == support_chat_roles_t::agent);
            ensure (joined.state.status == conversation_statuses_t::active);

            auto assigned = assigned_for_agent.get ();
            ensure (assigned.conversation_id == opened.conversation_id);
            ensure (assigned.state.agent_actor_id == agent_auth.actor_id);

            // 13-14. typing change verified on the opposite client.
            auto agent_typing_for_customer =
              customer.wait_for<typing_changed_notify_t> ().to_future (
                "customer typing changed notify wait failed");
            const auto typing_request = set_typing_req_t{opened.conversation_id, true};
            auto typing = co_await stream_e2e_client::codecs::request (agent, typing_request)
                            .async<set_typing_res_t> ();
            ensure (typing.state.status == conversation_statuses_t::active);
            auto agent_typing_push = agent_typing_for_customer.get ();
            ensure (agent_typing_push.actor_id == agent_auth.actor_id);
            ensure (agent_typing_push.is_typing);

            // 9-10. agent greeting; customer receives MessageSeq 1.
            auto agent_message_for_customer =
              customer.wait_for<chat_message_notify_t> ().to_future (
                "customer chat message notify wait failed");
            const auto agent_message_request = send_chat_message_req_t{
              opened.conversation_id, "I can help with a new payment link."};
            auto sent_by_agent =
              co_await stream_e2e_client::codecs::request (agent, agent_message_request)
                .async<send_chat_message_res_t> ();
            ensure (sent_by_agent.message.message_seq == 1ULL);
            ensure (sent_by_agent.state.last_message_seq == 1ULL);
            auto agent_message_push = agent_message_for_customer.get ();
            ensure (agent_message_push.message.sender_actor_id == agent_auth.actor_id);
            ensure (agent_message_push.message.text == "I can help with a new payment link.");

            // 15-16. reconnect the customer with the same token, refresh state.
            trace ("customer reconnect");
            co_await reconnecting_customer.connect ().async ();
            const auto reconnect_auth_request =
              authenticate_req_t{support_chat_tokens_t::customer1};
            auto reconnected = co_await stream_e2e_client::codecs::request (reconnecting_customer,
                                                                            reconnect_auth_request)
                                 .async<authenticate_res_t> ();
            ensure (reconnected.actor_id == customer_auth.actor_id);

            // 11-12. customer reply; agent receives MessageSeq 2.
            auto customer_message_for_agent = agent.wait_for<chat_message_notify_t> ().to_future (
              "agent chat message notify wait failed");
            const auto customer_message_request =
              send_chat_message_req_t{opened.conversation_id, "I cannot complete payment."};
            auto sent_by_customer = co_await stream_e2e_client::codecs::request (
                                      reconnecting_customer, customer_message_request)
                                      .async<send_chat_message_res_t> ();
            ensure (sent_by_customer.message.message_seq == 2ULL);
            auto customer_message_push = customer_message_for_agent.get ();
            ensure (customer_message_push.message.sender_actor_id == customer_auth.actor_id);
            ensure (customer_message_push.message.text == "I cannot complete payment.");

            auto follow_up_for_customer =
              reconnecting_customer.wait_for<chat_message_notify_t> ().to_future (
                "reconnecting customer chat message notify wait failed");
            const auto follow_up_request =
              send_chat_message_req_t{opened.conversation_id, "Please try this replacement link."};
            auto follow_up_by_agent =
              co_await stream_e2e_client::codecs::request (agent, follow_up_request)
                .async<send_chat_message_res_t> ();
            ensure (follow_up_by_agent.message.message_seq == 3ULL);
            auto follow_up_push = follow_up_for_customer.get ();
            ensure (follow_up_push.message.sender_actor_id == agent_auth.actor_id);
            ensure (follow_up_push.state.last_message_seq == 3ULL);

            // 17. idle timeout transitions both clients to WaitingForClose.
            const auto idle_timeout = std::chrono::milliseconds (10000);
            auto idle_for_customer = reconnecting_customer.wait_for<conversation_idle_notify_t> ()
                                       .timeout (idle_timeout)
                                       .to_future ("reconnecting customer idle notify wait failed");
            auto idle_for_agent = agent.wait_for<conversation_idle_notify_t> ()
                                    .timeout (idle_timeout)
                                    .to_future ("agent idle notify wait failed");
            auto idle_customer_push = idle_for_customer.get ();
            ensure (idle_customer_push.conversation_id == opened.conversation_id);
            ensure (idle_customer_push.state.status == conversation_statuses_t::waiting_for_close);
            auto idle_agent_push = idle_for_agent.get ();
            ensure (idle_agent_push.conversation_id == opened.conversation_id);
            ensure (idle_agent_push.state.status == conversation_statuses_t::waiting_for_close);

            // Explicit close: customer closes, agent receives ConversationClosed.
            auto closed_for_agent = agent.wait_for<conversation_closed_notify_t> ().to_future (
              "agent conversation closed notify wait failed");
            const auto close_request = close_conversation_req_t{opened.conversation_id, "resolved"};
            auto closed =
              co_await stream_e2e_client::codecs::request (reconnecting_customer, close_request)
                .async<close_conversation_res_t> ();
            ensure (closed.state.status == conversation_statuses_t::closed);
            auto closed_agent_push = closed_for_agent.get ();
            ensure (closed_agent_push.conversation_id == opened.conversation_id);
            ensure (closed_agent_push.state.status == conversation_statuses_t::closed);

            // 18. closed conversation rejects follow-up messages.
            const auto after_close_request =
              send_chat_message_req_t{opened.conversation_id, "after close"};
            auto after_close =
              stream_e2e_client::codecs::request (reconnecting_customer, after_close_request)
                .async<send_chat_message_res_t> ()
                .result ();
            ensure (!after_close, "Closed conversation must reject follow-up messages.");

            // Agent-unavailable scenario: conversation stays WaitingForAgent.
            trace ("waiting customer");
            co_await waiting_customer.connect ().async ();
            const auto waiting_auth_request = authenticate_req_t{support_chat_tokens_t::customer2};
            auto waiting_auth =
              co_await stream_e2e_client::codecs::request (waiting_customer, waiting_auth_request)
                .async<authenticate_res_t> ();
            ensure (waiting_auth.actor_id == support_chat_tokens_t::customer2);
            const auto no_agent_request = open_conversation_req_t{"agent unavailable"};
            auto no_agent =
              co_await stream_e2e_client::codecs::request (waiting_customer, no_agent_request)
                .async<open_conversation_res_t> ();
            ensure (no_agent.state.status == conversation_statuses_t::waiting_for_agent);
            ensure (no_agent.state.subject == "agent unavailable");
            auto unexpected_close = waiting_customer.wait_for<conversation_closed_notify_t> ()
                                      .timeout (std::chrono::milliseconds (500))
                                      .async ()
                                      .result ();
            ensure (!unexpected_close, "Waiting conversation must not be closed.");

            // Second agent + customer: explicit close end-to-end.
            trace ("explicit close");
            co_await closing_agent.connect ().async ();
            const auto closing_agent_auth_request =
              authenticate_req_t{support_chat_tokens_t::agent2};
            auto closing_agent_auth = co_await stream_e2e_client::codecs::request (
                                        closing_agent, closing_agent_auth_request)
                                        .async<authenticate_res_t> ();
            ensure (closing_agent_auth.actor_id == support_chat_tokens_t::agent2);
            const auto second_available_request = set_agent_available_req_t{true};
            auto second_available =
              co_await stream_e2e_client::codecs::request (closing_agent, second_available_request)
                .async<set_agent_available_res_t> ();
            ensure (second_available.is_available);

            co_await closing_customer.connect ().async ();
            const auto closing_customer_auth_request =
              authenticate_req_t{support_chat_tokens_t::customer3};
            auto closing_customer_auth = co_await stream_e2e_client::codecs::request (
                                           closing_customer, closing_customer_auth_request)
                                           .async<authenticate_res_t> ();
            ensure (closing_customer_auth.actor_id == support_chat_tokens_t::customer3);

            auto explicit_assigned_for_agent =
              closing_agent.wait_for<conversation_assigned_notify_t> ().to_future (
                "closing agent conversation assigned notify wait failed");
            auto explicit_joined_for_customer =
              closing_customer.wait_for<participant_joined_notify_t> ().to_future (
                "closing customer participant joined notify wait failed");
            const auto explicit_open_request = open_conversation_req_t{"explicit close"};
            auto explicit_opened =
              co_await stream_e2e_client::codecs::request (closing_customer, explicit_open_request)
                .async<open_conversation_res_t> ();
            auto explicit_joined_push = explicit_joined_for_customer.get ();
            ensure (explicit_joined_push.actor_id == closing_agent_auth.actor_id);
            ensure (explicit_joined_push.state.status == conversation_statuses_t::active);
            auto explicit_assigned_push = explicit_assigned_for_agent.get ();
            ensure (explicit_assigned_push.state.agent_actor_id == closing_agent_auth.actor_id);

            auto explicit_closed_for_agent =
              closing_agent.wait_for<conversation_closed_notify_t> ().to_future (
                "closing agent conversation closed notify wait failed");
            const auto explicit_close_request =
              close_conversation_req_t{explicit_opened.conversation_id, "done"};
            auto explicit_closed =
              co_await stream_e2e_client::codecs::request (closing_customer, explicit_close_request)
                .async<close_conversation_res_t> ();
            ensure (explicit_closed.state.status == conversation_statuses_t::closed);
            auto explicit_closed_push = explicit_closed_for_agent.get ();
            ensure (explicit_closed_push.conversation_id == explicit_opened.conversation_id);
            ensure (explicit_closed_push.state.status == conversation_statuses_t::closed);

            const auto close_again_request =
              close_conversation_req_t{explicit_opened.conversation_id, "again"};
            auto close_again =
              stream_e2e_client::codecs::request (closing_customer, close_again_request)
                .async<close_conversation_res_t> ()
                .result ();
            ensure (!close_again, "Closed conversation must reject duplicate close.");

            co_await customer.close ().async ();
            co_await agent.close ().async ();
            co_await reconnecting_customer.close ().async ();
            co_await waiting_customer.close ().async ();
            co_await closing_customer.close ().async ();
            co_await closing_agent.close ().async ();
            co_return true;
        }
        catch (const std::exception &ex) {
            std::cerr << "supportchat scenario failed: " << ex.what () << '\n';
            (void) customer.close ();
            (void) agent.close ();
            (void) reconnecting_customer.close ();
            (void) waiting_customer.close ();
            (void) closing_customer.close ();
            (void) closing_agent.close ();
            co_return false;
        }
    }

    static void ensure (bool condition, const char *expression)
    {
        if (!condition) {
            throw std::runtime_error (std::string ("Ensure failed: ") + expression);
        }
    }

    static void ensure (bool condition) { ensure (condition, "condition"); }

    static void trace (const char *step) { std::cerr << "supportchat step: " << step << '\n'; }
};

} // namespace zlink::samples::supportchat
