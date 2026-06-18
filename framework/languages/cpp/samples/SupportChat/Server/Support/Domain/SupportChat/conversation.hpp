/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "conversation_models.hpp"

#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace zlink::samples::supportchat
{

// Conversation is the aggregate root. It owns status transitions, participant
// membership, message sequencing, typing state, idle and close decisions, and
// returns domain events for the adapter layer to translate into push messages.
// It never references ZLink framework types.
class conversation_t
{
  public:
    conversation_t (std::string conversation_id,
                    std::string subject,
                    std::string customer_actor_id,
                    std::string customer_display_name,
                    long long created_at_unix_ms,
                    conversation_policy_t policy = conversation_policy_t::sample ()) :
        _conversation_id (std::move (conversation_id)),
        _subject (std::move (subject)),
        _customer_actor_id (std::move (customer_actor_id)),
        _policy (policy)
    {
        if (_subject.empty ()) {
            throw std::runtime_error ("Conversation subject is required.");
        }
        _status = conversation_statuses_t::waiting_for_agent;
        _participants[_customer_actor_id] = conversation_participant_t{
          _customer_actor_id, support_chat_roles_t::customer, std::move (customer_display_name),
          created_at_unix_ms, false};
    }

    const std::string &conversation_id () const noexcept { return _conversation_id; }
    const std::string &subject () const noexcept { return _subject; }
    const std::string &status () const noexcept { return _status; }
    const std::string &customer_actor_id () const noexcept { return _customer_actor_id; }
    const std::string &agent_actor_id () const noexcept { return _agent_actor_id; }

    conversation_state_t snapshot () const
    {
        conversation_state_t state;
        state.conversation_id = _conversation_id;
        state.subject = _subject;
        state.status = _status;
        state.customer_actor_id = _customer_actor_id;
        state.agent_actor_id = _agent_actor_id;
        state.last_message_seq = _last_message_seq;
        state.has_last_message_at = _has_last_message_at;
        state.last_message_at_unix_ms = _last_message_at_unix_ms;
        state.has_idle_deadline = _has_idle_deadline;
        state.idle_deadline_unix_ms = _idle_deadline_unix_ms;
        return state;
    }

    conversation_change_t
    join_agent (const std::string &agent_actor_id, std::string agent_display_name,
                long long joined_at_unix_ms)
    {
        ensure_not_closed ("join an agent");
        if (!_agent_actor_id.empty () && _agent_actor_id != agent_actor_id) {
            throw std::runtime_error ("Conversation already has an assigned agent.");
        }

        _agent_actor_id = agent_actor_id;
        _status = conversation_statuses_t::active;
        _participants[agent_actor_id] = conversation_participant_t{
          agent_actor_id, support_chat_roles_t::agent, std::move (agent_display_name),
          joined_at_unix_ms, false};

        const auto state = snapshot ();
        std::vector<conversation_event_t> events;
        events.push_back (conversation_event_t{conversation_event_kind_t::participant_joined, state,
                                               agent_actor_id, support_chat_roles_t::agent, {}, {}});
        events.push_back (conversation_event_t{conversation_event_kind_t::assigned, state,
                                               agent_actor_id, support_chat_roles_t::agent, {}, {}});
        return {state, std::move (events)};
    }

    conversation_change_t
    send_message (const std::string &sender_actor_id, const std::string &text,
                  long long sent_at_unix_ms)
    {
        ensure_participant (sender_actor_id);
        if (_status == conversation_statuses_t::closed) {
            throw std::runtime_error ("Closed conversation cannot accept messages.");
        }
        if (_status == conversation_statuses_t::waiting_for_agent) {
            throw std::runtime_error ("Conversation is waiting for an agent.");
        }
        if (text.empty ()) {
            throw std::runtime_error ("Message text is required.");
        }
        if (static_cast<int> (text.size ()) > _policy.max_message_length) {
            throw std::runtime_error ("Message text is too long.");
        }

        _status = conversation_statuses_t::active;
        _last_message_seq += 1;
        _last_message_at_unix_ms = sent_at_unix_ms;
        _has_last_message_at = true;
        _idle_deadline_unix_ms = sent_at_unix_ms + _policy.idle_timeout_ms;
        _has_idle_deadline = true;

        chat_message_t message;
        message.conversation_id = _conversation_id;
        message.message_seq = _last_message_seq;
        message.sender_actor_id = sender_actor_id;
        message.text = text;
        message.sent_at_unix_ms = sent_at_unix_ms;

        const auto state = snapshot ();
        std::vector<conversation_event_t> events;
        events.push_back (conversation_event_t{conversation_event_kind_t::message_appended, state,
                                               sender_actor_id, {}, message, {}});
        return {state, std::move (events)};
    }

    conversation_change_t set_typing (const std::string &actor_id, bool is_typing)
    {
        auto &participant = ensure_participant (actor_id);
        ensure_not_closed ("change typing state");
        participant.is_typing = is_typing;

        const auto state = snapshot ();
        std::vector<conversation_event_t> events;
        events.push_back (conversation_event_t{conversation_event_kind_t::typing_changed, state,
                                               actor_id, participant.role, {}, is_typing});
        return {state, std::move (events)};
    }

    conversation_change_t mark_idle (long long now_unix_ms)
    {
        if (_status != conversation_statuses_t::active || !_has_idle_deadline
            || now_unix_ms < _idle_deadline_unix_ms) {
            return {snapshot (), {}};
        }

        _status = conversation_statuses_t::waiting_for_close;
        const auto state = snapshot ();
        std::vector<conversation_event_t> events;
        events.push_back (conversation_event_t{conversation_event_kind_t::idle, state, {}, {}, {},
                                               {}});
        return {state, std::move (events)};
    }

    conversation_change_t close (const std::string &actor_id, const std::string &reason)
    {
        (void) reason;
        ensure_participant (actor_id);
        if (_status == conversation_statuses_t::closed) {
            throw std::runtime_error ("Conversation is already closed.");
        }

        _status = conversation_statuses_t::closed;
        const auto state = snapshot ();
        std::vector<conversation_event_t> events;
        events.push_back (conversation_event_t{conversation_event_kind_t::closed, state, actor_id,
                                               {}, {}, {}});
        return {state, std::move (events)};
    }

  private:
    conversation_participant_t &ensure_participant (const std::string &actor_id)
    {
        auto found = _participants.find (actor_id);
        if (found == _participants.end ()) {
            throw std::runtime_error ("Actor is not a conversation participant.");
        }
        return found->second;
    }

    void ensure_not_closed (const std::string &action)
    {
        if (_status == conversation_statuses_t::closed) {
            throw std::runtime_error ("Closed conversation cannot " + action + ".");
        }
    }

    std::string _conversation_id;
    std::string _subject;
    std::string _customer_actor_id;
    std::string _agent_actor_id;
    std::string _status;
    conversation_policy_t _policy;
    std::map<std::string, conversation_participant_t> _participants;
    unsigned long long _last_message_seq = 0;
    long long _last_message_at_unix_ms = 0;
    bool _has_last_message_at = false;
    long long _idle_deadline_unix_ms = 0;
    bool _has_idle_deadline = false;
};

} // namespace zlink::samples::supportchat
