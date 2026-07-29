/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#include "../Configuration/location_store.hpp"
#include "../Configuration/sample_configuration.hpp"
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
#include <set>
#include <string>
#include <vector>

namespace zlink::samples::supportchat
{
using namespace zlink::framework;

inline constexpr const char *supportchat_support_channel_rid = "supportchat-support";
inline constexpr const char *support_user_actor_type = "support-user";
inline constexpr const char *support_conversation_spot = "supportchat.conversation";

class support_user_actor_t : public actor_t
{
  public:
    explicit support_user_actor_t (actor_context_t value) :
        actor_id (value.actor_ref ().actor_id ()),
        actor_ref (value.actor_ref ()),
        actor_context (std::move (value))
    {
    }

    actor_context_t &context () noexcept override { return actor_context; }
    const actor_context_t &context () const noexcept override { return actor_context; }

    std::string actor_id;
    std::string display_name;
    std::string role;
    std::string participant_id;
    zlink::framework::actor_ref_t actor_ref;
    actor_context_t actor_context;
};

struct support_user_actor_relocation_state_t
{
    std::string display_name;
    std::string role;
    std::string participant_id;
};

inline void to_json (nlohmann::json &json, const support_user_actor_relocation_state_t &value)
{
    json = {{"displayName", value.display_name},
            {"role", value.role},
            {"participantId", value.participant_id}};
}

inline void from_json (const nlohmann::json &json, support_user_actor_relocation_state_t &value)
{
    value.display_name = json.value ("displayName", std::string{});
    value.role = json.value ("role", std::string{});
    value.participant_id = json.value ("participantId", std::string{});
}

class support_user_actor_relocation_adapter_t final
    : public actor_relocation_adapter_t<support_user_actor_t>
{
  public:
    task_t<std::vector<std::byte>>
    capture (support_user_actor_t &actor, std::stop_token) override
    {
        const auto message = zlink::message_t::from_json (
          support_user_actor_relocation_state_t{
            actor.display_name, actor.role, actor.participant_id});
        co_return std::vector<std::byte> (
          message.bytes ().begin (), message.bytes ().end ());
    }

    task_t<void>
    restore (support_user_actor_t &actor,
             std::vector<std::byte> payload,
             std::stop_token) override
    {
        const auto message = zlink::message_t::from (
          std::span<const std::byte> (payload.data (), payload.size ()));
        auto relocated =
          message.parse_json<support_user_actor_relocation_state_t> ();
        actor.display_name = std::move (relocated.display_name);
        actor.role = std::move (relocated.role);
        actor.participant_id = std::move (relocated.participant_id);
        co_return;
    }
};

class supportchat_conversation_runtime_t
{
  public:
    struct actor_profile_t
    {
        std::string actor_id;
        std::string display_name;
        std::string role;
        std::string participant_id;
    };

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

    set_agent_available_res_t set_agent_available (const std::string &actor_id,
                                                   const std::string &display_name,
                                                   bool available)
    {
        std::lock_guard lock (_mutex);
        _assignment.set_available (actor_id, display_name, available);
        return {available};
    }

    std::optional<std::string> assign_agent (const std::string &conversation_id)
    {
        std::lock_guard lock (_mutex);
        const auto assigned = _assignment.assign_for_conversation (conversation_id);
        return assigned ? std::optional<std::string>{assigned->roster_actor_id} : std::nullopt;
    }

    void release_conversation (const std::string &conversation_id)
    {
        std::lock_guard lock (_mutex);
        _assignment.release_conversation (conversation_id);
    }

    std::optional<actor_profile_t> actor_profile (const std::string &actor_id) const
    {
        std::lock_guard lock (_mutex);
        const auto found = _actors.find (actor_id);
        if (found == _actors.end ()) {
            return std::nullopt;
        }
        return actor_profile_t{found->second.actor_id, found->second.display_name,
                               found->second.role, found->second.participant_id};
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
    agent_availability_directory_t _agent_availability{3};
    agent_assignment_service_t _assignment{_agent_availability};
    int _next_conversation_seq{1};
};

struct conversation_create_req_t
{
    std::string conversation_id;
    std::string subject;
    std::string customer_actor_id;
    std::string customer_display_name;
};

inline void to_json (nlohmann::json &json, const conversation_create_req_t &value)
{
    json = {{"conversationId", value.conversation_id},
            {"subject", value.subject},
            {"customerActorId", value.customer_actor_id},
            {"customerDisplayName", value.customer_display_name}};
}

inline void from_json (const nlohmann::json &json, conversation_create_req_t &value)
{
    value.conversation_id = json.value ("conversationId", "");
    value.subject = json.value ("subject", "");
    value.customer_actor_id = json.value ("customerActorId", "");
    value.customer_display_name = json.value ("customerDisplayName", "");
}

class conversation_spot_t;

/* conversation Spot의 유휴 tick handler(공통 sample spec §14). */
struct conversation_idle_timer_handler_t
{
    void handle (conversation_spot_t &spot, const zlink::framework::timer_tick_t &tick) const;
};

class conversation_spot_t : public spot_t<support_user_actor_t>
{
  public:
    conversation_spot_t (spot_context_t context,
                         supportchat_conversation_runtime_t &runtime) :
        _runtime (runtime), _context (std::move (context))
    {
    }

    spot_context_t &context () noexcept override { return _context; }
    const spot_context_t &context () const noexcept override { return _context; }

    void configure () override
    {
        _context.handlers ()
          .add_actor_request<&conversation_spot_t::join> (join_conversation_req_t::packet_name)
          .add_actor_request<&conversation_spot_t::send_message> (
            send_chat_message_req_t::packet_name)
          .add_actor_send<&conversation_spot_t::set_typing> (set_typing_req_t::packet_name)
          .add_actor_request<&conversation_spot_t::close> (close_conversation_req_t::packet_name);
    }

    /* 공통 sample spec §14: 유휴 감지는 conversation Spot의 server-side timer가 소유한다.
     * idle deadline이 지나면 `WaitingForClose`로 바꾸고 idle 알림을, close grace가 지나면
     * 대화를 닫고 closed 알림을 **모든 참가자**에게 보낸다. */
    task_t<void> on_initialize () override
    {
        _idle_timer =
          _context.add_timer<conversation_idle_timer_handler_t> ("conversation-idle",
                                                                 std::chrono::milliseconds (500));
        co_return;
    }

    void on_idle_tick ()
    {
        if (!_conversation) {
            return;
        }
        auto transition = _conversation->advance_time (now_unix_ms ());
        if (const auto *idle = std::get_if<conversation_idle_notify_t> (&transition)) {
            broadcast (*idle, conversation_idle_notify_t::packet_name);
        }
        if (const auto *closed = std::get_if<conversation_closed_notify_t> (&transition)) {
            _runtime.release_conversation (closed->conversation_id);
            broadcast (*closed, conversation_closed_notify_t::packet_name);
        }
    }

    task_t<spot_create_response_t>
    on_create (const zlink::framework::message_t &request) override
    {
        auto create = request.decode<conversation_create_req_t> ();
        _conversation = conversation_t (create.conversation_id, create.subject,
                                        create.customer_actor_id);
        co_return spot_create_response_t::accept ();
    }

    task_t<spot_actor_join_response_t>
    on_actor_join (std::string_view actor_id,
                   const zlink::framework::message_t &message) override
    {
        const auto profile = _runtime.actor_profile (std::string (actor_id));
        if (!profile) {
            co_return spot_actor_join_response_t::reject ();
        }
        const auto request = message.decode<join_conversation_req_t> ();
        const auto participant_id = profile->participant_id.empty () ? profile->actor_id
                                                                      : profile->participant_id;
        if (request.participant_id != participant_id || request.role != profile->role
            || request.display_name != profile->display_name) {
            co_return spot_actor_join_response_t::reject ();
        }
        auto projected = require_conversation ();
        conversation_state_t admission_state;
        if (request.role == role_t::agent) {
            admission_state =
              projected.join_agent (request.participant_id, request.display_name).state;
        } else {
            const auto joined =
              projected.join_customer (request.participant_id, request.display_name);
            admission_state = joined.state;
            if (auto assigned = _runtime.assign_agent (joined.state.conversation_id)) {
                _pending_agent_assignments[std::string (actor_id)] = *assigned;
                admission_state = projected.assign_agent (*assigned).state;
            }
        }
        _pending_actor_joins.insert (std::string (actor_id));
        co_return spot_actor_join_response_t::accept (
          join_conversation_res_t{admission_state});
    }

    task_t<void> on_actor_joined (support_user_actor_t &actor) override
    {
        if (_pending_actor_joins.erase (actor.actor_id) == 0) {
            throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                         "accepted support actor admission is missing");
        }
        (void) join_actor (actor);
        co_return;
    }

    task_t<void> on_leave_actor (support_user_actor_t &) override { co_return; }

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
        auto sent = require_conversation ().send_message (actor.participant_id, request.text,
                                                          now_unix_ms ());
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
        (void) request;
        auto closed = require_conversation ().close ();
        _runtime.release_conversation (closed.state.conversation_id);
        /* 종료 알림은 대화의 모든 참가자가 받는다(공통 sample spec §14). */
        broadcast (conversation_closed_notify_t{closed.state.conversation_id, closed.state},
                   conversation_closed_notify_t::packet_name);
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
        const auto pending = _pending_agent_assignments.find (actor.actor_id);
        if (pending != _pending_agent_assignments.end ()) {
            const auto assigned = pending->second;
            _pending_agent_assignments.erase (pending);
            auto assignment = require_conversation ().assign_agent (assigned);
            send_to_actor (assigned,
                           conversation_assigned_notify_t{assignment.state.conversation_id,
                                                          assignment.state},
                           conversation_assigned_notify_t::packet_name);
            return {assignment.state};
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

    template <typename TMessage> void broadcast (const TMessage &message, const char *packet_name)
    {
        const auto state = require_conversation ().snapshot ();
        send_to_actor (state.customer_actor_id, message, packet_name);
        if (state.agent_actor_id && !state.agent_actor_id->empty ()) {
            send_to_actor (*state.agent_actor_id, message, packet_name);
        }
    }

    static std::int64_t now_unix_ms ()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds> (
                 std::chrono::system_clock::now ().time_since_epoch ())
          .count ();
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
        (*actor)->context.bound_session ().send (message).submit ();
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
    zlink::framework::timer_t _idle_timer;
    std::optional<conversation_t> _conversation;
    std::set<std::string> _pending_actor_joins;
    std::map<std::string, std::string> _pending_agent_assignments;
};

struct support_user_actor_factory_t final
    : public actor_factory_t<support_user_actor_t>
{
    task_t<std::shared_ptr<support_user_actor_t>>
    create (actor_context_t context, std::stop_token) override
    {
        co_return std::make_shared<support_user_actor_t> (
          std::move (context));
    }
};

inline void conversation_idle_timer_handler_t::handle (conversation_spot_t &spot,
                                                      const zlink::framework::timer_tick_t &) const
{
    spot.on_idle_tick ();
}

class support_entry_spot_t : public entry_spot_t<support_user_actor_t>
{
  public:
    support_entry_spot_t (entry_spot_context_t context,
                          supportchat_conversation_runtime_t &runtime) :
        _runtime (runtime), _context (std::move (context))
    {
    }

    entry_spot_context_t &context () noexcept override { return _context; }
    const entry_spot_context_t &context () const noexcept override
    {
        return _context;
    }

    void configure () override
    {
        _context.handlers ()
          .add_actor_request<&support_entry_spot_t::set_available> (
            set_agent_available_req_t::packet_name)
          .add_actor_request<&support_entry_spot_t::open_conversation> (
            open_conversation_req_t::packet_name);
    }

    task_t<spot_actor_join_response_t>
    on_actor_join (std::string_view actor_id,
                   const zlink::message_t &request) override
    {
        auto join = request.parse_json<ensure_support_user_actor_req_t> ();
        _pending_profiles[std::string (actor_id)] = std::move (join);
        co_return spot_actor_join_response_t::accept ();
    }

    task_t<actor_create_response_t>
    on_create_actor (support_user_actor_t &actor,
                     const zlink::framework::message_t &request) override
    {
        apply_actor_profile (actor, request.decode<ensure_support_user_actor_req_t> ());
        co_return actor_create_response_t::accept ();
    }

    task_t<void> on_actor_joined (support_user_actor_t &actor) override
    {
        const auto pending = _pending_profiles.find (actor.actor_id);
        if (pending != _pending_profiles.end ()) {
            auto profile = std::move (pending->second);
            _pending_profiles.erase (pending);
            apply_actor_profile (actor, std::move (profile));
        } else {
            _actors[actor.actor_id] = &actor;
            _runtime.remember_live_actor (actor);
        }
        co_return;
    }

    task_t<void> on_leave_actor (support_user_actor_t &) override { co_return; }

    set_agent_available_res_t set_available (support_user_actor_t &actor,
                                             spot_actor_request_context_t &,
                                             const set_agent_available_req_t &request)
    {
        if (actor.role != role_t::agent) {
            throw framework_exception_t (framework_error_kind_t::request_rejected,
                                         "only agent actors can set availability");
        }
        return _runtime.set_agent_available (actor.actor_id, actor.display_name,
                                             request.is_available);
    }

    task_t<open_conversation_res_t> open_conversation (support_user_actor_t &actor,
                                                       spot_actor_request_context_t &,
                                                       const open_conversation_req_t &request)
    {
        if (actor.role != role_t::customer) {
            throw framework_exception_t (framework_error_kind_t::request_rejected,
                                         "only customer actors can open conversations");
        }
        auto allocated =
          co_await _context.outbound ().request ("supportchat.api",
                                                 open_conversation_api_req_t{
                                                   actor.actor_id, actor.display_name,
                                                   request.subject})
            .submit<open_conversation_api_res_t> ();
        const auto conversation_id = allocated.conversation_id;
        const auto spot_rid = spot_rid_t::from_string (conversation_id);
        const auto participant_id = actor.participant_id.empty () ? actor.actor_id
                                                                   : actor.participant_id;
        auto joined =
          co_await actor.context ()
            .join_spot (spot_rid, join_conversation_req_t{participant_id, actor.role,
                                                          actor.display_name})
            .submit<join_conversation_res_t> ();
        const auto *accepted =
          std::get_if<framework::actor_join_accepted_t<join_conversation_res_t>> (&joined);
        if (accepted == nullptr) {
            throw framework_exception_t (framework_error_kind_t::request_failed,
                                         "SupportChat conversation join was rejected");
        }
        co_return open_conversation_res_t{conversation_id, accepted->reply.state};
    }

  private:
    void apply_actor_profile (support_user_actor_t &actor,
                              ensure_support_user_actor_req_t profile)
    {
        actor.display_name = std::move (profile.display_name);
        actor.role = std::move (profile.role);
        actor.participant_id = std::move (profile.participant_id);
        if (actor.participant_id.empty ()) {
            actor.participant_id = actor.actor_id;
        }
        _actors[actor.actor_id] = &actor;
        _runtime.remember_actor (actor.actor_id, actor.display_name, actor.role,
                                 actor.participant_id);
        _runtime.remember_live_actor (actor);
    }

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
    std::map<std::string, ensure_support_user_actor_req_t> _pending_profiles;
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

/* 공통 sample spec §12: 대화 개설은 API가 접수하고 Support가 배정한다. 이 채널 handler가
 * conversation id를 만들고 conversation Spot을 생성한다(참가자 join은 actor 경로가 수행). */
class allocate_conversation_handler_t
{
  public:
    using request_type = allocate_conversation_req_t;
    using reply_type = allocate_conversation_res_t;
    using dependency_types =
      dependency_list_t<spot_node_manager_t, supportchat_conversation_runtime_t>;
    static constexpr const char *topic_name = allocate_conversation_req_t::packet_name;

    allocate_conversation_handler_t (spot_node_manager_t &spots,
                                     supportchat_conversation_runtime_t &runtime) :
        _spots (spots), _runtime (runtime)
    {
    }

    allocate_conversation_res_t handle (const allocate_conversation_req_t &request)
    {
        const auto conversation_id = _runtime.next_conversation_id ();
        (void) _spots.get_or_create_spot (
          support_conversation_spot, spot_rid_t::from_string (conversation_id),
          zlink::framework::message_t::from (conversation_create_req_t{conversation_id, request.subject,
                                                     request.customer_actor_id,
                                                     request.customer_display_name}));
        return allocate_conversation_res_t{conversation_id,
                                           conversation_status_t::waiting_for_agent};
    }

  private:
    spot_node_manager_t &_spots;
    supportchat_conversation_runtime_t &_runtime;
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
            .join_entry_spot (ensure_support_user_actor_req_t{conversation_actor_id,
                                                               request.display_name,
                                                               role_t::agent,
                                                               request.roster_actor_id})
            .async ();
        if (!std::holds_alternative<framework::actor_join_accepted_t<framework::message_t>> (
              entry_joined)) {
            throw framework_exception_t (framework_error_kind_t::request_failed,
                                         "SupportChat entry join was rejected");
        }
        auto joined =
          co_await actor.value ()
            .context ()
            .join_spot (spot_rid_t::from_string (request.conversation_id),
                        join_conversation_req_t{request.roster_actor_id, role_t::agent,
                                                request.display_name})
            .submit<join_conversation_res_t> ();
        const auto *joined_accepted =
          std::get_if<framework::actor_join_accepted_t<join_conversation_res_t>> (&joined);
        if (joined_accepted == nullptr) {
            throw framework_exception_t (framework_error_kind_t::request_failed,
                                         "SupportChat agent conversation join was rejected");
        }
        co_return ensure_agent_conversation_res_t{
          snapshot_of (joined_accepted->actor), joined_accepted->reply.state};
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
        agent_availability_directory_t agents (2);
        agent_assignment_service_t assignment (agents);
        assignment.set_available ("agent-1", "Agent One", true);
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

        const auto room1_state = room1.snapshot ();
        require (room1_state.idle_deadline_unix_ms.has_value (),
                 "active conversation has no idle deadline");
        const auto idle1 = room1.advance_time (*room1_state.idle_deadline_unix_ms);
        const auto *idle_notify = std::get_if<conversation_idle_notify_t> (&idle1);
        require (idle_notify != nullptr
                   && idle_notify->state.status == conversation_status_t::waiting_for_close,
                 "idle did not move to WaitingForClose");
        const auto closed1 = room1.advance_time (*room1_state.idle_deadline_unix_ms
                                                 + conversation_t::close_grace_ms);
        const auto *closed_notify = std::get_if<conversation_closed_notify_t> (&closed1);
        require (closed_notify != nullptr
                   && closed_notify->state.status == conversation_status_t::closed,
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
        if (auto agent = assignment.assign_for_conversation (conversation_id)) {
            const auto assigned = conversation.assign_agent (agent->roster_actor_id);
            require (assigned.state.agent_actor_id == agent->roster_actor_id,
                     "assigned agent mismatch");
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

    /* dispatch 로그는 framework message-flow가 남긴다(공통 sample spec §5). 샘플이 직접
     * "message flow" 줄을 쓰지 않는다. */
    auto app = app_t::create ();
    const auto configuration = load_sample_configuration (app, argc, argv);
    const auto &topology = configuration.topology;
    std::filesystem::create_directories (configuration.role.log_dir);

    app.add_zlink_framework ([&] (zlink_framework_options_t &options) {
        options.configure_dispatch ()
          .message_flow (message_flow_log_mode_t::key_transitions)
          .trace_log_file (configuration.flow_log_path ())
          .trace_label ("supportchat-support");
        add_supportchat_location_store (options, topology);
        auto runtime = std::make_unique<supportchat_conversation_runtime_t> ();
        auto *runtime_ptr = runtime.get ();
        options.services ().add_singleton<supportchat_conversation_runtime_t> (std::move (runtime));
        options.services ().add_singleton<supportchat_server_story_t> ();
        options.add_client_server_channel ("supportchat.support")
          .enable_server (topology.support_route_endpoint)
          .set_routing_id (zlink::routing_id_t::from (supportchat_support_channel_rid))
          .use_handler_group ("supportchat-support");
        options.add_client_server_channel ("supportchat.api").enable_client ();
        options.handlers ()
          .group ("supportchat-support")
          .add<ensure_support_user_actor_handler_t> ()
          .add<ensure_agent_conversation_handler_t> ()
          .add<allocate_conversation_handler_t> ();
        options.http ()
          .listen (topology.support_http_url)
          .map_health ("/health")
          .map_post<supportchat_assert_handler_t> ("/self-check/assert");
        auto actor_route = options.add_route_mesh ("supportchat.session.actor.route");
        actor_route.listen (topology.support_actor_route_endpoint)
          .channel_name ("supportchat.session.actor.route");
        auto support_spot = options.add_route_mesh ("supportchat.support.spot");
        support_spot.channel_name ("supportchat.session.actor.route");
        support_spot.listen (topology.support_spot_router_endpoint)
          .add_entry_spot<support_entry_spot_t> (
            [runtime_ptr] (entry_spot_context_t context) {
                return std::make_shared<support_entry_spot_t> (
                  std::move (context), *runtime_ptr);
            })
          .add_spot_factory<conversation_spot_t> (
            support_conversation_spot,
            [runtime_ptr] (spot_context_t context) {
                return std::make_shared<conversation_spot_t> (
                  std::move (context), *runtime_ptr);
            },
            [] (auto &factory) {
                factory.disable_relocation ();
            })
          .add_actor_factory<
            support_user_actor_t,
            support_user_actor_factory_t> (
            support_user_actor_type,
            std::make_shared<support_user_actor_factory_t> (),
            [] (auto &factory) {
                factory
                  .template preserve_state_with<
                    support_user_actor_relocation_adapter_t> ();
            });
    });
    return app.run (argc, argv);
}
