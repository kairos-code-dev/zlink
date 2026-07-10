/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "play_spot_types.hpp"
#include "../Handlers/play_basic_spot_handlers.hpp"
#include "../Handlers/play_failure_spot_handlers.hpp"
#include "../Handlers/play_remote_spot_handlers.hpp"
#include "../Handlers/play_timer_spot_handlers.hpp"
#include "../Support/play_support.hpp"

#include <zlink/framework.hpp>

#include <chrono>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

namespace zlink::framework::e2e::yield_dispatch::server::play
{

namespace yd = zlink::framework::e2e::yield_dispatch;

class yield_probe_spot_t : public zlink::framework::spot_t
{
  public:
    explicit yield_probe_spot_t (evidence_store_t &evidence) : _evidence (evidence) {}

    void configure (zlink::framework::spot_context_t &context)
    {
        _context = context;
        context.handlers ()
          .add_handler<&yield_probe_spot_t::hold_req> (yd::hold_req_t::packet_name)
          .add_handler<&yield_probe_spot_t::hold_command> (yd::hold_msg_t::packet_name)
          .add_handler<&yield_probe_spot_t::yield_req> (yd::yield_req_t::packet_name)
          .add_handler<&yield_probe_spot_t::yield_command> (yd::yield_msg_t::packet_name)
          .add_handler<&yield_probe_spot_t::worker_yield_req> (
            yd::worker_yield_req_t::packet_name)
          .add_handler<&yield_probe_spot_t::worker_yield_command> (
            yd::worker_yield_msg_t::packet_name)
          .add_handler<&yield_probe_spot_t::yield_timeout_req> (
            yd::yield_timeout_req_t::packet_name)
          .add_handler<&yield_probe_spot_t::yield_timeout_command> (
            yd::yield_timeout_msg_t::packet_name)
          .add_handler<&yield_probe_spot_t::yield_cancel_req> (
            yd::yield_cancel_req_t::packet_name)
          .add_handler<&yield_probe_spot_t::yield_cancel_command> (
            yd::yield_cancel_msg_t::packet_name)
          .add_handler<&yield_probe_spot_t::remote_spot_yield_req> (
            yd::remote_spot_yield_req_t::packet_name)
          .add_handler<&yield_probe_spot_t::timer_start_command> (
            yd::timer_start_msg_t::packet_name)
          .add_handler<&yield_probe_spot_t::timer_stop_command> (
            yd::timer_stop_msg_t::packet_name)
          .add_handler<&yield_probe_spot_t::probe_req> (yd::probe_req_t::packet_name)
          .add_handler<&yield_probe_spot_t::probe_command> (yd::probe_msg_t::packet_name)
          .add_actor_request<&yield_probe_spot_t::actor_yield_req> (
            yd::actor_yield_req_t::packet_name)
          .add_actor_request<&yield_probe_spot_t::actor_fast_req> (
            yd::actor_fast_req_t::packet_name)
          .add_actor_request<&yield_probe_spot_t::actor_join_yield_req> (
            yd::actor_join_yield_req_t::packet_name)
          .add_actor_request<&yield_probe_spot_t::actor_push_yield_req> (
            yd::actor_push_yield_req_t::packet_name);
    }

    zlink::framework::spot_actor_join_response_t
    on_actor_join (std::string_view actor_id,
                   const zlink::framework::message_t &request_message)
    {
        const auto request = request_message.decode<yd::delay_req_t> ();
        const auto spot_rid = std::string (_context.spot_rid ().value ());
        _evidence.add ("actor-join-target-started|rid=" + _evidence.node_rid + "|spot="
                       + spot_rid + "|actor=" + std::string (actor_id) + "|request="
                       + request.request_id + "|handler=spot");
        std::this_thread::sleep_for (std::chrono::milliseconds (request.delay_ms));
        _evidence.add ("actor-join-target-completed|rid=" + _evidence.node_rid + "|spot="
                       + spot_rid + "|actor=" + std::string (actor_id) + "|request="
                       + request.request_id + "|handler=spot");
        return zlink::framework::spot_actor_join_response_t::accept (
          yd::delay_res_t{.request_id = request.request_id,
                          .marker = request.marker,
                          .node_rid = _evidence.node_rid});
    }

    zlink::framework::task_t<yd::yield_dispatch_res_t> hold_req (const yd::hold_req_t &request)
    {
        co_await handle_basic_hold (_context, _evidence, request.request_id, request.delay_ms);
        co_return basic_spot_reply (_context, _evidence, "YD-A1", request.request_id,
                                    "hold-completed");
    }

    zlink::framework::task_t<void> hold_command (const yd::hold_msg_t &request)
    {
        co_await handle_basic_hold (_context, _evidence, request.request_id, request.delay_ms);
    }

    zlink::framework::task_t<yd::yield_dispatch_res_t> yield_req (const yd::yield_req_t &request)
    {
        co_await handle_basic_yield (_context, _evidence, request.request_id, request.delay_ms,
                                     request.correlation_id);
        co_return basic_spot_reply (_context, _evidence, "YD-A2", request.request_id,
                                    "yield-completed");
    }

    zlink::framework::task_t<void> yield_command (const yd::yield_msg_t &request)
    {
        co_await handle_basic_yield (_context, _evidence, request.request_id, request.delay_ms,
                                     request.correlation_id);
    }

    zlink::framework::task_t<yd::yield_dispatch_res_t>
    worker_yield_req (const yd::worker_yield_req_t &request)
    {
        co_await handle_basic_worker_yield (_context, _evidence, request.request_id,
                                            request.delay_ms);
        co_return basic_spot_reply (_context, _evidence, "YD-A4", request.request_id,
                                    "worker-yield-completed");
    }

    zlink::framework::task_t<void> worker_yield_command (
      const yd::worker_yield_msg_t &request)
    {
        co_await handle_basic_worker_yield (_context, _evidence, request.request_id,
                                            request.delay_ms);
    }

    zlink::framework::task_t<yd::yield_timeout_res_t>
    yield_timeout_req (const yd::yield_timeout_req_t &request)
    {
        co_return co_await handle_yield_timeout (_context, _evidence, request);
    }

    zlink::framework::task_t<void> yield_timeout_command (
      const yd::yield_timeout_msg_t &request)
    {
        co_await handle_yield_timeout_command (_context, _evidence, request);
    }

    zlink::framework::task_t<yd::yield_cancel_res_t>
    yield_cancel_req (const yd::yield_cancel_req_t &request)
    {
        co_return co_await handle_yield_cancel (_context, _evidence, request);
    }

    zlink::framework::task_t<void> yield_cancel_command (
      const yd::yield_cancel_msg_t &request)
    {
        co_await handle_yield_cancel_command (_context, _evidence, request);
    }

    zlink::framework::task_t<yd::yield_dispatch_res_t>
    remote_spot_yield_req (const yd::remote_spot_yield_req_t &request)
    {
        co_return co_await handle_remote_spot_yield (_context, _evidence, request);
    }

    void timer_start_command (const yd::timer_start_msg_t &request)
    {
        handle_timer_start_command (_context, _evidence, _timers, _timer_mutex, request);
    }

    void timer_stop_command (const yd::timer_stop_msg_t &request)
    {
        handle_timer_stop_command (_timers, _timer_mutex, request);
    }

    yd::yield_dispatch_res_t probe_req (const yd::probe_req_t &request)
    {
        handle_basic_probe (_context, _evidence, request.request_id, request.marker);
        return basic_spot_reply (_context, _evidence, "YD-PROBE", request.request_id,
                                 request.marker);
    }

    void probe_command (const yd::probe_msg_t &request)
    {
        handle_basic_probe (_context, _evidence, request.request_id, request.marker);
    }

    zlink::framework::task_t<yd::actor_yield_res_t>
    actor_yield_req (yield_actor_t &actor,
                     zlink::framework::spot_actor_request_context_t &,
                     const yd::actor_yield_req_t &request)
    {
        const auto spot_rid = std::string (_context.spot_rid ().value ());
        const auto mailbox = "actor:" + actor.actor_id;
        _evidence.add ("actor-yield-started|rid=" + _evidence.node_rid + "|spot=" + spot_rid
                       + "|actor=" + actor.actor_id + "|mailbox=" + mailbox + "|request="
                       + request.request_id + "|handler=actor");
        auto call =
          _context.outbound ()
            .request (yd::delay_channel,
                      yd::delay_req_t{.request_id = request.request_id,
                                      .delay_ms = request.delay_ms,
                                      .marker = "actor-" + actor.actor_id})
            .packet_name (yd::delay_req_t::packet_name)
            .timeout (std::chrono::milliseconds (3000));
        _evidence.add ("actor-yield-released|rid=" + _evidence.node_rid + "|spot=" + spot_rid
                       + "|actor=" + actor.actor_id + "|mailbox=" + mailbox + "|request="
                       + request.request_id + "|handler=actor");
        co_await call.yield<yd::delay_res_t> ();
        _evidence.add ("actor-yield-resumed|rid=" + _evidence.node_rid + "|spot=" + spot_rid
                       + "|actor=" + actor.actor_id + "|mailbox=" + mailbox + "|request="
                       + request.request_id + "|handler=actor");
        _evidence.add ("actor-yield-completed|rid=" + _evidence.node_rid + "|spot=" + spot_rid
                       + "|actor=" + actor.actor_id + "|mailbox=" + mailbox + "|request="
                       + request.request_id + "|handler=actor");
        co_return actor_reply ("YD-B", request.request_id, actor.actor_id,
                               "actor-yield-completed");
    }

    yd::actor_yield_res_t actor_fast_req (yield_actor_t &actor,
                                          zlink::framework::spot_actor_request_context_t &,
                                          const yd::actor_fast_req_t &request)
    {
        const auto spot_rid = std::string (_context.spot_rid ().value ());
        const auto mailbox = "actor:" + actor.actor_id;
        _evidence.add ("actor-fast-started|rid=" + _evidence.node_rid + "|spot=" + spot_rid
                       + "|actor=" + actor.actor_id + "|mailbox=" + mailbox + "|request="
                       + request.request_id + "|marker=" + request.marker + "|handler=actor");
        _evidence.add ("actor-fast-completed|rid=" + _evidence.node_rid + "|spot=" + spot_rid
                       + "|actor=" + actor.actor_id + "|mailbox=" + mailbox + "|request="
                       + request.request_id + "|marker=" + request.marker + "|handler=actor");
        return actor_reply ("YD-B", request.request_id, actor.actor_id, request.marker);
    }

    zlink::framework::task_t<yd::actor_yield_res_t>
    actor_join_yield_req (yield_actor_t &actor,
                          zlink::framework::spot_actor_request_context_t &,
                          const yd::actor_join_yield_req_t &request)
    {
        const auto spot_rid = std::string (_context.spot_rid ().value ());
        const auto mailbox = "actor:" + actor.actor_id;
        _evidence.add ("actor-join-yield-started|rid=" + _evidence.node_rid + "|spot="
                       + spot_rid + "|actor=" + actor.actor_id + "|mailbox=" + mailbox
                       + "|request=" + request.request_id + "|target_node="
                       + request.target_node_rid + "|handler=actor");
        auto call = actor.context
                      .join_entry_spot (zlink::framework::node_rid_t::from_string (
                                          request.target_node_rid),
                                        yd::delay_req_t{.request_id = request.request_id,
                                                        .delay_ms = 350,
                                                        .marker = "join"})
                      .timeout (std::chrono::milliseconds (3000));
        _evidence.add ("actor-join-yield-released|rid=" + _evidence.node_rid + "|spot="
                       + spot_rid + "|actor=" + actor.actor_id + "|mailbox=" + mailbox
                       + "|request=" + request.request_id + "|target_node="
                       + request.target_node_rid + "|handler=actor");
        const auto joined = co_await call.yield<yd::delay_res_t> ();
        const auto accepted = joined.result_code == 0 ? "true" : "false";
        _evidence.add ("actor-join-yield-resumed|rid=" + _evidence.node_rid + "|spot="
                       + spot_rid + "|actor=" + actor.actor_id + "|mailbox=" + mailbox
                       + "|request=" + request.request_id + "|target_node="
                       + request.target_node_rid + "|accepted=" + accepted
                       + "|handler=actor");
        _evidence.add ("actor-join-yield-completed|rid=" + _evidence.node_rid + "|spot="
                       + spot_rid + "|actor=" + actor.actor_id + "|mailbox=" + mailbox
                       + "|request=" + request.request_id + "|target_node="
                       + request.target_node_rid + "|accepted=" + accepted
                       + "|handler=actor");
        co_return actor_reply ("YD-B3", request.request_id, actor.actor_id,
                               "actor-join-yield-completed");
    }

    zlink::framework::task_t<yd::actor_yield_res_t>
    actor_push_yield_req (yield_actor_t &actor,
                          zlink::framework::spot_actor_request_context_t &,
                          const yd::actor_push_yield_req_t &request)
    {
        const auto spot_rid = std::string (_context.spot_rid ().value ());
        const auto mailbox = "actor:" + actor.actor_id;
        _evidence.add ("actor-push-yield-started|rid=" + _evidence.node_rid + "|spot="
                       + spot_rid + "|actor=" + actor.actor_id + "|mailbox=" + mailbox
                       + "|request=" + request.request_id + "|handler=actor");
        auto call =
          _context.outbound ()
            .request (yd::delay_channel,
                      yd::delay_req_t{.request_id = request.request_id,
                                      .delay_ms = request.delay_ms,
                                      .marker = "actor-push-" + actor.actor_id})
            .packet_name (yd::delay_req_t::packet_name)
            .timeout (std::chrono::milliseconds (3000));
        _evidence.add ("actor-push-yield-released|rid=" + _evidence.node_rid + "|spot="
                       + spot_rid + "|actor=" + actor.actor_id + "|mailbox=" + mailbox
                       + "|request=" + request.request_id + "|handler=actor");
        co_await call.yield<yd::delay_res_t> ();
        _evidence.add ("actor-push-yield-resumed|rid=" + _evidence.node_rid + "|spot="
                       + spot_rid + "|actor=" + actor.actor_id + "|mailbox=" + mailbox
                       + "|request=" + request.request_id + "|handler=actor");
        auto pushed =
          actor.context.bound_session ()
            .send (yd::actor_push_notify_t{.actor_id = actor.actor_id,
                                           .request_id = request.request_id,
                                           .value = request.value,
                                           .node_rid = _evidence.node_rid})
            .packet_name (yd::actor_push_notify_t::packet_name)
            .submit ();
        if (!pushed) {
            const auto *error = pushed.error ();
            _evidence.add ("actor-push-yield-failed|rid=" + _evidence.node_rid + "|spot="
                           + spot_rid + "|actor=" + actor.actor_id + "|mailbox=" + mailbox
                           + "|request=" + request.request_id + "|reason="
                           + (error != nullptr ? error->what () : "actor bound push failed")
                           + "|handler=actor");
            throw zlink::framework::framework_exception_t (
              pushed.error_kind (), error != nullptr ? error->what ()
                                                     : "actor bound push failed");
        }
        _evidence.add ("actor-push-yield-completed|rid=" + _evidence.node_rid + "|spot="
                       + spot_rid + "|actor=" + actor.actor_id + "|mailbox=" + mailbox
                       + "|request=" + request.request_id + "|handler=actor");
        co_return actor_reply ("YD-D4", request.request_id, actor.actor_id,
                               "actor-push-yield-completed");
    }

    zlink::framework::task_t<void> handle_timer_tick (
      const zlink::framework::timer_tick_t &tick)
    {
        co_await play::handle_timer_tick (_context, _evidence, _timers, _timer_mutex, tick);
        co_return;
    }

  private:
    yd::actor_yield_res_t
    actor_reply (std::string scenario_id,
                 std::string request_id,
                 std::string actor_id,
                 std::string marker) const
    {
        return {.scenario_id = std::move (scenario_id),
                .request_id = std::move (request_id),
                .actor_id = std::move (actor_id),
                .spot_rid = std::string (_context.spot_rid ().value ()),
                .node_rid = _evidence.node_rid,
                .marker = std::move (marker)};
    }

    evidence_store_t &_evidence;
    zlink::framework::spot_context_t _context;
    std::mutex _timer_mutex;
    std::map<std::string, yield_timer_state_t> _timers;
};

inline zlink::framework::task_t<void>
yield_timer_handler_t::handle (yield_probe_spot_t &spot,
                               const zlink::framework::timer_tick_t &tick) const
{
    co_await spot.handle_timer_tick (tick);
}

} // namespace zlink::framework::e2e::yield_dispatch::server::play
