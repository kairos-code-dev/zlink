/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../../Shared/spot_service_contracts.hpp"
#include "../../Shared/scenario_state.hpp"
#include "../../Shared/spot_actor_support.hpp"

#include <zlink/framework.hpp>

#include <chrono>
#include <exception>
#include <iostream>
#include <string>
#include <thread>
#include <utility>

namespace e2e = zlink::framework::e2e::spot_service;

namespace
{

class user_spot_t;

struct user_spot_timer_handler_t
{
    void handle (user_spot_t &spot, const zlink::framework::timer_tick_t &tick) const;
};

struct user_spot_idle_close_timer_handler_t
{
    zlink::framework::task_t<void>
    handle (user_spot_t &spot, const zlink::framework::timer_tick_t &tick) const;
};

struct user_spot_overrun_timer_handler_t
{
    void handle (user_spot_t &spot, const zlink::framework::timer_tick_t &tick) const;
};

struct user_spot_stage_timer_handler_t
{
    void handle (user_spot_t &spot, const zlink::framework::timer_tick_t &tick) const;
};

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
        context.handlers ().add_handler<&user_spot_t::direct_command> ("DirectSpotMsg");
        context.handlers ().add_handler<&user_spot_t::start_worker> ("WorkerStartReq");
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
        context.handlers ().add_handler<&user_spot_t::start_idle_close> ("IdleCloseMsg");
        context.handlers ().add_handler<&user_spot_t::stage_probe> ("StageProbeReq");
        context.handlers ().add_handler<&user_spot_t::start_stage_timer> ("StageTimerStartMsg");
        context.handlers ().add_subscribe<&user_spot_t::on_mesh_event> (e2e::mesh_topic);
    }

    void on_initialize ()
    {
        _state.record ("SpotInitialized", {}, std::string (_context.spot_rid ().value ()));
        const auto spot_rid = std::string (_context.spot_rid ().value ());
        if (spot_rid.find ("sm-e2-timer") != std::string::npos) {
            _timer = _context.add_timer<user_spot_timer_handler_t> (
              "sm-e2-tick", std::chrono::milliseconds (100));
        }
    }

    zlink::framework::spot_create_response_t
    on_create (const zlink::framework::message_t &request)
    {
        if (!request.empty ()) {
            try {
                const auto command = request.decode<e2e::overrun_timer_msg_t> ();
                if (!command.name.empty () && command.period_ms > 0) {
                    start_overrun_timer (command);
                }
            }
            catch (const std::exception &) {
                try {
                    const auto command = request.decode<e2e::idle_close_msg_t> ();
                    if (!command.name.empty () && command.period_ms > 0) {
                        start_idle_close (command);
                    }
                }
                catch (const std::exception &) {
                    // Other create payloads use the normal spot lifecycle without a timer.
                }
            }
        }
        return zlink::framework::spot_create_response_t::accept ();
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

    void record_timer_tick (const zlink::framework::timer_tick_t &tick)
    {
        ++_timer_ticks;
        _state.record ("SpotTimerTick", {}, std::string (_context.spot_rid ().value ()),
                       tick.name + ":" + std::to_string (tick.delivery_index));
        if (_timer_ticks >= 2) {
            _timer.cancel ();
        }
    }

    void start_idle_close (const e2e::idle_close_msg_t &request)
    {
        _idle_close_timer = _context.add_timer<user_spot_idle_close_timer_handler_t> (
          request.name, std::chrono::milliseconds (request.period_ms));
    }

    void start_overrun_timer (const e2e::overrun_timer_msg_t &request)
    {
        zlink::framework::timer_options_t options;
        options.overrun_policy = overrun_policy_from_name (request.policy);
        options.max_catch_up_ticks = 2;
        _overrun_timer = _context.add_timer<user_spot_overrun_timer_handler_t> (
          request.name, std::chrono::milliseconds (request.period_ms), options);
    }

    zlink::framework::task_t<void> record_idle_close (const zlink::framework::timer_tick_t &tick)
    {
        if (tick.delivery_index > 1) {
            co_return;
        }
        auto closed = co_await _context.close ();
        _state.record ("SpotIdleTimerClosed", {}, std::string (_context.spot_rid ().value ()),
                       tick.name + ":closed=" + std::string (closed ? "true" : "false"));
    }

    void record_overrun_tick (const zlink::framework::timer_tick_t &tick)
    {
        ++_overrun_ticks;
        _state.record ("SpotTimerOverrun", {}, std::string (_context.spot_rid ().value ()),
                       tick.name + "|delivery=" + std::to_string (tick.delivery_index)
                         + "|scheduled=" + std::to_string (tick.scheduled_index)
                         + "|skipped=" + std::to_string (tick.skipped_ticks));
        if (_overrun_ticks >= 5) {
            _overrun_timer.cancel ();
            return;
        }
        std::this_thread::sleep_for (std::chrono::milliseconds (90));
    }

    e2e::state_res_t stage_probe (const e2e::stage_probe_req_t &request)
    {
        _value += request.delta;
        ++_sequence;
        _state.record ("StageRequest", {}, std::string (_context.spot_rid ().value ()),
                       request.marker + ":" + std::to_string (_value));
        return {.spot_rid = std::string (_context.spot_rid ().value ()),
                .owner_node_rid = _state.node_rid,
                .value = _value,
                .sequence = _sequence};
    }

    void start_stage_timer (const e2e::stage_timer_start_msg_t &request)
    {
        _stage_timer_ticks = 0;
        _stage_timer = _context.add_timer<user_spot_stage_timer_handler_t> (
          request.name, std::chrono::milliseconds (request.period_ms));
    }

    void record_stage_timer_tick (const zlink::framework::timer_tick_t &tick)
    {
        ++_stage_timer_ticks;
        _state.record ("StageTimer", {}, std::string (_context.spot_rid ().value ()),
                       tick.name + ":" + std::to_string (tick.delivery_index));
        if (_stage_timer_ticks >= 1) {
            _stage_timer.cancel ();
        }
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
        _context.outbound ()
          .send (e2e::api_channel,
                 e2e::channel_msg_t{"cmd-" + actor.actor_id + "-" + request.value})
          .submit ();
        _context
          .publish (e2e::mesh_topic, e2e::mesh_msg_t{"evt-" + actor.actor_id, request.value})
          .submit ();
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
        _context.outbound ()
          .send (e2e::api_channel, e2e::channel_msg_t{"notify-" + request.value})
          .submit ();
        _context
          .publish (e2e::mesh_topic, e2e::mesh_msg_t{"evt-sm-c2", "sm-c2-publish"})
          .submit ();
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
        _context.outbound ()
          .send (e2e::api_channel, e2e::channel_msg_t{"missing-" + request.value})
          .packet_name ("MissingChannelMsg")
          .submit ();
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

    e2e::spot_worker_start_res_t start_worker (const e2e::spot_worker_start_req_t &request)
    {
        const auto spot_rid = std::string (_context.spot_rid ().value ());
        _state.record ("WorkerStarted", {}, spot_rid, request.marker);
        _context
          .run_worker ([request] {
              std::this_thread::sleep_for (std::chrono::milliseconds (request.delay_ms));
              return request.marker;
          })
          .timeout (std::chrono::milliseconds (30000))
          .submit ([this, spot_rid] (zlink::framework::result_t<std::string> result)
                     -> zlink::framework::task_t<void> {
              if (result) {
                  _value += 100;
                  ++_sequence;
                  _state.record ("WorkerCompleted", {}, spot_rid, result.value ());
              }
              co_return;
          });
        return {.spot_rid = spot_rid, .owner_node_rid = _state.node_rid, .marker = request.marker};
    }

    void direct_command (const e2e::direct_spot_msg_t &request)
    {
        _state.record ("SpotToSpotMsg", request.source_actor_id,
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
        _context
          .send_to (target_node, target_spot,
                    e2e::direct_spot_msg_t{actor.actor_id, request.value + ":command"})
          .packet_name ("DirectSpotMsg")
          .submit ();
        _context
          .publish (e2e::mesh_topic,
                    e2e::mesh_msg_t{"evt-spot-to-spot", actor.actor_id + ":" + request.value})
          .submit ();
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
        _context
          .send_to (target_node, target_spot,
                    e2e::direct_spot_msg_t{source_spot,
                                               "sm-c3-send-" + request.marker})
          .packet_name ("DirectSpotMsg")
          .submit ();
        _context
          .publish (e2e::mesh_topic,
                    e2e::mesh_msg_t{"evt-sm-c3", "sm-c3-publish-" + request.marker})
          .submit ();
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
        _context
          .send_to (target_node, target_spot,
                    e2e::direct_spot_msg_t{source_spot,
                                               "missing-" + request.marker})
          .packet_name ("MissingSpotMsg")
          .submit ();
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
        actor.context.bound_session ()
          .send (e2e::actor_push_notify_t{actor.actor_id, request.value})
          .submit ();
        _state.record ("ActorPushedSession", actor.actor_id,
                       std::string (_context.spot_rid ().value ()), request.value);
        co_return e2e::actor_push_res_t{true, actor.actor_id};
    }

    void on_mesh_event (const e2e::mesh_msg_t &event)
    {
        _state.record ("MeshMsgReceived", {}, std::string (_context.spot_rid ().value ()),
                       event.event_id + ":" + event.value);
    }

  private:
    scenario_state_t &_state;
    zlink::framework::spot_context_t _context;
    zlink::framework::timer_t _timer;
    zlink::framework::timer_t _idle_close_timer;
    zlink::framework::timer_t _overrun_timer;
    zlink::framework::timer_t _stage_timer;
    int _value = 0;
    int _sequence = 0;
    int _timer_ticks = 0;
    int _overrun_ticks = 0;
    int _stage_timer_ticks = 0;

    static zlink::framework::timer_overrun_policy_t
    overrun_policy_from_name (const std::string &policy)
    {
        if (policy == "SkipLateTicks") {
            return zlink::framework::timer_overrun_policy_t::skip_late_ticks;
        }
        if (policy == "CatchUpBounded") {
            return zlink::framework::timer_overrun_policy_t::catch_up_bounded;
        }
        if (policy == "DelayNextTick") {
            return zlink::framework::timer_overrun_policy_t::delay_next_tick;
        }
        throw std::runtime_error ("unknown timer overrun policy: " + policy);
    }
};

inline void user_spot_timer_handler_t::handle (user_spot_t &spot,
                                               const zlink::framework::timer_tick_t &tick) const
{
    spot.record_timer_tick (tick);
}

inline zlink::framework::task_t<void>
user_spot_idle_close_timer_handler_t::handle (user_spot_t &spot,
                                              const zlink::framework::timer_tick_t &tick) const
{
    co_await spot.record_idle_close (tick);
}

inline void
user_spot_overrun_timer_handler_t::handle (user_spot_t &spot,
                                           const zlink::framework::timer_tick_t &tick) const
{
    spot.record_overrun_tick (tick);
}

inline void
user_spot_stage_timer_handler_t::handle (user_spot_t &spot,
                                         const zlink::framework::timer_tick_t &tick) const
{
    spot.record_stage_timer_tick (tick);
}

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
        try {
            const auto rid = user_spot_rid (request.key);
            _context.manager ().get_or_create_spot (e2e::user_spot, rid, request);
            _state.record ("EntryJoin", actor.actor_id,
                           std::string (_context.spot_rid ().value ()), request.key);
            auto joined =
              co_await actor.context.join_spot (rid, request).async<e2e::join_res_t> ();
            co_return joined.reply;
        }
        catch (const zlink::framework::framework_exception_t &error) {
            std::cerr << "entry spot join failed: kind="
                      << static_cast<int> (error.kind ())
                      << " message=" << error.what () << '\n';
            co_return zlink::framework::result_t<e2e::join_res_t>::failure (
              error.kind (), error.what (), error.is_retriable ());
        }
        catch (const std::exception &error) {
            std::cerr << "entry spot join failed: message=" << error.what () << '\n';
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
        actor.context.bound_session ()
          .send (e2e::actor_push_notify_t{actor.actor_id, request.value})
          .submit ();
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

} // namespace
