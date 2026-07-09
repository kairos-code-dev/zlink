/* SPDX-License-Identifier: MPL-2.0 */

#include "../Configuration/location_store.hpp"
#include "../Configuration/sample_topology.hpp"
#include "Application/ConversationAssignment/agent_assignment_service.hpp"
#include "Domain/SupportChat/conversation.hpp"

#include <zlink/framework.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace zlink::samples::supportchat
{
using namespace zlink::framework;

inline constexpr const char *supportchat_support_node = "supportchat-support";
inline constexpr const char *support_user_actor_type = "support-user";
inline constexpr const char *support_conversation_spot = "supportchat.conversation";

support_actor_ref_snapshot_t snapshot_of (const zlink::framework::actor_ref_t &actor_ref)
{
    return {supportchat_support_node,
            std::string (actor_ref.actor_id ()),
            actor_ref.generation ()};
}

class support_user_actor_t
{
  public:
    explicit support_user_actor_t (std::string id) : actor_id (std::move (id)) {}

    void set_actor_ref (const zlink::framework::actor_ref_t &value)
    {
        actor_ref = value;
        actor_id = std::string (value.actor_id ());
    }

    void set_actor_context (actor_context_t value) { context = std::move (value); }

    std::string actor_id;
    std::string display_name;
    std::string role;
    std::string participant_id;
    zlink::framework::actor_ref_t actor_ref;
    actor_context_t context;
};

class supportchat_conversation_runtime_t
{
  public:
    void remember_actor (const std::string &actor_id,
                         const std::string &display_name,
                         const std::string &role,
                         const std::string &participant_id)
    {
        std::lock_guard lock (_mutex);
        _actors[actor_id] = participant_t{actor_id, display_name, role, participant_id};
    }

    std::string next_conversation_id ()
    {
        std::lock_guard lock (_mutex);
        return "supportchat-conversation-" + std::to_string (_next_conversation_seq++);
    }

    set_agent_available_res_t set_agent_available (const std::string &actor_id, bool available)
    {
        std::lock_guard lock (_mutex);
        if (available) {
            _available_agent = actor_id;
        } else if (_available_agent == actor_id) {
            _available_agent.reset ();
        }
        return {available};
    }

    std::optional<std::string> assign_agent ()
    {
        std::lock_guard lock (_mutex);
        return _available_agent;
    }

    std::optional<support_user_actor_t *> actor_for (const std::string &participant_id) const
    {
        std::lock_guard lock (_mutex);
        const auto actor_id = actor_id_for_participant (participant_id);
        const auto found = _live_actors.find (actor_id);
        if (found == _live_actors.end ()) {
            return std::nullopt;
        }
        return found->second;
    }

    void remember_live_actor (support_user_actor_t &actor)
    {
        std::lock_guard lock (_mutex);
        _live_actors[actor.actor_id] = &actor;
        if (actor.participant_id == actor.actor_id) {
            _live_actors[actor.participant_id] = &actor;
        }
    }

  private:
    struct participant_t
    {
        std::string actor_id;
        std::string display_name;
        std::string role;
        std::string participant_id;
    };

    std::string actor_id_for_participant (const std::string &participant_id) const
    {
        const auto identity = _actors.find (participant_id);
        if (identity != _actors.end () && identity->second.participant_id == participant_id) {
            return identity->second.actor_id;
        }
        return participant_id;
    }

    mutable std::mutex _mutex;
    std::map<std::string, participant_t> _actors;
    std::map<std::string, support_user_actor_t *> _live_actors;
    std::optional<std::string> _available_agent;
    int _next_conversation_seq{1};
};

struct conversation_create_req_t
{
    std::string conversation_id;
    std::string subject;
    std::string customer_actor_id;
    std::string customer_display_name;
    std::optional<std::string> assigned_agent_actor_id;
};

inline void to_json (nlohmann::json &json, const conversation_create_req_t &value)
{
    json = {{"conversationId", value.conversation_id},
            {"subject", value.subject},
            {"customerActorId", value.customer_actor_id},
            {"customerDisplayName", value.customer_display_name}};
    if (value.assigned_agent_actor_id) {
        json.emplace ("assignedAgentActorId", *value.assigned_agent_actor_id);
    }
}

inline void from_json (const nlohmann::json &json, conversation_create_req_t &value)
{
    value.conversation_id = json.value ("conversationId", "");
    value.subject = json.value ("subject", "");
    value.customer_actor_id = json.value ("customerActorId", "");
    value.customer_display_name = json.value ("customerDisplayName", "");
    const auto assigned = json.find ("assignedAgentActorId");
    if (assigned != json.end () && !assigned->is_null ()) {
        value.assigned_agent_actor_id = assigned->get<std::string> ();
    } else {
        value.assigned_agent_actor_id.reset ();
    }
}

class conversation_spot_t : public spot_t
{
  public:
    explicit conversation_spot_t (supportchat_conversation_runtime_t &runtime) :
        _runtime (runtime)
    {
    }

    void configure (spot_context_t &context)
    {
        _context = context;
        context.handlers ()
          .add_actor_request<&conversation_spot_t::join> (join_conversation_req_t::packet_name)
          .add_actor_request<&conversation_spot_t::send_message> (
            send_chat_message_req_t::packet_name)
          .add_actor_send<&conversation_spot_t::set_typing> (set_typing_req_t::packet_name)
          .add_actor_request<&conversation_spot_t::close> (close_conversation_req_t::packet_name);
    }

    spot_create_response_t on_create (const zlink::framework::message_t &request)
    {
        auto create = request.decode<conversation_create_req_t> ();
        _conversation = conversation_t (create.conversation_id, create.subject,
                                        create.customer_actor_id);
        if (create.assigned_agent_actor_id) {
            _conversation->assign_agent (*create.assigned_agent_actor_id);
        }
        return spot_create_response_t::accept ();
    }

    spot_actor_join_response_t on_actor_join (support_user_actor_t &actor,
                                              const zlink::framework::message_t &)
    {
        auto joined = join_actor (actor);
        return spot_actor_join_response_t::accept (joined);
    }

    join_conversation_res_t join (support_user_actor_t &actor,
                                  spot_actor_request_context_t &,
                                  const join_conversation_req_t &)
    {
        return join_actor (actor);
    }

    send_chat_message_res_t send_message (support_user_actor_t &actor,
                                          spot_actor_request_context_t &,
                                          const send_chat_message_req_t &request)
    {
        auto sent = require_conversation ().send_message (
          actor.participant_id, request.text,
          1000 + static_cast<std::int64_t> (require_conversation ().snapshot ().last_message_seq)
                   + 1);
        if (auto peer = peer_for (actor.participant_id)) {
            send_to_actor (*peer,
                           chat_message_notify_t{sent.state.conversation_id, sent.message,
                                                 sent.state},
                           chat_message_notify_t::packet_name);
        }
        return sent;
    }

    void set_typing (support_user_actor_t &actor,
                     spot_actor_send_context_t &,
                     const set_typing_req_t &request)
    {
        auto typing = require_conversation ().set_typing (actor.participant_id,
                                                          request.is_typing);
        if (auto peer = peer_for (actor.participant_id)) {
            send_to_actor (*peer, typing, typing_changed_notify_t::packet_name);
        }
    }

    close_conversation_res_t close (support_user_actor_t &actor,
                                    spot_actor_request_context_t &,
                                    const close_conversation_req_t &request)
    {
        if (request.reason && *request.reason == "idle") {
            auto idle = require_conversation ().mark_idle ();
            if (auto peer = peer_for (actor.participant_id)) {
                send_to_actor (*peer, idle, conversation_idle_notify_t::packet_name);
            }
        }
        auto closed = require_conversation ().close ();
        if (auto peer = peer_for (actor.participant_id)) {
            send_to_actor (*peer,
                           conversation_closed_notify_t{closed.state.conversation_id,
                                                        closed.state},
                           conversation_closed_notify_t::packet_name);
        }
        return {closed.state};
    }

  private:
    join_conversation_res_t join_actor (support_user_actor_t &actor)
    {
        if (actor.participant_id.empty ()) {
            actor.participant_id = actor.actor_id;
        }
        if (actor.role == role_t::agent) {
            auto joined = require_conversation ().join_agent (actor.participant_id,
                                                             actor.display_name);
            send_to_actor (joined.state.customer_actor_id,
                           participant_joined_notify_t{joined.conversation_id,
                                                       actor.participant_id,
                                                       actor.role,
                                                       joined.state},
                           participant_joined_notify_t::packet_name);
            return {joined.state};
        }

        auto joined = require_conversation ().join_customer (actor.participant_id,
                                                            actor.display_name);
        if (joined.state.agent_actor_id) {
            send_to_actor (*joined.state.agent_actor_id,
                           conversation_assigned_notify_t{joined.state.conversation_id,
                                                          joined.state},
                           conversation_assigned_notify_t::packet_name);
        }
        return {joined.state};
    }

    std::optional<std::string> peer_for (const std::string &participant_id) const
    {
        const auto state = require_conversation ().snapshot ();
        if (state.customer_actor_id != participant_id) {
            return state.customer_actor_id;
        }
        return state.agent_actor_id;
    }

    template <typename TMessage>
    void send_to_actor (const std::string &participant_id,
                        const TMessage &message,
                        const char *packet_name)
    {
        auto actor = _runtime.actor_for (participant_id);
        if (!actor) {
            std::cerr << "supportchat conversation: missing actor packet=" << packet_name
                      << " participant=" << participant_id << "\n";
            return;
        }
        (*actor)->context.bound_session ().send (message).packet_name (packet_name).submit ();
        std::cerr << "supportchat conversation: bound push packet=" << packet_name
                  << " participant=" << participant_id << "\n";
    }

    conversation_t &require_conversation ()
    {
        if (!_conversation) {
            throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                         "support conversation is not created");
        }
        return *_conversation;
    }

    const conversation_t &require_conversation () const
    {
        if (!_conversation) {
            throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                         "support conversation is not created");
        }
        return *_conversation;
    }

    supportchat_conversation_runtime_t &_runtime;
    spot_context_t _context;
    std::optional<conversation_t> _conversation;
};

struct support_user_actor_factory_t
{
    support_user_actor_t create (std::string actor_id) const
    {
        return support_user_actor_t (std::move (actor_id));
    }
};

class support_entry_spot_t : public entry_spot_t
{
  public:
    explicit support_entry_spot_t (supportchat_conversation_runtime_t &runtime) :
        _runtime (runtime)
    {
    }

    void configure (entry_spot_context_t &context)
    {
        _context = context;
        context.handlers ()
          .add_actor_request<&support_entry_spot_t::set_available> (
            set_agent_available_req_t::packet_name)
          .add_actor_request<&support_entry_spot_t::open_conversation> (
            open_conversation_req_t::packet_name);
    }

    void configure (spot_context_t &context)
    {
        entry_spot_context_t entry_context (context);
        configure (entry_context);
    }

    spot_actor_join_response_t on_actor_join (support_user_actor_t &actor,
                                              const zlink::message_t &request)
    {
        auto join = request.parse_json<ensure_support_user_actor_req_t> ();
        actor.display_name = join.display_name;
        actor.role = join.role;
        actor.participant_id = join.participant_id;
        if (actor.participant_id.empty ()) {
            actor.participant_id = actor.actor_id;
        }
        _actors[actor.actor_id] = &actor;
        _runtime.remember_actor (actor.actor_id, actor.display_name, actor.role,
                                 actor.participant_id);
        _runtime.remember_live_actor (actor);
        return spot_actor_join_response_t::accept ();
    }

    set_agent_available_res_t set_available (support_user_actor_t &actor,
                                             spot_actor_request_context_t &,
                                             const set_agent_available_req_t &request)
    {
        return _runtime.set_agent_available (actor.actor_id, request.is_available);
    }

    task_t<open_conversation_res_t> open_conversation (support_user_actor_t &actor,
                                                       spot_actor_request_context_t &,
                                                       const open_conversation_req_t &request)
    {
        const auto conversation_id = _runtime.next_conversation_id ();
        const auto spot_rid = spot_rid_t::from_string (conversation_id);
        (void) _context.manager ().get_or_create_spot (
          support_conversation_spot, spot_rid,
          conversation_create_req_t{conversation_id,
                                    request.subject,
                                    actor.participant_id,
                                    actor.display_name,
                                    _runtime.assign_agent ()});
        auto joined =
          co_await actor.context.join_spot (spot_rid, join_conversation_req_t {})
            .async<join_conversation_res_t> ();
        co_return open_conversation_res_t{conversation_id, joined.reply.state};
    }

  private:
    template <typename TMessage>
    void send_to_actor (const std::string &actor_id, const TMessage &message, const char *packet_name)
    {
        const auto found = _actors.find (actor_id);
        if (found == _actors.end ()) {
            std::cerr << "supportchat support: missing actor packet=" << packet_name
                      << " actor=" << actor_id << "\n";
            return;
        }
        found->second->context.bound_session ().send (message).packet_name (packet_name).submit ();
        std::cerr << "supportchat support: bound push packet=" << packet_name
                  << " actor=" << actor_id << "\n";
    }

    supportchat_conversation_runtime_t &_runtime;
    entry_spot_context_t _context;
    std::map<std::string, support_user_actor_t *> _actors;
};

class ensure_support_user_actor_handler_t
{
  public:
    using request_type = ensure_support_user_actor_req_t;
    using reply_type = ensure_support_user_actor_res_t;
    using dependency_types = dependency_list_t<session_actor_manager_t>;
    static constexpr const char *topic_name = "EnsureSupportUserActorReq";

    explicit ensure_support_user_actor_handler_t (session_actor_manager_t &actors) :
        _actors (actors)
    {
    }

    ensure_support_user_actor_res_t handle (const ensure_support_user_actor_req_t &request)
    {
        auto actor = _actors.get_or_create (support_user_actor_type, request.actor_id, request);
        if (!actor) {
            throw framework_exception_t (
              actor.error_kind (),
              actor.error () ? actor.error ()->what () : "support actor create failed");
        }
        return {snapshot_of (actor.value ().ref ())};
    }

  private:
    session_actor_manager_t &_actors;
};

class ensure_agent_conversation_handler_t
{
  public:
    using request_type = ensure_agent_conversation_req_t;
    using reply_type = ensure_agent_conversation_res_t;
    using dependency_types =
      dependency_list_t<session_actor_manager_t, supportchat_conversation_runtime_t>;
    static constexpr const char *topic_name = "EnsureAgentConversationReq";

    ensure_agent_conversation_handler_t (session_actor_manager_t &actors,
                                         supportchat_conversation_runtime_t &runtime) :
        _actors (actors), _runtime (runtime)
    {
    }

    task_t<ensure_agent_conversation_res_t> handle (const ensure_agent_conversation_req_t &request)
    {
        const auto conversation_actor_id =
          request.roster_actor_id + "@" + request.conversation_id;
        auto actor = _actors.get_or_create (
          support_user_actor_type, conversation_actor_id,
          ensure_support_user_actor_req_t{conversation_actor_id,
                                          request.display_name,
                                          role_t::agent,
                                          request.roster_actor_id});
        if (!actor) {
            throw framework_exception_t (
              actor.error_kind (),
              actor.error () ? actor.error ()->what () : "support conversation actor create failed");
        }
        auto entry_joined =
          co_await actor.value ()
            .context ()
            .join_entry_spot (node_rid_t::from_string (supportchat_support_node),
                              ensure_support_user_actor_req_t{conversation_actor_id,
                                                              request.display_name,
                                                              role_t::agent,
                                                              request.roster_actor_id})
            .async ();
        (void) entry_joined;
        auto joined =
          co_await actor.value ()
            .context ()
            .join_spot (spot_rid_t::from_string (request.conversation_id),
                        join_conversation_req_t {})
            .async<join_conversation_res_t> ();
        co_return ensure_agent_conversation_res_t{
          snapshot_of (joined.actor.value_or (actor.value ().ref ())), joined.reply.state};
    }

  private:
    session_actor_manager_t &_actors;
    supportchat_conversation_runtime_t &_runtime;
};

class supportchat_server_story_t
{
  public:
    supportchat_server_assertion_res_t run ()
    {
        _evidence.clear ();
        agent_availability_directory_t agents;
        agent_assignment_service_t assignment (agents);
        agents.set_available ("agent-1", 2);
        record ("agent-availability=registered");

        auto room1 = open_conversation ("supportchat-conversation-1", "checkout payment failed",
                                        "customer-1", assignment);
        auto room2 = open_conversation ("supportchat-conversation-2", "cannot log in",
                                        "customer-2", assignment);
        require (room1.snapshot ().agent_actor_id == std::optional<std::string>{"agent-1"},
                 "room1 assigned agent mismatch");
        require (room2.snapshot ().agent_actor_id == std::optional<std::string>{"agent-1"},
                 "room2 assigned agent mismatch");
        record ("one-agent-many-conversations=verified");

        const auto room1_join = room1.join_agent ("agent-1", "Agent One");
        const auto room2_join = room2.join_agent ("agent-1", "Agent One");
        require (room1_join.state.status == conversation_status_t::active,
                 "room1 did not activate");
        require (room2_join.state.status == conversation_status_t::active,
                 "room2 did not activate");
        record ("agent-join=verified");

        const auto greet1 = room1.send_message ("agent-1", "How can I help?", 1000);
        const auto reply1 = room1.send_message ("customer-1", "Payment keeps failing.", 1200);
        const auto greet2 = room2.send_message ("agent-1", "Let me check your account.", 1300);
        require (greet1.message.message_seq == 1 && reply1.message.message_seq == 2,
                 "room1 sequence mismatch");
        require (greet2.message.message_seq == 1, "room2 sequence did not start at 1");
        record ("conversation-sequence=verified");

        const auto typing = room1.set_typing ("agent-1", true);
        require (typing.is_typing && typing.actor_id == "agent-1", "typing event mismatch");
        record ("typing-one-way=verified");

        const auto rejoin = room1.join_customer ("customer-1", "Customer One");
        require (rejoin.state.last_message_seq == 2, "reconnect state did not preserve messages");
        record ("reconnect-state=verified");

        const auto closed2 = room2.close ();
        require (closed2.state.status == conversation_status_t::closed,
                 "explicit close did not close room2");
        bool duplicate_close_failed = false;
        try {
            (void) room2.send_message ("customer-2", "again", 1400);
        }
        catch (const std::logic_error &) {
            duplicate_close_failed = true;
        }
        require (duplicate_close_failed, "closed room accepted a message");
        record ("explicit-close=verified");

        const auto idle1 = room1.mark_idle ();
        require (idle1.state.status == conversation_status_t::waiting_for_close,
                 "idle did not move to WaitingForClose");
        const auto closed1 = room1.close ();
        require (closed1.state.status == conversation_status_t::closed,
                 "idle close did not close room1");
        record ("idle-close=verified");

        auto no_agent = open_conversation ("supportchat-conversation-3", "agent unavailable",
                                          "customer-3", assignment);
        require (!no_agent.snapshot ().agent_actor_id
                   && no_agent.snapshot ().status == conversation_status_t::waiting_for_agent,
                 "no-agent conversation did not wait");
        record ("no-agent-waiting=verified");

        return {.ok = true, .evidence = _evidence};
    }

  private:
    conversation_t open_conversation (const std::string &conversation_id,
                                      const std::string &subject,
                                      const std::string &customer_id,
                                      agent_assignment_service_t &assignment)
    {
        conversation_t conversation (conversation_id, subject, customer_id);
        const auto customer_join = conversation.join_customer (customer_id, customer_id);
        require (customer_join.state.status == conversation_status_t::waiting_for_agent,
                 "customer join state mismatch");
        if (auto agent = assignment.assign ()) {
            const auto assigned = conversation.assign_agent (*agent);
            require (assigned.state.agent_actor_id == *agent, "assigned agent mismatch");
        }
        record ("open:" + conversation_id + ":" + conversation.snapshot ().status);
        return conversation;
    }

    void record (std::string entry) { _evidence.push_back (std::move (entry)); }

    static void require (bool condition, const std::string &message)
    {
        if (!condition) {
            throw std::logic_error (message);
        }
    }

    std::vector<std::string> _evidence;
};

class supportchat_assert_handler_t
{
  public:
    using dependency_types = zlink::framework::dependency_list_t<supportchat_server_story_t>;
    using request_type = supportchat_server_assertion_req_t;
    using reply_type = supportchat_server_assertion_res_t;

    explicit supportchat_assert_handler_t (supportchat_server_story_t &story) : _story (story) {}

    supportchat_server_assertion_res_t handle (const supportchat_server_assertion_req_t &)
    {
        return _story.run ();
    }

  private:
    supportchat_server_story_t &_story;
};

} // namespace zlink::samples::supportchat

int main (int argc, char **argv)
{
    using namespace zlink::framework;
    using namespace zlink::samples::supportchat;

    std::filesystem::create_directories (supportchat_log_dir ());
    const sample_topology_t topology;
    std::ofstream flow (supportchat_log_dir () + "/flow-support.log", std::ios::app);
    flow << "message flow role=support action=location-store-claim redis="
         << topology.redis_endpoint << " prefix=" << topology.redis_key_prefix << "\n";
    flow << "message flow role=support action=topology-ready sessionStream="
         << topology.session_stream_endpoint << " apiRoute=" << topology.api_route_endpoint
         << " supportRoute=" << topology.support_route_endpoint << "\n";
    flow.flush ();

    auto app = app_t::create ();
    app.add_zlink_framework ([&] (zlink_framework_options_t &options) {
        options.configure_dispatch ()
          .message_flow (message_flow_log_mode_t::key_transitions)
          .trace_log_file (supportchat_log_dir () + "/flow-support.log")
          .trace_label ("supportchat-support");
        add_supportchat_location_store (options, topology);
        auto runtime = std::make_unique<supportchat_conversation_runtime_t> ();
        auto *runtime_ptr = runtime.get ();
        options.services ().add_singleton<supportchat_conversation_runtime_t> (std::move (runtime));
        options.services ().add_singleton<supportchat_server_story_t> ();
        options.add_client_server_channel ("supportchat.support")
          .enable_server (topology.support_route_endpoint)
          .set_routing_id (zlink::routing_id_t::from (supportchat_support_node))
          .use_handler_group ("supportchat-support");
        options.handlers ()
          .group ("supportchat-support")
          .add<ensure_support_user_actor_handler_t> ()
          .add<ensure_agent_conversation_handler_t> ();
        options.http ()
          .listen (topology.support_http_url)
          .map_health ("/health")
          .map_post<supportchat_assert_handler_t> ("/self-check/assert");
        options.add_route_mesh_channel ("supportchat.session.actor.route")
          .enable_server (topology.support_actor_route_endpoint)
          .enable_client ()
          .set_routing_id (zlink::routing_id_t::from ("supportchat-support"));
        options.add_spot_mesh ("supportchat.support.spot")
          .accept_route_mesh ("supportchat.session.actor.route")
          .set_routing_id (zlink::routing_id_t::from (supportchat_support_node))
          .enable_router (topology.support_spot_router_endpoint)
          .enable_pub_sub (topology.support_spot_endpoint)
          .add_entry_spot<support_entry_spot_t> (
            [runtime_ptr] { return std::make_shared<support_entry_spot_t> (*runtime_ptr); })
          .add_spot<conversation_spot_t> (
            support_conversation_spot,
            [runtime_ptr] { return std::make_shared<conversation_spot_t> (*runtime_ptr); })
          .add_actor_factory<support_user_actor_factory_t> (support_user_actor_type);
    });
    return app.run (argc, argv);
}
