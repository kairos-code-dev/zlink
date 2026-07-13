/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include "../Shared/Contracts/messages.hpp"

#include <zlink/framework/codecs/json_stream_connector.hpp>
#include <zlink/http_client.hpp>
#include <zlink/stream_connector.hpp>
#include <zlink/stream_e2e_client.hpp>
#include <zlink/stream_e2e_client/codecs/auto_codec.hpp>

#include <algorithm>
#include <chrono>
#include <future>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace zlink::samples::supportchat
{

inline constexpr const char *conversation_id_metadata_key = "ConversationId";

class supportchat_client_scenario_t
{
  public:
    void run (const std::string &support_http_url, const std::string &session_stream_endpoint)
    {
        assert_support_server (support_http_url);
        run_stream_conversation (session_stream_endpoint);

        std::cout << "supportchat authentication=verified" << std::endl;
        std::cout << "supportchat conversation-assignment=verified" << std::endl;
        std::cout << "supportchat bound-push=verified" << std::endl;
        std::cout << "supportchat reconnect=verified" << std::endl;
        std::cout << "supportchat idle-close=verified" << std::endl;
        std::cout << "supportchat=completed" << std::endl;
    }

  private:
    using connector_t = zlink::stream_e2e_client::coroutine_connector_t;

    static zlink::stream_connector::connector_t make_connector (const std::string &endpoint)
    {
        zlink::stream_connector::connector_options_t options;
        options.endpoint = endpoint;
        options.connect_timeout = std::chrono::seconds (5);
        options.request_timeout = std::chrono::seconds (12);
        options.dispatch_mode = zlink::stream_connector::dispatch_mode_t::immediate;
        return zlink::stream_connector::connector_factory_t::create (options);
    }

    static void run_stream_conversation (const std::string &endpoint)
    {
        auto customer_core = make_connector (endpoint);
        auto customer = zlink::stream_e2e_client::use (customer_core);
        auto customer_connected = customer.connect ().submit ();
        expect (static_cast<bool> (customer_connected), "customer stream connect failed");

        auto agent_core = make_connector (endpoint);
        auto agent = zlink::stream_e2e_client::use (agent_core);
        auto agent_connected = agent.connect ().submit ();
        expect (static_cast<bool> (agent_connected), "agent stream connect failed");

        auto customer_auth = request<authenticate_res_t> (
          customer, authenticate_req_t{"customer"}, "customer auth failed");
        auto agent_auth =
          request<authenticate_res_t> (agent, authenticate_req_t{"agent"}, "agent auth failed");
        expect (customer_auth.role == role_t::customer, "customer role mismatch");
        expect (agent_auth.role == role_t::agent, "agent role mismatch");

        auto available = request<set_agent_available_res_t> (
          agent, set_agent_available_req_t{true}, "agent availability failed");
        expect (available.is_available, "agent was not made available");

        auto assigned = wait_assigned_packet (agent);
        auto joined = wait_packet<participant_joined_notify_t> (
          customer, participant_joined_notify_t::packet_name, "participant join wait failed");
        auto agent_message = wait_chat (agent, "Payment keeps failing.");
        auto customer_typing = wait_typing (customer, "agent-1", true);
        auto customer_message = wait_chat (customer, "Please retry your card.");
        auto agent_idle = wait_packet<conversation_idle_notify_t> (
          agent, conversation_idle_notify_t::packet_name, "idle wait failed");
        auto agent_closed = wait_packet<conversation_closed_notify_t> (
          agent, conversation_closed_notify_t::packet_name, "closed wait failed");

        auto opened = request<open_conversation_res_t> (
          customer, open_conversation_req_t{"checkout payment failed"}, "open conversation failed");
        expect (opened.state.agent_actor_id == std::optional<std::string>{"agent-1"},
                "conversation was not assigned");
        expect (assigned.get ().conversation_id == opened.conversation_id,
                "assignment notification mismatch");

        auto agent_joined = request_in_conversation<join_conversation_res_t> (
          agent, opened.conversation_id, join_conversation_req_t{}, "agent join failed");
        expect (agent_joined.state.status == conversation_status_t::active,
                "agent join did not activate conversation");
        expect (joined.get ().actor_id == "agent-1", "participant join notification mismatch");

        auto sent = request_in_conversation<send_chat_message_res_t> (
          customer, opened.conversation_id, send_chat_message_req_t{"Payment keeps failing."},
          "customer message failed");
        expect (sent.message.message_seq == 1, "first message sequence mismatch");
        expect (agent_message.get ().message.text == "Payment keeps failing.",
                "agent did not receive customer message");

        agent.send (set_typing_req_t{true})
          .metadata (conversation_id_metadata_key, opened.conversation_id)
          .submit ();
        expect (customer_typing.get ().is_typing, "customer did not receive typing notification");

        auto reply = request_in_conversation<send_chat_message_res_t> (
          agent, opened.conversation_id, send_chat_message_req_t{"Please retry your card."},
          "agent message failed");
        expect (reply.message.message_seq == 2, "second message sequence mismatch");
        expect (customer_message.get ().message.text == "Please retry your card.",
                "customer did not receive agent message");

        auto closed = request_in_conversation<close_conversation_res_t> (
          customer, opened.conversation_id, close_conversation_req_t{std::string ("idle")},
          "close failed");
        expect (closed.state.status == conversation_status_t::closed,
                "conversation did not close");
        expect (agent_idle.get ().state.status == conversation_status_t::waiting_for_close,
                "agent did not receive idle notification");
        expect (agent_closed.get ().state.status == conversation_status_t::closed,
                "agent did not receive closed notification");
    }

    static void assert_support_server (const std::string &support_http_url)
    {
        auto client = zlink::http_client::client_t::create ()
                        .base_url (support_http_url)
                        .timeout (std::chrono::seconds (30))
                        .build ();
        const auto assertion =
          client.post ("/self-check/assert")
            .body (supportchat_server_assertion_req_t {})
            .fetch<supportchat_server_assertion_res_t> ();
        expect (assertion.ok, "support server self-check failed");
        require_evidence (assertion.evidence, "agent-availability=registered");
        require_evidence (assertion.evidence, "one-agent-many-conversations=verified");
        require_evidence (assertion.evidence, "conversation-sequence=verified");
        require_evidence (assertion.evidence, "typing-one-way=verified");
        require_evidence (assertion.evidence, "reconnect-state=verified");
        require_evidence (assertion.evidence, "explicit-close=verified");
        require_evidence (assertion.evidence, "idle-close=verified");
        require_evidence (assertion.evidence, "no-agent-waiting=verified");
    }

    template <typename TReply, typename TRequest>
    static TReply request (connector_t &connector, const TRequest &request, const char *message)
    {
        auto reply = connector.request (request).template async<TReply> ().result ();
        if (!reply) {
            throw std::runtime_error (reply.error () ? reply.error ()->message : message);
        }
        return reply.value ();
    }

    template <typename TReply, typename TRequest>
    static TReply request_in_conversation (connector_t &connector,
                                           const std::string &conversation_id,
                                           const TRequest &request,
                                           const char *message)
    {
        auto reply = connector.request (request)
                       .metadata (conversation_id_metadata_key, conversation_id)
                       .template async<TReply> ()
                       .result ();
        if (!reply) {
            throw std::runtime_error (reply.error () ? reply.error ()->message : message);
        }
        return reply.value ();
    }

    static std::future<conversation_assigned_notify_t> wait_assigned_packet (connector_t &agent)
    {
        return std::async (std::launch::async, [&agent] {
            auto packet = agent
                            .wait_for<zlink::stream_connector::packet_t> (
                              conversation_assigned_notify_t::packet_name)
                            .timeout (std::chrono::seconds (12))
                            .to_future ("assignment packet wait failed")
                            .get ();
            return packet.payload.parse_json<conversation_assigned_notify_t> ();
        });
    }

    template <typename TMessage>
    static std::future<TMessage> wait_packet (connector_t &connector,
                                              const std::string &packet_name,
                                              const char *failure_message)
    {
        return std::async (std::launch::async, [&connector, packet_name, failure_message] {
            auto packet = connector.wait_for<zlink::stream_connector::packet_t> (packet_name)
                            .timeout (std::chrono::seconds (12))
                            .to_future (failure_message)
                            .get ();
            return packet.payload.parse_json<TMessage> ();
        });
    }

    static std::future<chat_message_notify_t> wait_chat (connector_t &connector,
                                                         const std::string &text)
    {
        return std::async (std::launch::async, [&connector, text] {
            auto message = wait_packet<chat_message_notify_t> (
                             connector, chat_message_notify_t::packet_name,
                             "chat message wait failed")
                             .get ();
            if (message.message.text != text) {
                throw std::runtime_error ("chat message payload mismatch");
            }
            return message;
        });
    }

    static std::future<typing_changed_notify_t>
    wait_typing (connector_t &connector, const std::string &actor_id, bool is_typing)
    {
        return std::async (std::launch::async, [&connector, actor_id, is_typing] {
            auto message = wait_packet<typing_changed_notify_t> (
                             connector, typing_changed_notify_t::packet_name,
                             "typing wait failed")
                             .get ();
            if (message.actor_id != actor_id || message.is_typing != is_typing) {
                throw std::runtime_error ("typing payload mismatch");
            }
            return message;
        });
    }

    static void require_evidence (const std::vector<std::string> &evidence,
                                  const std::string &expected)
    {
        const auto found = std::find (evidence.begin (), evidence.end (), expected);
        expect (found != evidence.end (), "missing support evidence: " + expected);
    }

    static void expect (bool condition, const std::string &message)
    {
        if (!condition) {
            throw std::runtime_error (message);
        }
    }
};

} // namespace zlink::samples::supportchat
