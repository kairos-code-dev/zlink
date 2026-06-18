/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../../../Shared/Contracts/messages.hpp"

#include <optional>
#include <string>
#include <vector>

namespace zlink::samples::supportchat
{

struct conversation_policy_t
{
    long long idle_timeout_ms = 3000;
    long long close_grace_timeout_ms = 2000;
    int max_message_length = 500;

    static conversation_policy_t sample () { return conversation_policy_t{}; }
};

struct conversation_participant_t
{
    std::string actor_id;
    std::string role;
    std::string display_name;
    long long joined_at_unix_ms = 0;
    bool is_typing = false;
};

struct conversation_message_t
{
    std::string conversation_id;
    unsigned long long message_seq = 0;
    std::string sender_actor_id;
    std::string text;
    long long sent_at_unix_ms = 0;
};

enum class conversation_event_kind_t
{
    participant_joined,
    assigned,
    message_appended,
    typing_changed,
    idle,
    closed,
};

struct conversation_event_t
{
    conversation_event_kind_t kind;
    conversation_state_t state;
    std::string actor_id;
    std::string role;
    std::optional<chat_message_t> message;
    std::optional<bool> is_typing;
};

struct conversation_change_t
{
    conversation_state_t state;
    std::vector<conversation_event_t> events;
};

} // namespace zlink::samples::supportchat
