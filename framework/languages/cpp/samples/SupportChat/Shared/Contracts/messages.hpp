/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace zlink::samples::supportchat
{

struct role_t
{
    static constexpr const char *customer = "Customer";
    static constexpr const char *agent = "Agent";
};

struct conversation_status_t
{
    static constexpr const char *waiting_for_agent = "WaitingForAgent";
    static constexpr const char *active = "Active";
    static constexpr const char *waiting_for_close = "WaitingForClose";
    static constexpr const char *closed = "Closed";
};

struct chat_message_t
{
    std::string conversation_id;
    std::uint64_t message_seq{0};
    std::string sender_actor_id;
    std::string text;
    std::int64_t sent_at_unix_ms{0};
};

struct conversation_state_t
{
    std::string conversation_id;
    std::string subject;
    std::string status;
    std::string customer_actor_id;
    std::optional<std::string> agent_actor_id;
    std::uint64_t last_message_seq{0};
    std::optional<std::int64_t> last_message_at_unix_ms;
    std::optional<std::int64_t> idle_deadline_unix_ms;
};

struct authenticate_req_t
{
    static constexpr const char *packet_name = "AuthenticateReq";
    std::string access_token;
};

struct authenticate_res_t
{
    static constexpr const char *packet_name = "AuthenticateRes";
    std::string actor_id;
    std::string display_name;
    std::string role;
};

struct authenticate_user_req_t
{
    static constexpr const char *packet_name = "AuthenticateUserReq";
    std::string access_token;
};

struct authenticate_user_res_t
{
    static constexpr const char *packet_name = "AuthenticateUserRes";
    bool accepted{false};
    std::optional<std::string> actor_id;
    std::optional<std::string> display_name;
    std::optional<std::string> role;
    std::optional<std::string> reason;
};

struct open_conversation_api_req_t
{
    static constexpr const char *packet_name = "OpenConversationApiReq";
    std::string customer_actor_id;
    std::string customer_display_name;
    std::string subject;
};

struct open_conversation_api_res_t
{
    static constexpr const char *packet_name = "OpenConversationApiRes";
    std::string conversation_id;
    std::string status;
};

struct allocate_conversation_req_t
{
    static constexpr const char *packet_name = "AllocateConversationReq";
    std::string customer_actor_id;
    std::string customer_display_name;
    std::string subject;
};

struct allocate_conversation_res_t
{
    static constexpr const char *packet_name = "AllocateConversationRes";
    std::string conversation_id;
    std::string status;
};

struct open_conversation_req_t
{
    static constexpr const char *packet_name = "OpenConversationReq";
    std::string subject;
};

struct open_conversation_res_t
{
    static constexpr const char *packet_name = "OpenConversationRes";
    std::string conversation_id;
    conversation_state_t state;
};

struct set_agent_available_req_t
{
    static constexpr const char *packet_name = "SetAgentAvailableReq";
    bool is_available{false};
};

struct set_agent_available_res_t
{
    static constexpr const char *packet_name = "SetAgentAvailableRes";
    bool is_available{false};
};

struct join_conversation_req_t
{
    static constexpr const char *packet_name = "JoinConversationReq";
};

struct join_conversation_res_t
{
    static constexpr const char *packet_name = "JoinConversationRes";
    conversation_state_t state;
};

struct send_chat_message_req_t
{
    static constexpr const char *packet_name = "SendChatMessageReq";
    std::string text;
};

struct send_chat_message_res_t
{
    static constexpr const char *packet_name = "SendChatMessageRes";
    chat_message_t message;
    conversation_state_t state;
};

struct set_typing_req_t
{
    static constexpr const char *packet_name = "SetTypingReq";
    bool is_typing{false};
};

struct close_conversation_req_t
{
    static constexpr const char *packet_name = "CloseConversationReq";
    std::optional<std::string> reason;
};

struct close_conversation_res_t
{
    static constexpr const char *packet_name = "CloseConversationRes";
    conversation_state_t state;
};

struct participant_joined_notify_t
{
    static constexpr const char *packet_name = "ParticipantJoinedNotify";
    std::string conversation_id;
    std::string actor_id;
    std::string role;
    conversation_state_t state;
};

struct conversation_assigned_notify_t
{
    static constexpr const char *packet_name = "ConversationAssignedNotify";
    std::string conversation_id;
    conversation_state_t state;
};

struct chat_message_notify_t
{
    static constexpr const char *packet_name = "ChatMessageNotify";
    std::string conversation_id;
    chat_message_t message;
    conversation_state_t state;
};

struct typing_changed_notify_t
{
    static constexpr const char *packet_name = "TypingChangedNotify";
    std::string conversation_id;
    std::string actor_id;
    bool is_typing{false};
    conversation_state_t state;
};

struct conversation_idle_notify_t
{
    static constexpr const char *packet_name = "ConversationIdleNotify";
    std::string conversation_id;
    conversation_state_t state;
};

struct conversation_closed_notify_t
{
    static constexpr const char *packet_name = "ConversationClosedNotify";
    std::string conversation_id;
    conversation_state_t state;
};

} // namespace zlink::samples::supportchat
