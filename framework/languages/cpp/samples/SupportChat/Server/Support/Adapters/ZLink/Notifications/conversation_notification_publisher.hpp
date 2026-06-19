/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Actors/support_user_actor.hpp"
#include "../../../Domain/SupportChat/conversation_models.hpp"

#include <zlink/framework.hpp>

#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace zlink::samples::supportchat
{

// Translates conversation domain events into bound session push messages and
// delivers them to the relevant participants. This is the only place that maps
// a ConversationEvent onto a ParticipantJoined/Assigned/ChatMessage/Typing/
// Idle/Closed push, keeping the domain free of transport detail.
class conversation_notification_publisher_t
{
  public:
    using actor_map_t = std::map<std::string, support_user_actor_t *>;

    void publish (const std::vector<conversation_event_t> &events, const actor_map_t &actors)
    {
        for (const auto &event : events) {
            publish_one (event, actors);
        }
    }

    void publish_joined_agent_to_customer (const support_user_actor_t &customer,
                                           const conversation_state_t &state)
    {
        if (state.agent_actor_id.empty ()) {
            return;
        }
        push (customer, participant_joined_notify_t{state.conversation_id, state.agent_actor_id,
                                                    support_chat_roles_t::agent, state});
    }

  private:
    void publish_one (const conversation_event_t &event, const actor_map_t &actors)
    {
        switch (event.kind) {
        case conversation_event_kind_t::participant_joined:
            publish_participant_joined (event, actors);
            break;
        case conversation_event_kind_t::assigned:
            publish_assigned (event, actors);
            break;
        case conversation_event_kind_t::message_appended:
            publish_message (event, actors);
            break;
        case conversation_event_kind_t::typing_changed:
            publish_typing (event, actors);
            break;
        case conversation_event_kind_t::idle:
            for (const auto &[_, actor] : actors) {
                push (*actor, conversation_idle_notify_t{event.state.conversation_id, event.state});
            }
            break;
        case conversation_event_kind_t::closed:
            for (const auto &[_, actor] : actors) {
                push (*actor,
                      conversation_closed_notify_t{event.state.conversation_id, event.state});
            }
            break;
        default:
            throw std::runtime_error ("Unsupported conversation event.");
        }
    }

    void publish_participant_joined (const conversation_event_t &event, const actor_map_t &actors)
    {
        const auto &customer_id = event.state.customer_actor_id;
        auto found = actors.find (customer_id);
        if (found != actors.end () && found->second != nullptr
            && found->second->actor_id () != event.actor_id) {
            push (*found->second, participant_joined_notify_t{event.state.conversation_id,
                                                              event.actor_id, event.role,
                                                              event.state});
        }
    }

    void publish_assigned (const conversation_event_t &event, const actor_map_t &actors)
    {
        auto found = actors.find (event.actor_id);
        if (found == actors.end () || found->second == nullptr) {
            return;
        }
        push (*found->second,
              conversation_assigned_notify_t{event.state.conversation_id, event.state});
    }

    void publish_message (const conversation_event_t &event, const actor_map_t &actors)
    {
        if (!event.message) {
            throw std::runtime_error ("Message event requires a chat message.");
        }
        const auto &message = *event.message;
        for (const auto &[actor_id, actor] : actors) {
            if (actor_id == message.sender_actor_id || actor == nullptr) {
                continue;
            }
            push (*actor,
                  chat_message_notify_t{event.state.conversation_id, message, event.state});
        }
    }

    void publish_typing (const conversation_event_t &event, const actor_map_t &actors)
    {
        if (!event.is_typing) {
            throw std::runtime_error ("Typing event requires a typing state.");
        }
        for (const auto &[actor_id, actor] : actors) {
            if (actor_id == event.actor_id || actor == nullptr) {
                continue;
            }
            push (*actor, typing_changed_notify_t{event.state.conversation_id, event.actor_id,
                                                  *event.is_typing, event.state});
        }
    }

    template <typename TNotify> void push (const support_user_actor_t &actor, const TNotify &notify)
    {
        auto send_task = actor.context.bound_session ().send (notify).async ();
        zlink::framework::observe_task_completion (
          send_task, [] (const zlink::framework::result_t<void> &) {});
    }
};

} // namespace zlink::samples::supportchat
