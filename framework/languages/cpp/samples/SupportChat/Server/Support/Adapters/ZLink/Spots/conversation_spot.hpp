/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Actors/support_user_actor.hpp"
#include "../../../../Configuration/sample_names.hpp"
#include "../Notifications/conversation_notification_publisher.hpp"
#include "../../../Domain/SupportChat/conversation.hpp"

#include <zlink/framework.hpp>

#include <chrono>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

namespace zlink::samples::supportchat
{

// ConversationSpot owns the ZLink Spot lifecycle, the joined actors, and the
// Conversation domain aggregate. It registers the in-conversation actor
// handlers (send message, set typing, close), forwards domain calls, and routes
// domain events through the notification publisher. Idle/close decisions live in
// the domain; the spot only forwards the time signal.
class conversation_spot_t : public zlink::framework::spot_t
{
  public:
    conversation_spot_t () : _publisher (notification_publisher_or_default ()) {}

    static void
    use_notification_publisher (std::shared_ptr<conversation_notification_publisher_t> publisher)
    {
        notification_publisher () = std::move (publisher);
    }

    void configure (zlink::framework::spot_context_t &context)
    {
        _context = context;
        context.handlers ().add_actor_packet<&conversation_spot_t::send_message> ();
        context.handlers ().add_actor_packet<&conversation_spot_t::set_typing> ();
        context.handlers ().add_actor_packet<&conversation_spot_t::close> ();
    }

    zlink::framework::spot_create_response_t on_create (const zlink::message_t &request)
    {
        conversation_create_request_t create;
        from_stream_payload (request, create);
        const auto conversation_id = std::string (_context.spot_rid ().value ());
        _conversation = std::make_unique<conversation_t> (
          conversation_id, create.subject, create.customer_actor_id, create.customer_display_name,
          create.created_at_unix_ms);
        return zlink::framework::spot_create_response_t::accept ();
    }

    zlink::framework::spot_actor_join_response_t
    on_actor_join (const support_user_actor_t &actor, const zlink::message_t &request_message)
    {
        join_conversation_req_t join;
        try {
            from_stream_payload (request_message, join);
        }
        catch (...) {
            join.conversation_id = std::string (_context.spot_rid ().value ());
        }
        auto &conversation = require_conversation ();
        if (join.conversation_id != conversation.conversation_id ()) {
            return zlink::framework::spot_actor_join_response_t::reject ();
        }

        if (actor.role == support_chat_roles_t::agent) {
            const auto agent_state = join_agent (actor);
            return zlink::framework::spot_actor_join_response_t::accept (
              to_stream_payload (join_conversation_res_t{agent_state}));
        }

        actor.join_conversation (join.conversation_id);
        _actors[actor.actor_id ()] = const_cast<support_user_actor_t *> (&actor);
        if (actor.actor_id () == conversation.customer_actor_id ()) {
            _publisher->publish_joined_agent_to_customer (actor, conversation.snapshot ());
        }
        return zlink::framework::spot_actor_join_response_t::accept (
          to_stream_payload (join_conversation_res_t{conversation.snapshot ()}));
    }

    conversation_state_t join_agent (const support_user_actor_t &agent)
    {
        auto &conversation = require_conversation ();
        auto change = conversation.join_agent (agent.actor_id (), agent.display_name, now_unix_ms ());
        agent.join_conversation (conversation.conversation_id ());
        _actors[agent.actor_id ()] = const_cast<support_user_actor_t *> (&agent);
        _publisher->publish (change.events, _actors);
        return change.state;
    }

    send_chat_message_res_t
    send_message (const support_user_actor_t &actor,
                  const zlink::framework::spot_actor_request_context_t &context,
                  const send_chat_message_req_t &request)
    {
        if (context.packet_name.empty ()) {
            throw std::runtime_error ("packet name is required");
        }
        ensure_conversation_id (request.conversation_id);
        auto change = require_conversation ().send_message (actor.actor_id (), request.text,
                                                            now_unix_ms ());
        _publisher->publish (change.events, _actors);
        chat_message_t appended;
        for (const auto &event : change.events) {
            if (event.kind == conversation_event_kind_t::message_appended && event.message) {
                appended = *event.message;
            }
        }
        return send_chat_message_res_t{appended, change.state};
    }

    set_typing_res_t set_typing (const support_user_actor_t &actor,
                                 const zlink::framework::spot_actor_request_context_t &context,
                                 const set_typing_req_t &request)
    {
        if (context.packet_name.empty ()) {
            throw std::runtime_error ("packet name is required");
        }
        ensure_conversation_id (request.conversation_id);
        auto change = require_conversation ().set_typing (actor.actor_id (), request.is_typing);
        _publisher->publish (change.events, _actors);
        return set_typing_res_t{change.state};
    }

    close_conversation_res_t close (const support_user_actor_t &actor,
                                    const zlink::framework::spot_actor_request_context_t &context,
                                    const close_conversation_req_t &request)
    {
        if (context.packet_name.empty ()) {
            throw std::runtime_error ("packet name is required");
        }
        ensure_conversation_id (request.conversation_id);
        auto change = require_conversation ().close (actor.actor_id (), request.reason);
        _publisher->publish (change.events, _actors);
        return close_conversation_res_t{change.state};
    }

    // The idle timer forwards only a time signal; the domain decides idle/close.
    void check_idle (long long now)
    {
        auto change = require_conversation ().mark_idle (now);
        _publisher->publish (change.events, _actors);
    }

    void on_actor_joined (const support_user_actor_t &actor)
    {
        _actors[actor.actor_id ()] = const_cast<support_user_actor_t *> (&actor);
    }

    void onLeaveActor (const support_user_actor_t &actor) { _actors.erase (actor.actor_id ()); }

    void onDisconnectActor (const support_user_actor_t &actor) { actor.mark_disconnected (); }

    const conversation_t &conversation () const { return require_conversation_const (); }

  private:
    static std::shared_ptr<conversation_notification_publisher_t> &notification_publisher ()
    {
        static std::shared_ptr<conversation_notification_publisher_t> publisher;
        return publisher;
    }

    static std::shared_ptr<conversation_notification_publisher_t>
    notification_publisher_or_default ()
    {
        auto publisher = notification_publisher ();
        if (publisher == nullptr) {
            publisher = std::make_shared<conversation_notification_publisher_t> ();
        }
        return publisher;
    }

    void ensure_conversation_id (const std::string &conversation_id)
    {
        if (conversation_id != require_conversation ().conversation_id ()) {
            throw std::runtime_error ("Request targets another conversation. conversation="
                                      + conversation_id);
        }
    }

    conversation_t &require_conversation ()
    {
        if (!_conversation) {
            throw std::runtime_error ("Conversation has not been created.");
        }
        return *_conversation;
    }

    const conversation_t &require_conversation_const () const
    {
        if (!_conversation) {
            throw std::runtime_error ("Conversation has not been created.");
        }
        return *_conversation;
    }

    static long long now_unix_ms ()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds> (
                 std::chrono::system_clock::now ().time_since_epoch ())
          .count ();
    }

    zlink::framework::spot_context_t _context;
    std::shared_ptr<conversation_notification_publisher_t> _publisher;
    std::map<std::string, support_user_actor_t *> _actors;
    std::unique_ptr<conversation_t> _conversation;
};

} // namespace zlink::samples::supportchat
