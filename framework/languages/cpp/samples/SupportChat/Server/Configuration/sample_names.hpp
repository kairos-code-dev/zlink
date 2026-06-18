/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

namespace zlink::samples::supportchat
{

struct sample_names_t
{
    static constexpr const char *api_channel = "supportchat.api";
    static constexpr const char *support_channel = "supportchat.support";
    static constexpr const char *support_actor_type = "supportchat.user";
    static constexpr const char *stream_node = "supportchat.client.stream";
    static constexpr const char *session_spot_node = "supportchat.session.node";
    static constexpr const char *support_spot_node = "supportchat.support.node";
    static constexpr const char *conversation_spot_node = "supportchat.conversation.node";
    static constexpr const char *support_spot_discovery = "supportchat.support.spots";
    static constexpr const char *conversation_spot = "supportchat.conversation";
    static constexpr const char *notification_channel = "supportchat.notifications";

    static constexpr const char *participant_joined_packet = "ParticipantJoinedNotify";
    static constexpr const char *conversation_assigned_packet = "ConversationAssignedNotify";
    static constexpr const char *chat_message_packet = "ChatMessageNotify";
    static constexpr const char *typing_changed_packet = "TypingChangedNotify";
    static constexpr const char *conversation_idle_packet = "ConversationIdleNotify";
    static constexpr const char *conversation_closed_packet = "ConversationClosedNotify";
};

} // namespace zlink::samples::supportchat
