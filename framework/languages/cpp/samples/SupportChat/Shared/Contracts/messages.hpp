/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/Contracts/Messaging/message.hpp>

#include <array>
#include <cstdint>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

namespace zlink::samples::supportchat
{

struct support_chat_roles_t
{
    static constexpr const char *customer = "Customer";
    static constexpr const char *agent = "Agent";
};

struct conversation_statuses_t
{
    static constexpr const char *waiting_for_agent = "WaitingForAgent";
    static constexpr const char *active = "Active";
    static constexpr const char *waiting_for_close = "WaitingForClose";
    static constexpr const char *closed = "Closed";
};

struct support_chat_tokens_t
{
    static constexpr const char *customer1 = "customer-1";
    static constexpr const char *customer2 = "customer-2";
    static constexpr const char *customer3 = "customer-3";
    static constexpr const char *agent1 = "agent-1";
    static constexpr const char *agent2 = "agent-2";
};

// --- client stream authentication messages ---

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

// --- API authentication and actor preparation messages ---

struct authenticate_user_req_t
{
    static constexpr const char *packet_name = "AuthenticateUserReq";
    std::string access_token;
};

struct authenticate_user_res_t
{
    static constexpr const char *packet_name = "AuthenticateUserRes";
    bool accepted = false;
    std::string actor_id;
    std::string display_name;
    std::string role;
    std::string reason;
};

struct actor_ref_snapshot_t
{
    std::array<unsigned char, 16> node_rid{};
    std::string actor_id;
    unsigned long long generation = 0;
};

struct ensure_support_user_actor_req_t
{
    static constexpr const char *packet_name = "EnsureSupportUserActorReq";
    std::string actor_id;
    std::string display_name;
    std::string role;
};

struct ensure_support_user_actor_res_t
{
    static constexpr const char *packet_name = "EnsureSupportUserActorRes";
    actor_ref_snapshot_t actor;
    std::string actor_type;
};

// --- session relay envelope (mirrors the Bingo gateway relay contract) ---

struct remote_actor_packet_req_t
{
    static constexpr const char *packet_name = "RemoteActorPacketReq";
    std::string actor_node_rid;
    std::string actor_type;
    std::string actor_id;
    unsigned long long actor_generation = 0;
    int header_kind = 0;
    int header_codec = 0;
    int header_flags = 0;
    bool request_seq_present = false;
    unsigned long long request_seq = 0;
    std::string relayed_packet_name;
    std::map<std::string, std::string> metadata;
    std::vector<unsigned char> payload;
};

struct remote_actor_packet_res_t
{
    static constexpr const char *packet_name = "RemoteActorPacketRes";
    bool actor_ref_present = false;
    std::string actor_node_rid;
    std::string actor_type;
    std::string actor_id;
    unsigned long long actor_generation = 0;
    bool has_reply = false;
    std::vector<unsigned char> reply_payload;
};

// --- API orchestration and Support allocation messages ---

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

struct assign_agent_req_t
{
    static constexpr const char *packet_name = "AssignAgentReq";
    std::string conversation_id;
    std::string requested_agent_actor_id;
};

struct assign_agent_res_t
{
    static constexpr const char *packet_name = "AssignAgentRes";
    std::string conversation_id;
    std::string status;
    std::string agent_actor_id;
};

// --- conversation state model ---

struct conversation_state_t
{
    std::string conversation_id;
    std::string subject;
    std::string status = conversation_statuses_t::waiting_for_agent;
    std::string customer_actor_id;
    std::string agent_actor_id;
    unsigned long long last_message_seq = 0;
    long long last_message_at_unix_ms = 0;
    bool has_last_message_at = false;
    long long idle_deadline_unix_ms = 0;
    bool has_idle_deadline = false;
};

struct chat_message_t
{
    std::string conversation_id;
    unsigned long long message_seq = 0;
    std::string sender_actor_id;
    std::string text;
    long long sent_at_unix_ms = 0;
};

// --- client stream request/response ---

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
    bool is_available = false;
};

struct set_agent_available_res_t
{
    static constexpr const char *packet_name = "SetAgentAvailableRes";
    bool is_available = false;
};

struct join_conversation_req_t
{
    static constexpr const char *packet_name = "JoinConversationReq";
    std::string conversation_id;
};

struct join_conversation_res_t
{
    static constexpr const char *packet_name = "JoinConversationRes";
    conversation_state_t state;
};

struct send_chat_message_req_t
{
    static constexpr const char *packet_name = "SendChatMessageReq";
    std::string conversation_id;
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
    std::string conversation_id;
    bool is_typing = false;
};

struct set_typing_res_t
{
    static constexpr const char *packet_name = "SetTypingRes";
    conversation_state_t state;
};

struct close_conversation_req_t
{
    static constexpr const char *packet_name = "CloseConversationReq";
    std::string conversation_id;
    std::string reason;
};

struct close_conversation_res_t
{
    static constexpr const char *packet_name = "CloseConversationRes";
    conversation_state_t state;
};

// --- server push messages ---

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
    bool is_typing = false;
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

// --- conversation create request (Support internal) ---

struct conversation_create_request_t
{
    std::string customer_actor_id;
    std::string customer_display_name;
    std::string subject;
    long long created_at_unix_ms = 0;
};

// --- JSON serialization (the DTO serializer hook boundary) ---

inline void to_json (nlohmann::json &json, const authenticate_req_t &value)
{
    json = {{"accessToken", value.access_token}};
}

inline void from_json (const nlohmann::json &json, authenticate_req_t &value)
{
    value.access_token = json.value ("accessToken", "");
}

inline void to_json (nlohmann::json &json, const authenticate_res_t &value)
{
    json = {{"actorId", value.actor_id},
            {"displayName", value.display_name},
            {"role", value.role}};
}

inline void from_json (const nlohmann::json &json, authenticate_res_t &value)
{
    value.actor_id = json.value ("actorId", "");
    value.display_name = json.value ("displayName", "");
    value.role = json.value ("role", "");
}

inline void to_json (nlohmann::json &json, const authenticate_user_req_t &value)
{
    json = {{"accessToken", value.access_token}};
}

inline void from_json (const nlohmann::json &json, authenticate_user_req_t &value)
{
    value.access_token = json.value ("accessToken", "");
}

inline void to_json (nlohmann::json &json, const authenticate_user_res_t &value)
{
    json = {{"accepted", value.accepted},
            {"actorId", value.actor_id},
            {"displayName", value.display_name},
            {"role", value.role},
            {"reason", value.reason}};
}

inline void from_json (const nlohmann::json &json, authenticate_user_res_t &value)
{
    value.accepted = json.value ("accepted", false);
    value.actor_id = json.value ("actorId", "");
    value.display_name = json.value ("displayName", "");
    value.role = json.value ("role", "");
    value.reason = json.value ("reason", "");
}

inline void to_json (nlohmann::json &json, const actor_ref_snapshot_t &value)
{
    json = {
      {"nodeRid", value.node_rid}, {"actorId", value.actor_id}, {"generation", value.generation}};
}

inline void from_json (const nlohmann::json &json, actor_ref_snapshot_t &value)
{
    value.node_rid = json.value ("nodeRid", std::array<unsigned char, 16>{});
    value.actor_id = json.value ("actorId", "");
    value.generation = json.value ("generation", 0ULL);
}

inline void to_json (nlohmann::json &json, const ensure_support_user_actor_req_t &value)
{
    json = {{"actorId", value.actor_id},
            {"displayName", value.display_name},
            {"role", value.role}};
}

inline void from_json (const nlohmann::json &json, ensure_support_user_actor_req_t &value)
{
    value.actor_id = json.value ("actorId", "");
    value.display_name = json.value ("displayName", "");
    value.role = json.value ("role", "");
}

inline void to_json (nlohmann::json &json, const ensure_support_user_actor_res_t &value)
{
    json = {{"actor", value.actor}, {"actorType", value.actor_type}};
}

inline void from_json (const nlohmann::json &json, ensure_support_user_actor_res_t &value)
{
    value.actor = json.value ("actor", actor_ref_snapshot_t{});
    value.actor_type = json.value ("actorType", "");
}

inline void to_json (nlohmann::json &json, const remote_actor_packet_req_t &value)
{
    json = {{"actorNodeRid", value.actor_node_rid},
            {"actorType", value.actor_type},
            {"actorId", value.actor_id},
            {"actorGeneration", value.actor_generation},
            {"headerKind", value.header_kind},
            {"headerCodec", value.header_codec},
            {"headerFlags", value.header_flags},
            {"requestSeqPresent", value.request_seq_present},
            {"requestSeq", value.request_seq},
            {"packetName", value.relayed_packet_name},
            {"metadata", value.metadata},
            {"payload", value.payload}};
}

inline void from_json (const nlohmann::json &json, remote_actor_packet_req_t &value)
{
    value.actor_node_rid = json.value ("actorNodeRid", "");
    value.actor_type = json.value ("actorType", "");
    value.actor_id = json.value ("actorId", "");
    value.actor_generation = json.value ("actorGeneration", 0ULL);
    value.header_kind = json.value ("headerKind", 0);
    value.header_codec = json.value ("headerCodec", 0);
    value.header_flags = json.value ("headerFlags", 0);
    value.request_seq_present = json.value ("requestSeqPresent", false);
    value.request_seq = json.value ("requestSeq", 0ULL);
    value.relayed_packet_name = json.value ("packetName", "");
    value.metadata = json.value ("metadata", std::map<std::string, std::string>{});
    value.payload = json.value ("payload", std::vector<unsigned char>{});
}

inline void to_json (nlohmann::json &json, const remote_actor_packet_res_t &value)
{
    json = {{"actorRefPresent", value.actor_ref_present},
            {"actorNodeRid", value.actor_node_rid},
            {"actorType", value.actor_type},
            {"actorId", value.actor_id},
            {"actorGeneration", value.actor_generation},
            {"hasReply", value.has_reply},
            {"replyPayload", value.reply_payload}};
}

inline void from_json (const nlohmann::json &json, remote_actor_packet_res_t &value)
{
    value.actor_ref_present = json.value ("actorRefPresent", false);
    value.actor_node_rid = json.value ("actorNodeRid", "");
    value.actor_type = json.value ("actorType", "");
    value.actor_id = json.value ("actorId", "");
    value.actor_generation = json.value ("actorGeneration", 0ULL);
    value.has_reply = json.value ("hasReply", false);
    value.reply_payload = json.value ("replyPayload", std::vector<unsigned char>{});
}

inline void to_json (nlohmann::json &json, const open_conversation_api_req_t &value)
{
    json = {{"customerActorId", value.customer_actor_id},
            {"customerDisplayName", value.customer_display_name},
            {"subject", value.subject}};
}

inline void from_json (const nlohmann::json &json, open_conversation_api_req_t &value)
{
    value.customer_actor_id = json.value ("customerActorId", "");
    value.customer_display_name = json.value ("customerDisplayName", "");
    value.subject = json.value ("subject", "");
}

inline void to_json (nlohmann::json &json, const open_conversation_api_res_t &value)
{
    json = {{"conversationId", value.conversation_id}, {"status", value.status}};
}

inline void from_json (const nlohmann::json &json, open_conversation_api_res_t &value)
{
    value.conversation_id = json.value ("conversationId", "");
    value.status = json.value ("status", "");
}

inline void to_json (nlohmann::json &json, const allocate_conversation_req_t &value)
{
    json = {{"customerActorId", value.customer_actor_id},
            {"customerDisplayName", value.customer_display_name},
            {"subject", value.subject}};
}

inline void from_json (const nlohmann::json &json, allocate_conversation_req_t &value)
{
    value.customer_actor_id = json.value ("customerActorId", "");
    value.customer_display_name = json.value ("customerDisplayName", "");
    value.subject = json.value ("subject", "");
}

inline void to_json (nlohmann::json &json, const allocate_conversation_res_t &value)
{
    json = {{"conversationId", value.conversation_id}, {"status", value.status}};
}

inline void from_json (const nlohmann::json &json, allocate_conversation_res_t &value)
{
    value.conversation_id = json.value ("conversationId", "");
    value.status = json.value ("status", "");
}

inline void to_json (nlohmann::json &json, const assign_agent_req_t &value)
{
    json = {{"conversationId", value.conversation_id},
            {"requestedAgentActorId", value.requested_agent_actor_id}};
}

inline void from_json (const nlohmann::json &json, assign_agent_req_t &value)
{
    value.conversation_id = json.value ("conversationId", "");
    value.requested_agent_actor_id = json.value ("requestedAgentActorId", "");
}

inline void to_json (nlohmann::json &json, const assign_agent_res_t &value)
{
    json = {{"conversationId", value.conversation_id},
            {"status", value.status},
            {"agentActorId", value.agent_actor_id}};
}

inline void from_json (const nlohmann::json &json, assign_agent_res_t &value)
{
    value.conversation_id = json.value ("conversationId", "");
    value.status = json.value ("status", "");
    value.agent_actor_id = json.value ("agentActorId", "");
}

inline void to_json (nlohmann::json &json, const conversation_state_t &value)
{
    json = {{"conversationId", value.conversation_id},
            {"subject", value.subject},
            {"status", value.status},
            {"customerActorId", value.customer_actor_id},
            {"agentActorId", value.agent_actor_id},
            {"lastMessageSeq", value.last_message_seq},
            {"lastMessageAtUnixMs",
             value.has_last_message_at ? nlohmann::json (value.last_message_at_unix_ms)
                                       : nlohmann::json (nullptr)},
            {"idleDeadlineUnixMs",
             value.has_idle_deadline ? nlohmann::json (value.idle_deadline_unix_ms)
                                     : nlohmann::json (nullptr)}};
}

inline void from_json (const nlohmann::json &json, conversation_state_t &value)
{
    value.conversation_id = json.value ("conversationId", "");
    value.subject = json.value ("subject", "");
    value.status = json.value ("status", "");
    value.customer_actor_id = json.value ("customerActorId", "");
    value.agent_actor_id = json.value ("agentActorId", "");
    value.last_message_seq = json.value ("lastMessageSeq", 0ULL);
    const auto last_at = json.value ("lastMessageAtUnixMs", nlohmann::json (nullptr));
    value.has_last_message_at = !last_at.is_null ();
    value.last_message_at_unix_ms = value.has_last_message_at ? last_at.get<long long> () : 0;
    const auto idle = json.value ("idleDeadlineUnixMs", nlohmann::json (nullptr));
    value.has_idle_deadline = !idle.is_null ();
    value.idle_deadline_unix_ms = value.has_idle_deadline ? idle.get<long long> () : 0;
}

inline void to_json (nlohmann::json &json, const chat_message_t &value)
{
    json = {{"conversationId", value.conversation_id},
            {"messageSeq", value.message_seq},
            {"senderActorId", value.sender_actor_id},
            {"text", value.text},
            {"sentAtUnixMs", value.sent_at_unix_ms}};
}

inline void from_json (const nlohmann::json &json, chat_message_t &value)
{
    value.conversation_id = json.value ("conversationId", "");
    value.message_seq = json.value ("messageSeq", 0ULL);
    value.sender_actor_id = json.value ("senderActorId", "");
    value.text = json.value ("text", "");
    value.sent_at_unix_ms = json.value ("sentAtUnixMs", 0LL);
}

inline void to_json (nlohmann::json &json, const open_conversation_req_t &value)
{
    json = {{"subject", value.subject}};
}

inline void from_json (const nlohmann::json &json, open_conversation_req_t &value)
{
    value.subject = json.value ("subject", "");
}

inline void to_json (nlohmann::json &json, const open_conversation_res_t &value)
{
    json = {{"conversationId", value.conversation_id}, {"state", value.state}};
}

inline void from_json (const nlohmann::json &json, open_conversation_res_t &value)
{
    value.conversation_id = json.value ("conversationId", "");
    value.state = json.value ("state", conversation_state_t{});
}

inline void to_json (nlohmann::json &json, const set_agent_available_req_t &value)
{
    json = {{"isAvailable", value.is_available}};
}

inline void from_json (const nlohmann::json &json, set_agent_available_req_t &value)
{
    value.is_available = json.value ("isAvailable", false);
}

inline void to_json (nlohmann::json &json, const set_agent_available_res_t &value)
{
    json = {{"isAvailable", value.is_available}};
}

inline void from_json (const nlohmann::json &json, set_agent_available_res_t &value)
{
    value.is_available = json.value ("isAvailable", false);
}

inline void to_json (nlohmann::json &json, const join_conversation_req_t &value)
{
    json = {{"conversationId", value.conversation_id}};
}

inline void from_json (const nlohmann::json &json, join_conversation_req_t &value)
{
    value.conversation_id = json.value ("conversationId", "");
}

inline void to_json (nlohmann::json &json, const join_conversation_res_t &value)
{
    json = {{"state", value.state}};
}

inline void from_json (const nlohmann::json &json, join_conversation_res_t &value)
{
    value.state = json.value ("state", conversation_state_t{});
}

inline void to_json (nlohmann::json &json, const send_chat_message_req_t &value)
{
    json = {{"conversationId", value.conversation_id}, {"text", value.text}};
}

inline void from_json (const nlohmann::json &json, send_chat_message_req_t &value)
{
    value.conversation_id = json.value ("conversationId", "");
    value.text = json.value ("text", "");
}

inline void to_json (nlohmann::json &json, const send_chat_message_res_t &value)
{
    json = {{"message", value.message}, {"state", value.state}};
}

inline void from_json (const nlohmann::json &json, send_chat_message_res_t &value)
{
    value.message = json.value ("message", chat_message_t{});
    value.state = json.value ("state", conversation_state_t{});
}

inline void to_json (nlohmann::json &json, const set_typing_req_t &value)
{
    json = {{"conversationId", value.conversation_id}, {"isTyping", value.is_typing}};
}

inline void from_json (const nlohmann::json &json, set_typing_req_t &value)
{
    value.conversation_id = json.value ("conversationId", "");
    value.is_typing = json.value ("isTyping", false);
}

inline void to_json (nlohmann::json &json, const set_typing_res_t &value)
{
    json = {{"state", value.state}};
}

inline void from_json (const nlohmann::json &json, set_typing_res_t &value)
{
    value.state = json.value ("state", conversation_state_t{});
}

inline void to_json (nlohmann::json &json, const close_conversation_req_t &value)
{
    json = {{"conversationId", value.conversation_id}, {"reason", value.reason}};
}

inline void from_json (const nlohmann::json &json, close_conversation_req_t &value)
{
    value.conversation_id = json.value ("conversationId", "");
    value.reason = json.value ("reason", "");
}

inline void to_json (nlohmann::json &json, const close_conversation_res_t &value)
{
    json = {{"state", value.state}};
}

inline void from_json (const nlohmann::json &json, close_conversation_res_t &value)
{
    value.state = json.value ("state", conversation_state_t{});
}

inline void to_json (nlohmann::json &json, const participant_joined_notify_t &value)
{
    json = {{"conversationId", value.conversation_id},
            {"actorId", value.actor_id},
            {"role", value.role},
            {"state", value.state}};
}

inline void from_json (const nlohmann::json &json, participant_joined_notify_t &value)
{
    value.conversation_id = json.value ("conversationId", "");
    value.actor_id = json.value ("actorId", "");
    value.role = json.value ("role", "");
    value.state = json.value ("state", conversation_state_t{});
}

inline void to_json (nlohmann::json &json, const conversation_assigned_notify_t &value)
{
    json = {{"conversationId", value.conversation_id}, {"state", value.state}};
}

inline void from_json (const nlohmann::json &json, conversation_assigned_notify_t &value)
{
    value.conversation_id = json.value ("conversationId", "");
    value.state = json.value ("state", conversation_state_t{});
}

inline void to_json (nlohmann::json &json, const chat_message_notify_t &value)
{
    json = {{"conversationId", value.conversation_id},
            {"message", value.message},
            {"state", value.state}};
}

inline void from_json (const nlohmann::json &json, chat_message_notify_t &value)
{
    value.conversation_id = json.value ("conversationId", "");
    value.message = json.value ("message", chat_message_t{});
    value.state = json.value ("state", conversation_state_t{});
}

inline void to_json (nlohmann::json &json, const typing_changed_notify_t &value)
{
    json = {{"conversationId", value.conversation_id},
            {"actorId", value.actor_id},
            {"isTyping", value.is_typing},
            {"state", value.state}};
}

inline void from_json (const nlohmann::json &json, typing_changed_notify_t &value)
{
    value.conversation_id = json.value ("conversationId", "");
    value.actor_id = json.value ("actorId", "");
    value.is_typing = json.value ("isTyping", false);
    value.state = json.value ("state", conversation_state_t{});
}

inline void to_json (nlohmann::json &json, const conversation_idle_notify_t &value)
{
    json = {{"conversationId", value.conversation_id}, {"state", value.state}};
}

inline void from_json (const nlohmann::json &json, conversation_idle_notify_t &value)
{
    value.conversation_id = json.value ("conversationId", "");
    value.state = json.value ("state", conversation_state_t{});
}

inline void to_json (nlohmann::json &json, const conversation_closed_notify_t &value)
{
    json = {{"conversationId", value.conversation_id}, {"state", value.state}};
}

inline void from_json (const nlohmann::json &json, conversation_closed_notify_t &value)
{
    value.conversation_id = json.value ("conversationId", "");
    value.state = json.value ("state", conversation_state_t{});
}

inline void to_json (nlohmann::json &json, const conversation_create_request_t &value)
{
    json = {{"customerActorId", value.customer_actor_id},
            {"customerDisplayName", value.customer_display_name},
            {"subject", value.subject},
            {"createdAtUnixMs", value.created_at_unix_ms}};
}

inline void from_json (const nlohmann::json &json, conversation_create_request_t &value)
{
    value.customer_actor_id = json.value ("customerActorId", "");
    value.customer_display_name = json.value ("customerDisplayName", "");
    value.subject = json.value ("subject", "");
    value.created_at_unix_ms = json.value ("createdAtUnixMs", 0LL);
}

// --- JSON stream payload helpers (raw lifecycle boundary) ---

template <typename T> inline zlink::message_t to_stream_payload (const T &value)
{
    return zlink::message_t::from (nlohmann::json (value).dump ());
}

template <typename T> inline void from_stream_payload (const zlink::message_t &payload, T &value)
{
    value = nlohmann::json::parse (payload.to_string ()).template get<T> ();
}

} // namespace zlink::samples::supportchat
