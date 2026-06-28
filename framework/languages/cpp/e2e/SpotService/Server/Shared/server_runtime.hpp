/* SPDX-License-Identifier: MPL-2.0 */

#include "../../Shared/spot_service_contracts.hpp"
#include "../Play/Endpoints/spot_lifecycle_endpoints.hpp"
#include "scenario_state.hpp"

#include <zlink/framework.hpp>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace e2e = zlink::framework::e2e::spot_service;

namespace
{

std::string env_or (const char *name, std::string fallback = {})
{
    if (const char *value = std::getenv (name); value != nullptr && *value != '\0') {
        return value;
    }
    return fallback;
}

struct scenario_actor_t
{
    explicit scenario_actor_t (std::string actor_id) : actor_id (std::move (actor_id)) {}

    void set_actor_ref (const zlink::framework::actor_ref_t &value)
    {
        actor_ref = value;
        actor_id = std::string (value.actor_id ());
    }

    void set_actor_context (zlink::framework::actor_context_t value)
    {
        context = std::move (value);
    }

    std::string actor_id;
    std::string display_name;
    int level = 0;
    int ping_seen = 0;
    zlink::framework::actor_ref_t actor_ref;
    zlink::framework::actor_context_t context;
};

struct scenario_actor_factory_t
{
    scenario_actor_t create (std::string actor_id) const
    {
        return scenario_actor_t (std::move (actor_id));
    }
};

zlink::framework::actor_ref_t to_actor_ref (const e2e::actor_ref_dto_t &actor)
{
    return zlink::framework::actor_ref_t (
      zlink::framework::node_rid_t::from_string (actor.node_rid), actor.actor_type, actor.actor_id,
      actor.generation);
}

e2e::actor_ref_dto_t from_actor_ref (const zlink::framework::actor_ref_t &actor)
{
    return {.node_rid = std::string (actor.node_rid ().value ()),
            .actor_type = std::string (actor.actor_type ()),
            .actor_id = std::string (actor.actor_id ()),
            .generation = actor.generation ()};
}

std::string owner_for_key (const std::string &key)
{
    return e2e::owner_node_rid_for_key (key);
}

zlink::framework::spot_rid_t user_spot_rid (const std::string &key)
{
    return zlink::framework::spot_rid_t::from_string (e2e::user_spot_rid_for_key (key));
}

class user_spot_t : public zlink::framework::spot_t
{
  public:
    explicit user_spot_t (scenario_state_t &state) : _state (state) {}

    void configure (zlink::framework::spot_context_t &context)
    {
        _context = context;
        context.handlers ().add_actor_packet<&user_spot_t::mutate> ("StateReq");
        context.handlers ().add_actor_packet<&user_spot_t::ping> ("ActorPingReq");
        context.handlers ().add_actor_packet<&user_spot_t::slow_ping> ("SlowActorPingReq");
        context.handlers ().add_actor_packet<&user_spot_t::complex> ("ComplexActorReq");
        context.handlers ().add_actor_packet<&user_spot_t::leave> ("LeaveReq");
        context.handlers ().add_actor_packet<&user_spot_t::disconnect> ("DisconnectReq");
        context.handlers ().add_actor_packet<&user_spot_t::outbound> ("OutboundReq");
        context.handlers ().add_actor_packet<&user_spot_t::run_worker> ("WorkerReq");
        context.handlers ().add_actor_packet<&user_spot_t::spot_to_spot> ("SpotToSpotReq");
        context.handlers ().add_actor_packet<&user_spot_t::type_mismatch> ("TypeMismatchReq");
        context.handlers ().add_actor_packet<&user_spot_t::push_to_session> ("PushReq");
        context.handlers ().add_handler<&user_spot_t::direct_state> ("StateReq");
        context.handlers ().add_handler<&user_spot_t::direct_request> ("DirectSpotReq");
        context.handlers ().add_handler<&user_spot_t::direct_command> ("DirectSpotCommand");
        context.handlers ().add_handler<&user_spot_t::slow_request> ("SlowSpotReq");
        context.handlers ().add_handler<&user_spot_t::spot_outbound> ("SpotOutboundReq");
        context.handlers ().add_handler<&user_spot_t::spot_outbound_negative> (
          "SpotOutboundNegativeReq");
        context.handlers ().add_handler<&user_spot_t::spot_to_spot_direct> (
          "SpotToSpotDirectReq");
        context.handlers ().add_handler<&user_spot_t::spot_to_spot_timeout> (
          "SpotToSpotTimeoutReq");
        context.handlers ().add_handler<&user_spot_t::spot_to_spot_negative> (
          "SpotToSpotNegativeReq");
        context.handlers ().add_subscribe<&user_spot_t::on_mesh_event> (e2e::mesh_topic);
    }

    void on_initialize ()
    {
        _state.record ("SpotInitialized", {}, std::string (_context.spot_rid ().value ()));
    }

    void on_closing ()
    {
        _state.record ("SpotClosing", {}, std::string (_context.spot_rid ().value ()));
    }

    zlink::framework::spot_actor_join_response_t
    on_actor_join (scenario_actor_t &actor, const zlink::framework::message_t &request_message)
    {
        const auto request = request_message.decode<e2e::join_req_t> ();
        actor.display_name = request.display_name;
        actor.level = request.level;
        _state.record ("ActorJoined", actor.actor_id, std::string (_context.spot_rid ().value ()),
                       request.key);
        return zlink::framework::spot_actor_join_response_t::accept (
          e2e::join_res_t{.spot_rid = std::string (_context.spot_rid ().value ()),
                          .owner_node_rid = _state.node_rid,
                          .actor_id = actor.actor_id,
                          .display_name = actor.display_name,
                          .level = actor.level,
                          .tags = request.tags,
                          .actor = from_actor_ref (actor.actor_ref)});
    }

    void on_actor_joined (const scenario_actor_t &actor)
    {
        _state.record ("ActorJoinedCallback", actor.actor_id,
                       std::string (_context.spot_rid ().value ()));
    }

    void onLeaveActor (const scenario_actor_t &actor)
    {
        _state.record ("ActorLeft", actor.actor_id, std::string (_context.spot_rid ().value ()));
    }

    void onDisconnectActor (const scenario_actor_t &actor)
    {
        _state.record ("ActorDisconnected", actor.actor_id,
                       std::string (_context.spot_rid ().value ()));
    }

    e2e::state_res_t mutate (const scenario_actor_t &actor,
                             zlink::framework::spot_actor_request_context_t &,
                             const e2e::state_req_t &request)
    {
        if (request.op == "add") {
            _value += request.amount;
        } else if (request.op == "set") {
            _value = request.amount;
        }
        ++_sequence;
        _state.record ("StateMutated", actor.actor_id, std::string (_context.spot_rid ().value ()),
                       std::to_string (_value));
        return {.spot_rid = std::string (_context.spot_rid ().value ()),
                .owner_node_rid = _state.node_rid,
                .value = _value,
                .sequence = _sequence};
    }

    e2e::actor_ping_res_t ping (scenario_actor_t &actor,
                                zlink::framework::spot_actor_request_context_t &,
                                const e2e::actor_ping_req_t &request)
    {
        ++actor.ping_seen;
        _state.record ("ActorPing", actor.actor_id, std::string (_context.spot_rid ().value ()),
                       request.value + ":" + std::to_string (actor.ping_seen));
        return {.actor_id = actor.actor_id,
                .node_rid = _state.node_rid,
                .spot_rid = std::string (_context.spot_rid ().value ()),
                .value = request.value,
                .seen = actor.ping_seen};
    }

    e2e::actor_ping_res_t slow_ping (scenario_actor_t &actor,
                                     zlink::framework::spot_actor_request_context_t &,
                                     const e2e::slow_actor_ping_req_t &request)
    {
        std::this_thread::sleep_for (std::chrono::milliseconds (request.delay_ms));
        ++actor.ping_seen;
        _state.record ("ActorSlowPing", actor.actor_id,
                       std::string (_context.spot_rid ().value ()),
                       request.value + ":" + std::to_string (actor.ping_seen));
        return {.actor_id = actor.actor_id,
                .node_rid = _state.node_rid,
                .spot_rid = std::string (_context.spot_rid ().value ()),
                .value = request.value,
                .seen = actor.ping_seen};
    }

    e2e::complex_actor_res_t complex (const scenario_actor_t &actor,
                                      zlink::framework::spot_actor_request_context_t &,
                                      const e2e::complex_actor_req_t &request)
    {
        _state.record ("ActorComplex", actor.actor_id, std::string (_context.spot_rid ().value ()),
                       request.display_name + "|" + std::to_string (request.level) + "|"
                         + request.attributes.at ("role") + "|" + request.attributes.at ("region"));
        return {.actor_id = actor.actor_id,
                .display_name = request.display_name,
                .level = request.level,
                .tags = request.tags,
                .attributes = request.attributes};
    }

    e2e::state_res_t direct_state (const e2e::state_req_t &request)
    {
        if (request.op == "add") {
            _value += request.amount;
        } else if (request.op == "set") {
            _value = request.amount;
        }
        ++_sequence;
        _state.record ("StateRouted", {}, std::string (_context.spot_rid ().value ()),
                       std::to_string (_value));
        return {.spot_rid = std::string (_context.spot_rid ().value ()),
                .owner_node_rid = _state.node_rid,
                .value = _value,
                .sequence = _sequence};
    }

    zlink::framework::task_t<e2e::leave_res_t>
    leave (scenario_actor_t &actor,
           zlink::framework::spot_actor_request_context_t &,
           const e2e::leave_req_t &)
    {
        auto left = co_await _context.leaveActor (actor.actor_ref, actor);
        if (!left.empty ()) {
            _state.record ("ActorLeaveRequested", actor.actor_id,
                           std::string (_context.spot_rid ().value ()));
        }
        co_return e2e::leave_res_t{!left.empty (), actor.actor_id};
    }

    e2e::disconnect_res_t disconnect (const scenario_actor_t &actor,
                                      zlink::framework::spot_actor_request_context_t &,
                                      const e2e::disconnect_req_t &request)
    {
        _state.record ("DisconnectRequested", actor.actor_id,
                       std::string (_context.spot_rid ().value ()), request.reason);
        return {.disconnected = true, .actor_id = actor.actor_id};
    }

    zlink::framework::task_t<e2e::outbound_res_t>
    outbound (const scenario_actor_t &actor,
              zlink::framework::spot_actor_request_context_t &,
              const e2e::outbound_req_t &request)
    {
        auto reply = co_await _context.outbound ()
                       .request (e2e::api_channel, e2e::channel_echo_req_t{request.value})
                       .timeout (std::chrono::milliseconds (3000))
                       .async<e2e::channel_echo_res_t> ();
        co_await _context.outbound ()
          .send (e2e::api_channel,
                 e2e::channel_command_t{"cmd-" + actor.actor_id + "-" + request.value})
          .async ();
        co_await _context
          .publish (e2e::mesh_topic, e2e::mesh_event_t{"evt-" + actor.actor_id, request.value})
          .async ();
        _state.record ("SpotOutbound", actor.actor_id, std::string (_context.spot_rid ().value ()),
                       reply.value);
        co_return e2e::outbound_res_t{reply.value, true, true};
    }

    zlink::framework::task_t<e2e::outbound_res_t>
    spot_outbound (const e2e::outbound_req_t &request)
    {
        auto reply = co_await _context.outbound ()
                       .request (e2e::api_channel, e2e::channel_echo_req_t{request.value})
                       .timeout (std::chrono::milliseconds (3000))
                       .async<e2e::channel_echo_res_t> ();
        co_await _context.outbound ()
          .send (e2e::api_channel, e2e::channel_command_t{"notify-" + request.value})
          .async ();
        co_await _context
          .publish (e2e::mesh_topic, e2e::mesh_event_t{"evt-sm-c2", "sm-c2-publish"})
          .async ();
        bool timed_out = false;
        try {
            (void) co_await _context.outbound ()
              .request (e2e::api_channel,
                        e2e::channel_slow_req_t{.value = request.value, .delay_ms = 2000})
              .timeout (std::chrono::milliseconds (1000))
              .async<e2e::channel_slow_res_t> ();
        }
        catch (...) {
            timed_out = true;
        }
        _state.record ("SpotOutbound", {}, std::string (_context.spot_rid ().value ()),
                       reply.value + "|notify-" + request.value + "|timeout="
                         + std::string (timed_out ? "true" : "false"));
        co_return e2e::outbound_res_t{reply.value, true, true};
    }

    zlink::framework::task_t<e2e::outbound_res_t>
    spot_outbound_negative (const e2e::outbound_req_t &request)
    {
        bool request_failed = false;
        try {
            (void) co_await _context.outbound ()
              .request (e2e::api_channel, e2e::channel_echo_req_t{request.value})
              .packet_name ("MissingChannelReq")
              .timeout (std::chrono::milliseconds (1000))
              .async<e2e::channel_echo_res_t> ();
        }
        catch (...) {
            request_failed = true;
        }
        co_await _context.outbound ()
          .send (e2e::api_channel, e2e::channel_command_t{"missing-" + request.value})
          .packet_name ("MissingChannelSend")
          .async ();
        _state.record ("SpotOutboundNegative", {}, std::string (_context.spot_rid ().value ()),
                       std::string ("requestFailed=") + (request_failed ? "true" : "false"));
        co_return e2e::outbound_res_t{"missing-" + request.value, false, request_failed};
    }

    zlink::framework::task_t<e2e::worker_res_t>
    run_worker (const scenario_actor_t &actor,
                zlink::framework::spot_actor_request_context_t &,
                const e2e::worker_req_t &request)
    {
        const auto snapshot = _value;
        _state.record ("WorkerStarted", actor.actor_id, std::string (_context.spot_rid ().value ()),
                       std::to_string (snapshot));
        auto worker_result =
          co_await _context
            .run_worker ([snapshot, request] {
                std::this_thread::sleep_for (std::chrono::milliseconds (request.delay_ms));
                return snapshot + request.delta;
            })
            .timeout (std::chrono::milliseconds (3000))
            .yield ();
        _value += request.delta;
        ++_sequence;
        _state.record ("WorkerCompleted", actor.actor_id,
                       std::string (_context.spot_rid ().value ()), std::to_string (_value));
        co_return e2e::worker_res_t{snapshot, worker_result, _value, _sequence};
    }

    e2e::direct_spot_res_t direct_request (const e2e::direct_spot_req_t &request)
    {
        _state.record ("SpotToSpotRequest", request.source_actor_id,
                       std::string (_context.spot_rid ().value ()), request.value);
        return {.spot_rid = std::string (_context.spot_rid ().value ()),
                .owner_node_rid = _state.node_rid,
                .value = request.value + ":reply"};
    }

    void direct_command (const e2e::direct_spot_command_t &request)
    {
        _state.record ("SpotToSpotCommand", request.source_actor_id,
                       std::string (_context.spot_rid ().value ()), request.value);
    }

    e2e::direct_spot_res_t slow_request (const e2e::slow_spot_req_t &request)
    {
        std::this_thread::sleep_for (std::chrono::milliseconds (300));
        _state.record ("SpotToSpotSlow", {}, std::string (_context.spot_rid ().value ()),
                       request.value);
        return {.spot_rid = std::string (_context.spot_rid ().value ()),
                .owner_node_rid = _state.node_rid,
                .value = request.value + ":slow"};
    }

    zlink::framework::task_t<e2e::spot_to_spot_res_t>
    spot_to_spot (const scenario_actor_t &actor,
                  zlink::framework::spot_actor_request_context_t &,
                  const e2e::spot_to_spot_req_t &request)
    {
        const auto target_node =
          zlink::framework::node_rid_t::from_string (owner_for_key (request.target_key));
        const auto target_spot = user_spot_rid (request.target_key);
        auto reply =
          co_await _context
            .request_to<e2e::direct_spot_res_t> (
              target_node, target_spot, e2e::direct_spot_req_t{actor.actor_id, request.value})
            .packet_name ("DirectSpotReq")
            .timeout (std::chrono::milliseconds (3000))
            .async ();
        co_await _context
          .send_to (target_node, target_spot,
                    e2e::direct_spot_command_t{actor.actor_id, request.value + ":command"})
          .packet_name ("DirectSpotCommand")
          .async ();
        co_await _context
          .publish (e2e::mesh_topic,
                    e2e::mesh_event_t{"evt-spot-to-spot", actor.actor_id + ":" + request.value})
          .async ();
        auto missing = _context
                         .request_to<e2e::direct_spot_res_t> (
                           target_node, target_spot, e2e::unhandled_spot_req_t{request.value})
                         .packet_name ("MissingSpotReq")
                         .timeout (std::chrono::milliseconds (1000))
                         .async ()
                         .result ();
        auto timed_out = _context
                           .request_to<e2e::direct_spot_res_t> (target_node, target_spot,
                                                                e2e::slow_spot_req_t{request.value})
                           .packet_name ("SlowSpotReq")
                           .timeout (std::chrono::milliseconds (50))
                           .async ()
                           .result ();
        _state.record ("SpotToSpotOutbound", actor.actor_id,
                       std::string (_context.spot_rid ().value ()), reply.value);
        co_return e2e::spot_to_spot_res_t{reply.value, true, true, !missing.has_value (),
                                          !timed_out.has_value ()};
    }

    zlink::framework::task_t<e2e::spot_to_spot_route_res_t>
    spot_to_spot_direct (const e2e::spot_to_spot_route_req_t &request)
    {
        const auto target_node =
          zlink::framework::node_rid_t::from_string (request.target_node_rid);
        const auto target_spot =
          zlink::framework::spot_rid_t::from_string (request.target_spot_rid);
        const auto source_spot = std::string (_context.spot_rid ().value ());
        const auto target_value = "sm-c3-" + request.marker;
        auto reply =
          co_await _context
            .request_to<e2e::direct_spot_res_t> (
              target_node, target_spot, e2e::direct_spot_req_t{source_spot, target_value})
            .packet_name ("DirectSpotReq")
            .timeout (std::chrono::milliseconds (3000))
            .async ();
        co_await _context
          .send_to (target_node, target_spot,
                    e2e::direct_spot_command_t{source_spot,
                                               "sm-c3-send-" + request.marker})
          .packet_name ("DirectSpotCommand")
          .async ();
        co_await _context
          .publish (e2e::mesh_topic,
                    e2e::mesh_event_t{"evt-sm-c3", "sm-c3-publish-" + request.marker})
          .async ();
        _state.record ("SpotToSpotOutbound", {}, source_spot,
                       "target=" + request.target_spot_rid + "|value=" + reply.value);
        co_return e2e::spot_to_spot_route_res_t{
          .source_spot_rid = source_spot,
          .target_spot_rid = request.target_spot_rid,
          .target_value = reply.value};
    }

    zlink::framework::task_t<e2e::spot_to_spot_timeout_route_res_t>
    spot_to_spot_timeout (const e2e::spot_to_spot_route_req_t &request)
    {
        const auto target_node =
          zlink::framework::node_rid_t::from_string (request.target_node_rid);
        const auto target_spot =
          zlink::framework::spot_rid_t::from_string (request.target_spot_rid);
        bool failed = false;
        try {
            (void) co_await _context
              .request_to<e2e::direct_spot_res_t> (
                target_node, target_spot, e2e::slow_spot_req_t{"sm-c3-" + request.marker})
              .packet_name ("SlowSpotReq")
              .timeout (std::chrono::milliseconds (50))
              .async ();
        }
        catch (...) {
            failed = true;
        }
        const auto source_spot = std::string (_context.spot_rid ().value ());
        _state.record ("SpotToSpotTimeout", {}, source_spot,
                       "target=" + request.target_spot_rid + "|failed="
                         + std::string (failed ? "true" : "false"));
        co_return e2e::spot_to_spot_timeout_route_res_t{
          .source_spot_rid = source_spot,
          .target_spot_rid = request.target_spot_rid,
          .failed = failed};
    }

    zlink::framework::task_t<e2e::spot_to_spot_negative_route_res_t>
    spot_to_spot_negative (const e2e::spot_to_spot_route_req_t &request)
    {
        const auto target_node =
          zlink::framework::node_rid_t::from_string (request.target_node_rid);
        const auto target_spot =
          zlink::framework::spot_rid_t::from_string (request.target_spot_rid);
        const auto source_spot = std::string (_context.spot_rid ().value ());
        bool request_failed = false;
        try {
            (void) co_await _context
              .request_to<e2e::direct_spot_res_t> (
                target_node, target_spot, e2e::direct_spot_req_t{source_spot, request.marker})
              .packet_name ("MissingSpotReq")
              .timeout (std::chrono::milliseconds (1000))
              .async ();
        }
        catch (...) {
            request_failed = true;
        }
        co_await _context
          .send_to (target_node, target_spot,
                    e2e::direct_spot_command_t{source_spot,
                                               "missing-" + request.marker})
          .packet_name ("MissingSpotCommand")
          .async ();
        _state.record ("SpotToSpotNegative", {}, source_spot,
                       "target=" + request.target_spot_rid + "|requestFailed="
                         + std::string (request_failed ? "true" : "false"));
        co_return e2e::spot_to_spot_negative_route_res_t{
          .source_spot_rid = source_spot,
          .target_spot_rid = request.target_spot_rid,
          .request_failed = request_failed};
    }

    e2e::type_mismatch_res_t type_mismatch (const scenario_actor_t &actor,
                                            zlink::framework::spot_actor_request_context_t &,
                                            const e2e::type_mismatch_req_t &request)
    {
        try {
            (void) _context.manager ().get_or_create_spot (e2e::alternate_spot,
                                                           _context.spot_rid (), request);
        }
        catch (const zlink::framework::framework_exception_t &error) {
            if (error.kind () == zlink::framework::framework_error_kind_t::spot_type_mismatch) {
                const auto spot_name =
                  _context.manager ().spot_name_for (_context.spot_rid ()).value_or ("");
                _state.record ("SpotTypeMismatch", actor.actor_id,
                               std::string (_context.spot_rid ().value ()), spot_name);
                return {.rejected = true,
                        .error_kind = "spot_type_mismatch",
                        .spot_name = spot_name,
                        .value = _value};
            }
            throw;
        }
        return {.rejected = false,
                .error_kind = "none",
                .spot_name = _context.manager ().spot_name_for (_context.spot_rid ()).value_or (""),
                .value = _value};
    }

    zlink::framework::task_t<e2e::actor_push_res_t>
    push_to_session (scenario_actor_t &actor,
                     zlink::framework::spot_actor_request_context_t &,
                     const e2e::actor_push_req_t &request)
    {
        co_await actor.context.bound_session ()
          .send (e2e::actor_push_notify_t{actor.actor_id, request.value})
          .async ();
        _state.record ("ActorPushedSession", actor.actor_id,
                       std::string (_context.spot_rid ().value ()), request.value);
        co_return e2e::actor_push_res_t{true, actor.actor_id};
    }

    void on_mesh_event (const e2e::mesh_event_t &event)
    {
        _state.record ("MeshEventReceived", {}, std::string (_context.spot_rid ().value ()),
                       event.event_id + ":" + event.value);
    }

  private:
    scenario_state_t &_state;
    zlink::framework::spot_context_t _context;
    int _value = 0;
    int _sequence = 0;
};

class entry_spot_t : public zlink::framework::entry_spot_t
{
  public:
    explicit entry_spot_t (scenario_state_t &state) : _state (state) {}

    void configure (zlink::framework::entry_spot_context_t &context)
    {
        _context = context;
        context.handlers ().add_actor_packet<&entry_spot_t::join> ("JoinReq");
        context.handlers ().add_actor_packet<&entry_spot_t::ping> ("ActorPingReq");
        context.handlers ().add_actor_packet<&entry_spot_t::slow_ping> ("SlowActorPingReq");
        context.handlers ().add_actor_packet<&entry_spot_t::push_to_session> ("PushReq");
        context.handlers ().add_actor_packet<&entry_spot_t::destroy_actor> ("DestroyActorReq");
    }

    void configure (zlink::framework::spot_context_t &context)
    {
        zlink::framework::entry_spot_context_t entry_context (context);
        configure (entry_context);
    }

    void onCreateActor (const scenario_actor_t &actor)
    {
        _state.record ("ActorCreated", actor.actor_id, std::string (_context.spot_rid ().value ()));
    }

    void on_actor_joined (const scenario_actor_t &actor)
    {
        _state.record ("EntryActorJoined", actor.actor_id,
                       std::string (_context.spot_rid ().value ()));
    }

    zlink::framework::task_t<e2e::join_res_t>
    join (scenario_actor_t &actor,
          zlink::framework::spot_actor_request_context_t &,
          const e2e::join_req_t &request)
    {
        const auto rid = user_spot_rid (request.key);
        _context.manager ().get_or_create_spot (e2e::user_spot, rid, request);
        _state.record ("EntryJoin", actor.actor_id, std::string (_context.spot_rid ().value ()),
                       request.key);
        try {
            auto joined =
              co_await actor.context.join_spot (rid, request).async<e2e::join_res_t> ();
            co_return joined.reply;
        }
        catch (const zlink::framework::framework_exception_t &error) {
            co_return zlink::framework::result_t<e2e::join_res_t>::failure (
              error.kind (), error.what (), error.is_retriable ());
        }
        catch (const std::exception &error) {
            co_return zlink::framework::result_t<e2e::join_res_t>::failure (
              zlink::framework::framework_error_kind_t::request_failed, error.what ());
        }
    }

    zlink::framework::task_t<e2e::destroy_actor_res_t>
    destroy_actor (scenario_actor_t &actor,
                   zlink::framework::spot_actor_request_context_t &,
                   const e2e::destroy_actor_req_t &request)
    {
        co_await _context.destroyActor (actor.actor_ref, actor);
        _state.record ("ActorDestroyed", actor.actor_id,
                       std::string (_context.spot_rid ().value ()), request.reason);
        co_return e2e::destroy_actor_res_t{true, actor.actor_id};
    }

    e2e::actor_ping_res_t ping (scenario_actor_t &actor,
                                zlink::framework::spot_actor_request_context_t &,
                                const e2e::actor_ping_req_t &request)
    {
        ++actor.ping_seen;
        _state.record ("EntryActorPing", actor.actor_id,
                       std::string (_context.spot_rid ().value ()),
                       request.value + ":" + std::to_string (actor.ping_seen));
        return {.actor_id = actor.actor_id,
                .node_rid = _state.node_rid,
                .spot_rid = std::string (_context.spot_rid ().value ()),
                .value = request.value,
                .seen = actor.ping_seen};
    }

    e2e::actor_ping_res_t slow_ping (scenario_actor_t &actor,
                                     zlink::framework::spot_actor_request_context_t &,
                                     const e2e::slow_actor_ping_req_t &request)
    {
        std::this_thread::sleep_for (std::chrono::milliseconds (request.delay_ms));
        ++actor.ping_seen;
        _state.record ("EntryActorSlowPing", actor.actor_id,
                       std::string (_context.spot_rid ().value ()),
                       request.value + ":" + std::to_string (actor.ping_seen));
        return {.actor_id = actor.actor_id,
                .node_rid = _state.node_rid,
                .spot_rid = std::string (_context.spot_rid ().value ()),
                .value = request.value,
                .seen = actor.ping_seen};
    }

    zlink::framework::task_t<e2e::actor_push_res_t>
    push_to_session (scenario_actor_t &actor,
                     zlink::framework::spot_actor_request_context_t &,
                     const e2e::actor_push_req_t &request)
    {
        co_await actor.context.bound_session ()
          .send (e2e::actor_push_notify_t{actor.actor_id, request.value})
          .async ();
        _state.record ("EntryActorPushedSession", actor.actor_id,
                       std::string (_context.spot_rid ().value ()), request.value);
        co_return e2e::actor_push_res_t{true, actor.actor_id};
    }

  private:
    scenario_state_t &_state;
    zlink::framework::entry_spot_context_t _context;
};

class alternate_user_spot_t : public zlink::framework::spot_t
{
};

class spot_lifecycle_handler_t
{
  public:
    using dependency_types =
      zlink::framework::dependency_list_t<scenario_state_t, zlink::framework::spot_node_manager_t>;
    using request_type = e2e::lifecycle_req_t;
    using reply_type = e2e::lifecycle_res_t;

    spot_lifecycle_handler_t (scenario_state_t &state,
                              zlink::framework::spot_node_manager_t &spots) :
        _state (state), _spots (spots)
    {
    }

    e2e::lifecycle_res_t handle (const e2e::lifecycle_req_t &request,
                                 const zlink::framework::route_handler_context_t &)
    {
        const auto rid = user_spot_rid (request.key);
        const auto created = _spots.get_or_create_spot (e2e::user_spot, rid, request);
        const auto closed = _spots.close_spot (rid).result ();
        _state.record ("SpotLifecycleClosed", {}, std::string (rid.value ()),
                       closed && closed.value () ? "closed" : "not-closed");
        return {.spot_rid = std::string (created.spot_rid.value ()),
                .created = created.state == zlink::framework::spot_create_state_t::created,
                .closed = closed && closed.value ()};
    }

  private:
    scenario_state_t &_state;
    zlink::framework::spot_node_manager_t &_spots;
};

class ensure_actor_handler_t
{
  public:
    using dependency_types =
      zlink::framework::dependency_list_t<scenario_state_t, zlink::framework::spot_node_manager_t>;
    using request_type = e2e::ensure_actor_req_t;
    using reply_type = e2e::ensure_actor_res_t;

    ensure_actor_handler_t (scenario_state_t &state, zlink::framework::spot_node_manager_t &spots) :
        _state (state), _spots (spots)
    {
    }

    e2e::ensure_actor_res_t handle (const e2e::ensure_actor_req_t &request,
                                    const zlink::framework::route_handler_context_t &)
    {
        auto current = _spots.current_actor_ref (zlink::framework::actor_ref_t (
          zlink::framework::node_rid_t::from_string (_state.node_rid), e2e::actor_type,
          request.actor_id, 0));
        if (current) {
            _state.record ("ActorEnsured", request.actor_id, {}, request.display_name);
            return {.actor = from_actor_ref (*current)};
        }
        const auto generation = ++_generation;
        _state.record ("ActorEnsured", request.actor_id, {}, request.display_name);
        return {.actor = {.node_rid = _state.node_rid,
                          .actor_type = e2e::actor_type,
                          .actor_id = request.actor_id,
                          .generation = generation}};
    }

  private:
    scenario_state_t &_state;
    zlink::framework::spot_node_manager_t &_spots;
    std::uint64_t _generation = 0;
};

class evidence_handler_t
{
  public:
    using dependency_types = zlink::framework::dependency_list_t<scenario_state_t>;

    explicit evidence_handler_t (scenario_state_t &state) : _state (state) {}

    zlink::framework::http_response_t handle (const zlink::framework::http_request_t &)
    {
        zlink::framework::http_response_t response;
        response.body = nlohmann::json (_state.snapshot ()).dump ();
        return response;
    }

  private:
    scenario_state_t &_state;
};

class stream_session_t final : public zlink::framework::packet_stream_session_t
{
  public:
    using dependency_types =
      zlink::framework::dependency_list_t<scenario_state_t,
                                          zlink::framework::route_client_t,
                                          zlink::framework::session_actor_manager_t,
                                          zlink::framework::actor_gateway_t>;

    stream_session_t (scenario_state_t &state,
                      zlink::framework::route_client_t &routes,
                      zlink::framework::session_actor_manager_t &actors,
                      zlink::framework::actor_gateway_t &gateway) :
        _state (state), _routes (routes), _actors (actors), _gateway (gateway)
    {
    }

    zlink::framework::task_t<void> on_connected (zlink::framework::stream_t &stream) override
    {
        _state.record ("StreamConnected", {}, {}, stream.session_id ());
        co_return;
    }

    zlink::framework::task_t<void> on_disconnected (zlink::framework::stream_t &) override
    {
        for (const auto &[actor_id, _] : _bound_actors) {
            if (_notify_on_disconnect.contains (actor_id)) {
                if (auto actor = _bound_session_actors.find (actor_id);
                    actor != _bound_session_actors.end ()) {
                    co_await actor->second.notify_disconnected ().async ();
                    _state.record ("StreamDisconnectNotified", actor_id);
                }
            }
            _gateway.unbind_session_stream (actor_id);
            _actors.unbind_session (actor_id);
            _state.record ("StreamUnbound", actor_id);
        }
        _bound_actors.clear ();
        _bound_session_actors.clear ();
        _notify_on_disconnect.clear ();
        co_return;
    }

    zlink::framework::task_t<void> on_error (zlink::framework::stream_t &,
                                             const zlink::framework::stream_error_t &error) override
    {
        _state.record ("StreamError", {}, {}, std::string (error.message ()));
        co_return;
    }

    zlink::framework::task_t<void> on_packet (
      zlink::framework::stream_t &stream,
      const zlink::framework::stream_dispatch_context_t &dispatch,
      const zlink::message_t &payload) override
    {
        if (dispatch.packet_name () == "StreamAuthReq") {
            auto request = payload.parse_json<e2e::stream_auth_req_t> ();
            if (request.actor.actor_id.empty () || request.actor.actor_type.empty ()
                || (request.target_node_rid != "play-a" && request.target_node_rid != "play-b")) {
                _state.record ("StreamAuthFailed", request.actor_id, {}, request.target_node_rid);
                throw zlink::framework::framework_exception_t (
                  zlink::framework::framework_error_kind_t::request_protocol_error,
                  "stream auth target or actor ref is invalid");
            }
            auto bound = co_await _actors.bind (to_actor_ref (request.actor)).async ();
            const auto actor_id = std::string (bound.actor_id ());
            _bound_actors[actor_id] = request.target_node_rid;
            _bound_session_actors[actor_id] = bound;
            if (actor_id.find ("disconnect-d5-notified") != std::string::npos) {
                _notify_on_disconnect.insert (actor_id);
            }
            _gateway.bind_session_stream (actor_id, stream, zlink::framework::stream_codec_t::json);
            _state.record ("StreamBound", actor_id, {},
                           request.target_node_rid + ":" + stream.session_id ());
            if (dispatch.can_reply ()) {
                co_await stream
                  .reply_packet (
                    zlink::message_t::from_json (
                      e2e::stream_auth_res_t{request.actor, _state.node_rid}))
                  .async ();
            }
            co_return;
        }
        if (dispatch.packet_name () == "StreamEnsureAuthReq") {
            auto request = payload.parse_json<e2e::stream_ensure_auth_req_t> ();
            if (request.target_node_rid != "play-a" && request.target_node_rid != "play-b") {
                _state.record ("StreamAuthFailed", request.actor_id, {}, request.target_node_rid);
                throw zlink::framework::framework_exception_t (
                  zlink::framework::framework_error_kind_t::request_protocol_error,
                  "stream ensure auth target is invalid");
            }
            auto ensured =
              co_await _routes
                .request (e2e::route_channel, zlink::routing_id_t::from (request.target_node_rid),
                          e2e::ensure_actor_req_t{request.actor_id, request.display_name})
                .packet_name ("EnsureActor")
                .async<e2e::ensure_actor_res_t> ();
            auto bound = co_await _actors.bind (to_actor_ref (ensured.actor)).async ();
            const auto actor_id = std::string (bound.actor_id ());
            _bound_actors[actor_id] = request.target_node_rid;
            _bound_session_actors[actor_id] = bound;
            if (actor_id.find ("disconnect-d5-notified") != std::string::npos) {
                _notify_on_disconnect.insert (actor_id);
            }
            _gateway.bind_session_stream (actor_id, stream, zlink::framework::stream_codec_t::json);
            _state.record ("StreamBound", actor_id, {},
                           request.target_node_rid + ":" + stream.session_id ());
            if (dispatch.can_reply ()) {
                co_await stream
                  .reply_packet (
                    zlink::message_t::from_json (
                      e2e::stream_auth_res_t{ensured.actor, _state.node_rid}))
                  .async ();
            }
            co_return;
        }

        auto actor = require_bound_actor (dispatch, std::string (dispatch.packet_name ()));
        if (!actor) {
            throw zlink::framework::framework_exception_t (
              actor.error_kind (),
              actor.error () ? actor.error ()->what () : "bound actor route is not found");
        }
        if (dispatch.can_reply ()) {
            auto reply = co_await actor.value ().relay_request (payload).async ();
            co_await stream.reply_packet (reply).async ();
            co_return;
        }
        co_await actor.value ().relay (payload).async ();
        co_return;
    }

  private:
    zlink::framework::result_t<zlink::framework::session_actor_t>
    require_bound_actor (const zlink::framework::stream_dispatch_context_t &dispatch,
                         const std::string &packet_name) const
    {
        if (_bound_actors.empty ()) {
            return zlink::framework::result_t<zlink::framework::session_actor_t>::failure (
              zlink::framework::framework_error_kind_t::actor_session_not_bound,
              "stream session is not bound before " + packet_name);
        }
        std::string actor_id;
        if (auto selected = dispatch.metadata ().find ("actor-id")) {
            actor_id = std::string (*selected);
        } else if (_bound_actors.size () == 1) {
            actor_id = _bound_actors.begin ()->first;
        } else {
            return zlink::framework::result_t<zlink::framework::session_actor_t>::failure (
              zlink::framework::framework_error_kind_t::actor_route_not_found,
              "actor-id metadata is required when multiple actors are bound for " + packet_name);
        }
        if (!_bound_actors.contains (actor_id)) {
            return zlink::framework::result_t<zlink::framework::session_actor_t>::failure (
              zlink::framework::framework_error_kind_t::actor_route_not_found,
              "bound actor route is not found for " + actor_id + " / " + packet_name);
        }
        auto actor = _actors.find (actor_id);
        if (!actor) {
            return zlink::framework::result_t<zlink::framework::session_actor_t>::failure (
              zlink::framework::framework_error_kind_t::actor_route_not_found,
              "bound actor route is not found for " + packet_name);
        }
        return zlink::framework::result_t<zlink::framework::session_actor_t>::success (
          std::move (*actor));
    }

    scenario_state_t &_state;
    zlink::framework::route_client_t &_routes;
    zlink::framework::session_actor_manager_t &_actors;
    zlink::framework::actor_gateway_t &_gateway;
    std::map<std::string, std::string> _bound_actors;
    std::map<std::string, zlink::framework::session_actor_t> _bound_session_actors;
    std::set<std::string> _notify_on_disconnect;
};

void configure_codecs (zlink::framework::codec_options_builder_t codecs)
{
    codecs.add_json ();
    codecs.add_json<e2e::actor_ref_dto_t,
                    e2e::ensure_actor_req_t,
                    e2e::ensure_actor_res_t,
                    e2e::join_req_t,
                    e2e::join_res_t,
                    e2e::state_req_t,
                    e2e::remote_actor_flow_req_t,
                    e2e::remote_actor_flow_res_t,
                    e2e::remote_actor_request_req_t,
                    e2e::missing_actor_req_t,
                    e2e::missing_actor_res_t,
                    e2e::actor_ping_req_t,
                    e2e::slow_actor_ping_req_t,
                    e2e::actor_ping_res_t,
                    e2e::complex_actor_req_t,
                    e2e::complex_actor_res_t,
                    e2e::spot_complex_actor_req_t,
                    e2e::spot_complex_actor_res_t,
                    e2e::spot_state_route_req_t,
                    e2e::state_res_t,
                    e2e::leave_req_t,
                    e2e::leave_res_t,
                    e2e::destroy_actor_req_t,
                    e2e::destroy_actor_res_t,
                    e2e::disconnect_req_t,
                    e2e::disconnect_res_t,
                    e2e::channel_echo_req_t,
                    e2e::channel_echo_res_t,
                    e2e::channel_control_ping_req_t,
                    e2e::channel_control_ping_res_t,
                    e2e::channel_command_t,
                    e2e::channel_slow_req_t,
                    e2e::channel_slow_res_t,
                    e2e::mesh_event_t,
                    e2e::outbound_req_t,
                    e2e::outbound_res_t,
                    e2e::spot_outbound_route_req_t,
                    e2e::spot_outbound_route_res_t,
                    e2e::worker_req_t,
                    e2e::spot_worker_req_t,
                    e2e::worker_res_t,
                    e2e::direct_spot_req_t,
                    e2e::direct_spot_route_req_t,
                    e2e::create_spot_req_t,
                    e2e::create_spot_res_t,
                    e2e::direct_spot_res_t,
                    e2e::direct_spot_command_t,
                    e2e::spot_state_command_route_req_t,
                    e2e::spot_state_command_route_res_t,
                    e2e::spot_publish_route_req_t,
                    e2e::spot_publish_route_res_t,
                    e2e::slow_spot_req_t,
                    e2e::spot_slow_route_req_t,
                    e2e::spot_slow_route_res_t,
                    e2e::unhandled_spot_req_t,
                    e2e::spot_missing_route_req_t,
                    e2e::spot_missing_route_res_t,
                    e2e::spot_to_spot_req_t,
                    e2e::spot_to_spot_res_t,
                    e2e::spot_to_spot_route_req_t,
                    e2e::spot_to_spot_route_res_t,
                    e2e::spot_to_spot_timeout_route_res_t,
                    e2e::spot_to_spot_negative_route_res_t,
                    e2e::type_mismatch_req_t,
                    e2e::type_mismatch_res_t,
                    e2e::lifecycle_req_t,
                    e2e::lifecycle_res_t,
                    e2e::close_spot_req_t,
                    e2e::close_spot_res_t,
                    e2e::stream_auth_req_t,
                    e2e::stream_ensure_auth_req_t,
                    e2e::stream_auth_res_t,
                    e2e::actor_push_req_t,
                    e2e::actor_push_res_t,
                    e2e::actor_push_notify_t,
                    e2e::evidence_entry_t,
                    e2e::evidence_snapshot_t> ();
}

} // namespace

inline int run_registry_server (int argc, char **argv)
{
    auto app = zlink::framework::app_t::create ();
    const auto log_dir = env_or ("ZLINK_CPP_E2E_LOG_DIR", "logs");
    const auto pub = env_or ("ZLINK_CPP_E2E_REGISTRY_PUB");
    const auto router = env_or ("ZLINK_CPP_E2E_REGISTRY_ROUTER");
    app.logging ()
      .use_file (log_dir + "/registry.log")
      .set_min_level (zlink::framework::log_level_t::debug);
    app.add_zlink_framework ([&] (zlink::framework::zlink_framework_options_t &options) {
        options.configure_dispatch ()
          .message_flow (zlink::framework::message_flow_log_mode_t::key_transitions)
          .trace_log_file (log_dir + "/registry-flow.log")
          .trace_label ("cpp-sm-registry");
        options.enable_registry (pub, router);
    });
    return app.run (argc, argv);
}

inline int run_play_server (int argc, char **argv)
{
    auto app = zlink::framework::app_t::create ();
    const auto log_dir = env_or ("ZLINK_CPP_E2E_LOG_DIR", "logs");
    const auto node_rid = env_or ("ZLINK_CPP_E2E_NODE_RID", "play-a");
    const auto route_endpoint = env_or ("ZLINK_CPP_E2E_ROUTE_ENDPOINT");
    const auto spot_router_endpoint = env_or ("ZLINK_CPP_E2E_SPOT_ROUTER_ENDPOINT");
    const auto pubsub_endpoint = env_or ("ZLINK_CPP_E2E_PUBSUB_ENDPOINT");
    const auto api_peer_endpoint = env_or ("ZLINK_CPP_E2E_API_PEER_ENDPOINT");
    const auto api_endpoint = env_or ("ZLINK_CPP_E2E_API_ENDPOINT");
    const auto publisher_endpoint = env_or ("ZLINK_CPP_E2E_PUBLISHER_ENDPOINT");
    const auto http_endpoint = env_or ("ZLINK_CPP_E2E_HTTP_ENDPOINT");
    const auto registry_router = env_or ("ZLINK_CPP_E2E_REGISTRY_ROUTER");
    const auto route_a_endpoint = env_or ("ZLINK_CPP_E2E_ROUTE_A_ENDPOINT");
    const auto route_b_endpoint = env_or ("ZLINK_CPP_E2E_ROUTE_B_ENDPOINT");

    app.logging ()
      .use_file (log_dir + "/" + node_rid + ".log")
      .set_min_level (zlink::framework::log_level_t::debug);
    app.add_zlink_framework ([&] (zlink::framework::zlink_framework_options_t &options) {
        auto state = std::make_unique<scenario_state_t> (node_rid);
        auto *state_ptr = state.get ();
        options.configure_dispatch ()
          .message_flow (zlink::framework::message_flow_log_mode_t::key_transitions)
          .trace_log_file (log_dir + "/" + node_rid + "-flow.log")
          .trace_label ("cpp-sm-" + node_rid);
        options.services ()
          .add_singleton<scenario_state_t> (std::move (state))
          .add_transient<ensure_actor_handler_t, scenario_state_t,
                         zlink::framework::spot_node_manager_t> ()
          .add_transient<spot_lifecycle_handler_t, scenario_state_t,
                         zlink::framework::spot_node_manager_t> ()
          .add_transient<join_spot_handler_t, scenario_state_t,
                         zlink::framework::session_actor_manager_t> ()
          .add_transient<complex_actor_handler_t, scenario_state_t,
                         zlink::framework::session_actor_manager_t> ()
          .add_transient<missing_actor_handler_t, scenario_state_t,
                         zlink::framework::session_actor_manager_t> ()
          .add_transient<remote_actor_flow_handler_t, scenario_state_t,
                         zlink::framework::session_actor_manager_t> ()
          .add_transient<ensure_user_spot_handler_t, scenario_state_t,
                         zlink::framework::spot_node_manager_t> ()
          .add_transient<remote_actor_request_handler_t, scenario_state_t,
                         zlink::framework::route_client_t,
                         zlink::framework::session_actor_manager_t> ()
          .add_transient<worker_spot_handler_t, zlink::framework::session_actor_manager_t> ()
          .add_transient<create_spot_handler_t, scenario_state_t,
                         zlink::framework::spot_node_manager_t> ()
          .add_transient<create_alternate_spot_handler_t, scenario_state_t,
                         zlink::framework::spot_node_manager_t> ()
          .add_transient<spot_state_command_route_handler_t,
                         zlink::framework::route_client_t> ()
          .add_transient<spot_publish_route_handler_t,
                         zlink::framework::spot_publisher_client_t> ()
          .add_transient<spot_slow_route_handler_t, zlink::framework::route_client_t> ()
          .add_transient<spot_missing_route_handler_t, zlink::framework::route_client_t> ()
          .add_transient<spot_outbound_route_handler_t, zlink::framework::route_client_t> ()
          .add_transient<spot_outbound_negative_route_handler_t,
                         zlink::framework::route_client_t> ()
          .add_transient<spot_to_spot_route_handler_t, zlink::framework::route_client_t> ()
          .add_transient<spot_to_spot_timeout_route_handler_t,
                         zlink::framework::route_client_t> ()
          .add_transient<spot_to_spot_negative_route_handler_t,
                         zlink::framework::route_client_t> ()
          .add_transient<lifecycle_spot_handler_t, scenario_state_t,
                         zlink::framework::spot_node_manager_t> ()
          .add_transient<close_spot_handler_t, scenario_state_t,
                         zlink::framework::spot_node_manager_t> ()
          .add_transient<type_mismatch_spot_handler_t, scenario_state_t,
                         zlink::framework::spot_node_manager_t,
                         zlink::framework::session_actor_manager_t> ();
        configure_codecs (options.codecs ());
        options.handlers ()
          .add<channel_echo_handler_t> (e2e::handler_group)
          .add_send<channel_command_handler_t> (e2e::handler_group)
          .add<channel_slow_handler_t> (e2e::handler_group);
        options.use_discovery ().add_registry_endpoint (registry_router);

        auto play_route = options.add_route_mesh (e2e::route_channel)
                            .enable_server (route_endpoint)
                            .set_routing_id (zlink::routing_id_t::from (node_rid))
                            .enable_client ()
                            .add_request_handler<ensure_actor_handler_t, e2e::ensure_actor_req_t,
                                                 e2e::ensure_actor_res_t> (
                              "EnsureActor", &ensure_actor_handler_t::handle)
                            .add_request_handler<ensure_user_spot_handler_t, e2e::join_req_t,
                                                 e2e::join_res_t> (
                              "EnsureUserSpot", &ensure_user_spot_handler_t::handle)
                            .add_request_handler<channel_echo_handler_t, e2e::channel_echo_req_t,
                                                 e2e::channel_echo_res_t> (
                              "ChannelEchoReq", &channel_echo_handler_t::route_handle)
                            .add_request_handler<spot_lifecycle_handler_t, e2e::lifecycle_req_t,
                                                 e2e::lifecycle_res_t> (
                              "LifecycleReq", &spot_lifecycle_handler_t::handle);
        if (!route_a_endpoint.empty ()) {
            play_route.enable_client (route_a_endpoint);
        }
        if (!route_b_endpoint.empty ()) {
            play_route.enable_client (route_b_endpoint);
        }
        auto api = options.add_client_server_channel (e2e::api_channel);
        if (!api_endpoint.empty ()) {
            api.enable_server (api_endpoint).use_handler_group (e2e::handler_group);
        }
        if (!api_peer_endpoint.empty ()) {
            api.enable_client (api_peer_endpoint);
        }
        if (!publisher_endpoint.empty ()) {
            options.add_fanout_channel (e2e::publisher_channel)
              .enable_publisher (publisher_endpoint);
        }
        options.add_spot_mesh (e2e::spot_mesh)
          .use_registry_spot_resolver (e2e::route_channel)
          .set_routing_id (zlink::routing_id_t::from (node_rid))
          .enable_router (spot_router_endpoint)
          .enable_pub_sub (pubsub_endpoint)
          .add_entry_spot<entry_spot_t> (
            [state_ptr] { return std::make_shared<entry_spot_t> (*state_ptr); })
          .add_spot<user_spot_t> (
            e2e::user_spot, [state_ptr] { return std::make_shared<user_spot_t> (*state_ptr); })
          .add_spot<alternate_user_spot_t> (e2e::alternate_spot)
          .add_actor_factory<scenario_actor_factory_t> (e2e::actor_type);
        options.http ()
          .listen (http_endpoint)
          .map_health ("/health")
          .map_get<evidence_handler_t> ("/evidence")
          .map_post<join_spot_handler_t> ("/spot/join")
          .map_post<complex_actor_handler_t> ("/spot/complex")
          .map_post<missing_actor_handler_t> ("/spot/missing-actor")
          .map_post<mutate_spot_state_handler_t> ("/spot/state")
          .map_post<remote_actor_flow_handler_t> ("/spot/remote-actor")
          .map_post<remote_actor_request_handler_t> ("/spot/remote-actor-request")
          .map_post<worker_spot_handler_t> ("/spot/worker/start")
          .map_post<create_spot_handler_t> ("/spot/create")
          .map_post<create_alternate_spot_handler_t> ("/spot/create-alternate")
          .map_post<route_spot_state_handler_t> ("/spot/state/request")
          .map_post<spot_state_command_route_handler_t> ("/spot/state/command")
          .map_post<spot_publish_route_handler_t> ("/spot/publish")
          .map_post<spot_slow_route_handler_t> ("/spot/slow/request")
          .map_post<spot_missing_route_handler_t> ("/spot/missing-route")
          .map_post<spot_outbound_route_handler_t> ("/spot/outbound")
          .map_post<spot_outbound_negative_route_handler_t> ("/spot/outbound-negative")
          .map_post<spot_to_spot_route_handler_t> ("/spot/to-spot/request")
          .map_post<spot_to_spot_timeout_route_handler_t> ("/spot/to-spot/timeout")
          .map_post<spot_to_spot_negative_route_handler_t> ("/spot/to-spot/negative")
          .map_post<direct_spot_route_handler_t> ("/spot/direct")
          .map_post<channel_control_ping_route_handler_t> ("/channel/control-ping")
          .map_post<lifecycle_spot_handler_t> ("/spot/lifecycle")
          .map_post<close_spot_handler_t> ("/spot/close")
          .map_post<type_mismatch_spot_handler_t> ("/spot/type-mismatch");
    });
    return app.run (argc, argv);
}

inline int run_session_server (int argc, char **argv)
{
    auto app = zlink::framework::app_t::create ();
    const auto log_dir = env_or ("ZLINK_CPP_E2E_LOG_DIR", "logs");
    const auto node_rid = env_or ("ZLINK_CPP_E2E_NODE_RID", "session-a");
    const auto route_endpoint = env_or ("ZLINK_CPP_E2E_ROUTE_ENDPOINT");
    const auto spot_router_endpoint = env_or ("ZLINK_CPP_E2E_SPOT_ROUTER_ENDPOINT");
    const auto pubsub_endpoint = env_or ("ZLINK_CPP_E2E_PUBSUB_ENDPOINT");
    const auto http_endpoint = env_or ("ZLINK_CPP_E2E_HTTP_ENDPOINT");
    const auto registry_router = env_or ("ZLINK_CPP_E2E_REGISTRY_ROUTER");
    const auto route_a_endpoint = env_or ("ZLINK_CPP_E2E_ROUTE_A_ENDPOINT");
    const auto route_b_endpoint = env_or ("ZLINK_CPP_E2E_ROUTE_B_ENDPOINT");
    const auto stream_endpoint = env_or ("ZLINK_CPP_E2E_STREAM_ENDPOINT");

    app.logging ()
      .use_file (log_dir + "/" + node_rid + ".log")
      .set_min_level (zlink::framework::log_level_t::debug);
    app.add_zlink_framework ([&] (zlink::framework::zlink_framework_options_t &options) {
        auto state = std::make_unique<scenario_state_t> (node_rid);
        options.configure_dispatch ()
          .message_flow (zlink::framework::message_flow_log_mode_t::key_transitions)
          .trace_log_file (log_dir + "/" + node_rid + "-flow.log")
          .trace_label ("cpp-sm-" + node_rid);
        options.services ().add_singleton<scenario_state_t> (std::move (state));
        configure_codecs (options.codecs ());
        options.use_discovery ().add_registry_endpoint (registry_router);

        auto route = options.add_route_mesh (e2e::route_channel)
                       .enable_server (route_endpoint)
                       .set_routing_id (zlink::routing_id_t::from (node_rid))
                       .enable_client ();
        if (!route_a_endpoint.empty ()) {
            route.enable_client (route_a_endpoint);
        }
        if (!route_b_endpoint.empty ()) {
            route.enable_client (route_b_endpoint);
        }
        options.add_spot_mesh (e2e::spot_mesh)
          .use_registry_spot_resolver (e2e::route_channel)
          .set_routing_id (zlink::routing_id_t::from (node_rid))
          .enable_router (spot_router_endpoint)
          .enable_pub_sub (pubsub_endpoint);
        options.add_stream_node ("spot-service-stream")
          .bind (stream_endpoint)
          .register_session<stream_session_t> ();
        options.http ()
          .listen (http_endpoint)
          .map_health ("/health")
          .map_get<evidence_handler_t> ("/evidence")
          .map_post<channel_control_ping_route_handler_t> ("/channel/control-ping");
    });
    return app.run (argc, argv);
}
