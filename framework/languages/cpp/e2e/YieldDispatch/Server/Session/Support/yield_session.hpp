/* SPDX-License-Identifier: MPL-2.0 */

#pragma once

#include "../../Shared/codecs.hpp"

#include <zlink/framework.hpp>

#include <chrono>
#include <map>
#include <string>

namespace zlink::framework::e2e::yield_dispatch::server::session {

namespace yd = zlink::framework::e2e::yield_dispatch;

class yield_session_t final : public zlink::framework::packet_stream_session_t
{
  public:
    using dependency_types =
      zlink::framework::dependency_list_t<zlink::framework::route_client_t,
                                          zlink::framework::session_actor_manager_t,
                                          zlink::framework::actor_gateway_t>;

    yield_session_t (zlink::framework::route_client_t &routes,
                     zlink::framework::session_actor_manager_t &actors,
                     zlink::framework::actor_gateway_t &gateway) :
        _routes (routes), _actors (actors), _gateway (gateway)
    {
    }

    zlink::framework::task_t<void> on_connected (zlink::framework::stream_t &) override
    {
        co_return;
    }

    zlink::framework::task_t<void> on_disconnected (zlink::framework::stream_t &) override
    {
        for (const auto &[actor_id, _] : _bound_actors) {
            _gateway.unbind_session_stream (actor_id);
            _actors.unbind_session (actor_id);
        }
        _bound_actors.clear ();
        co_return;
    }

    zlink::framework::task_t<void> on_error (zlink::framework::stream_t &,
                                             const zlink::framework::stream_error_t &) override
    {
        co_return;
    }

    zlink::framework::task_t<void> on_packet (
      zlink::framework::stream_t &stream,
      const zlink::framework::stream_dispatch_context_t &dispatch,
      const zlink::message_t &payload) override
    {
        const auto packet = std::string (dispatch.packet_name ());
        if (packet == yd::bind_yield_actors_req_t::packet_name) {
            auto request = payload.parse_json<yd::bind_yield_actors_req_t> ();
            auto reply = co_await request_control<yd::bind_yield_actors_reply_t> (
              request, packet, target_or_default (dispatch));
            for (const auto &actor : reply.actors) {
                (void) co_await _actors.bind (to_actor_ref (actor)).async ();
                _gateway.bind_session_stream (actor.actor_id, stream,
                                              zlink::framework::stream_codec_t::json);
                _bound_actors[actor.actor_id] = actor.node_rid;
            }
            co_await stream.reply_packet (zlink::message_t::from_json (reply)).async ();
            co_return;
        }
        if (packet == yd::ensure_spot_req_t::packet_name) {
            auto request = payload.parse_json<yd::ensure_spot_req_t> ();
            if (request.spot_rid.empty ()) {
                throw zlink::framework::framework_exception_t (
                  zlink::framework::framework_error_kind_t::request_protocol_error,
                  "EnsureSpotReq spot_rid is missing at session relay");
            }
            auto reply = co_await request_control<yd::ensure_spot_reply_t> (
              request, packet, target_or_default (dispatch));
            co_await stream.reply_packet (zlink::message_t::from_json (reply)).async ();
            co_return;
        }
        if (packet == yd::yield_evidence_req_t::packet_name) {
            auto reply = co_await request_control<yd::yield_evidence_reply_t> (
              payload.parse_json<yd::yield_evidence_req_t> (), packet, target_or_default (dispatch));
            co_await stream.reply_packet (zlink::message_t::from_json (reply)).async ();
            co_return;
        }
        if (packet == yd::yield_evidence_wait_req_t::packet_name) {
            auto reply = co_await request_control<yd::yield_evidence_reply_t> (
              payload.parse_json<yd::yield_evidence_wait_req_t> (), packet,
              target_or_default (dispatch));
            co_await stream.reply_packet (zlink::message_t::from_json (reply)).async ();
            co_return;
        }
        if (packet == yd::yield_shutdown_scenario_req_t::packet_name) {
            auto reply =
              co_await run_shutdown_through_spot_route (
                payload.parse_json<yd::yield_shutdown_scenario_req_t> ());
            co_await stream.reply_packet (zlink::message_t::from_json (reply)).async ();
            co_return;
        }
        if (packet == yd::yield_shutdown_recovery_req_t::packet_name) {
            auto reply =
              co_await run_shutdown_recovery_through_spot_route (
                payload.parse_json<yd::yield_shutdown_recovery_req_t> ());
            co_await stream.reply_packet (zlink::message_t::from_json (reply)).async ();
            co_return;
        }
        if (packet == yd::hold_req_t::packet_name) {
            auto reply = co_await request_spot<yd::yield_dispatch_reply_t> (
              target_or_default (dispatch), spot_rid (dispatch),
              payload.parse_json<yd::hold_req_t> (), packet);
            co_await stream.reply_packet (zlink::message_t::from_json (reply)).async ();
            co_return;
        }
        if (packet == yd::yield_req_t::packet_name) {
            auto reply = co_await request_spot<yd::yield_dispatch_reply_t> (
              target_or_default (dispatch), spot_rid (dispatch),
              payload.parse_json<yd::yield_req_t> (), packet);
            co_await stream.reply_packet (zlink::message_t::from_json (reply)).async ();
            co_return;
        }
        if (packet == yd::worker_yield_req_t::packet_name) {
            auto reply = co_await request_spot<yd::yield_dispatch_reply_t> (
              target_or_default (dispatch), spot_rid (dispatch),
              payload.parse_json<yd::worker_yield_req_t> (), packet);
            co_await stream.reply_packet (zlink::message_t::from_json (reply)).async ();
            co_return;
        }
        if (packet == yd::yield_timeout_req_t::packet_name) {
            auto reply = co_await request_spot<yd::yield_timeout_reply_t> (
              target_or_default (dispatch), spot_rid (dispatch),
              payload.parse_json<yd::yield_timeout_req_t> (), packet);
            co_await stream.reply_packet (zlink::message_t::from_json (reply)).async ();
            co_return;
        }
        if (packet == yd::remote_spot_yield_req_t::packet_name) {
            auto reply = co_await request_spot<yd::yield_dispatch_reply_t> (
              target_or_default (dispatch), spot_rid (dispatch),
              payload.parse_json<yd::remote_spot_yield_req_t> (), packet);
            co_await stream.reply_packet (zlink::message_t::from_json (reply)).async ();
            co_return;
        }
        if (packet == yd::probe_req_t::packet_name) {
            auto reply = co_await request_spot<yd::yield_dispatch_reply_t> (
              target_or_default (dispatch), spot_rid (dispatch),
              payload.parse_json<yd::probe_req_t> (), packet);
            co_await stream.reply_packet (zlink::message_t::from_json (reply)).async ();
            co_return;
        }
        if (packet == yd::hold_command_t::packet_name) {
            co_await send_spot (target_or_default (dispatch), spot_rid (dispatch),
                                payload.parse_json<yd::hold_command_t> (), packet);
            co_return;
        }
        if (packet == yd::yield_command_t::packet_name) {
            co_await send_spot (target_or_default (dispatch), spot_rid (dispatch),
                                payload.parse_json<yd::yield_command_t> (), packet);
            co_return;
        }
        if (packet == yd::worker_yield_command_t::packet_name) {
            co_await send_spot (target_or_default (dispatch), spot_rid (dispatch),
                                payload.parse_json<yd::worker_yield_command_t> (), packet);
            co_return;
        }
        if (packet == yd::yield_timeout_command_t::packet_name) {
            co_await send_spot (target_or_default (dispatch), spot_rid (dispatch),
                                payload.parse_json<yd::yield_timeout_command_t> (), packet);
            co_return;
        }
        if (packet == yd::timer_start_command_t::packet_name) {
            co_await send_spot (target_or_default (dispatch), spot_rid (dispatch),
                                payload.parse_json<yd::timer_start_command_t> (), packet);
            co_return;
        }
        if (packet == yd::timer_stop_command_t::packet_name) {
            co_await send_spot (target_or_default (dispatch), spot_rid (dispatch),
                                payload.parse_json<yd::timer_stop_command_t> (), packet);
            co_return;
        }
        if (packet == yd::probe_command_t::packet_name) {
            co_await send_spot (target_or_default (dispatch), spot_rid (dispatch),
                                payload.parse_json<yd::probe_command_t> (), packet);
            co_return;
        }
        auto actor = require_bound_actor (dispatch, packet);
        if (!actor) {
            throw zlink::framework::framework_exception_t (
              actor.error_kind (), actor.error () ? actor.error ()->what ()
                                                  : "bound actor route is not found");
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
    static zlink::framework::actor_ref_t to_actor_ref (const yd::yield_actor_binding_t &actor)
    {
        return zlink::framework::actor_ref_t (
          zlink::framework::node_rid_t::from_string (actor.node_rid), yd::actor_type,
          actor.actor_id, actor.generation);
    }

    zlink::framework::result_t<zlink::framework::session_actor_t>
    require_bound_actor (const zlink::framework::stream_dispatch_context_t &dispatch,
                         const std::string &packet) const
    {
        if (_bound_actors.empty ()) {
            return zlink::framework::result_t<zlink::framework::session_actor_t>::failure (
              zlink::framework::framework_error_kind_t::actor_session_not_bound,
              "stream session is not bound before " + packet);
        }
        std::string actor_id;
        if (auto selected = dispatch.metadata ().find (yd::actor_id_metadata)) {
            actor_id = std::string (*selected);
        } else if (_bound_actors.size () == 1) {
            actor_id = _bound_actors.begin ()->first;
        } else {
            return zlink::framework::result_t<zlink::framework::session_actor_t>::failure (
              zlink::framework::framework_error_kind_t::actor_route_not_found,
              "actor-id metadata is required when multiple actors are bound for " + packet);
        }
        if (!_bound_actors.contains (actor_id)) {
            return zlink::framework::result_t<zlink::framework::session_actor_t>::failure (
              zlink::framework::framework_error_kind_t::actor_route_not_found,
              "bound actor route is not found for " + actor_id + " / " + packet);
        }
        auto actor = _actors.find (actor_id);
        if (!actor) {
            return zlink::framework::result_t<zlink::framework::session_actor_t>::failure (
              zlink::framework::framework_error_kind_t::actor_route_not_found,
              "bound actor route is not found for " + packet);
        }
        return zlink::framework::result_t<zlink::framework::session_actor_t>::success (
          std::move (*actor));
    }

    template <typename TReply, typename TRequest>
    zlink::framework::task_t<TReply>
    request_control (const TRequest &request, const std::string &packet, zlink::routing_id_t target)
    {
        const auto reply =
          co_await _routes.request (yd::control_channel, target, request)
            .packet_name (packet)
            .timeout (std::chrono::milliseconds (5000))
            .template async<TReply> ();
        co_return reply;
    }

    zlink::framework::task_t<yd::yield_scenario_result_t>
    run_shutdown_through_spot_route (const yd::yield_shutdown_scenario_req_t &request)
    {
        const auto target = zlink::routing_id_t::from ("play-a");
        (void) co_await request_control<yd::ensure_spot_reply_t> (
          yd::ensure_spot_req_t{.spot_rid = request.spot_rid},
          yd::ensure_spot_req_t::packet_name, target);
        (void) co_await request_spot<yd::yield_dispatch_reply_t> (
          target, request.spot_rid,
          yd::yield_req_t{.request_id = request.request_id,
                          .delay_ms = request.delay_ms,
                          .correlation_id = "shutdown"},
          yd::yield_req_t::packet_name);
        auto evidence = co_await request_control<yd::yield_evidence_reply_t> (
          yd::yield_evidence_req_t{.request_id = request.request_id},
          yd::yield_evidence_req_t::packet_name, target);
        co_return yd::yield_scenario_result_t{
          .operation = "yield.e3-shutdown-unexpected-completion",
          .spot_rid = request.spot_rid,
          .evidence = std::move (evidence.evidence)};
    }

    zlink::framework::task_t<yd::yield_scenario_result_t>
    run_shutdown_recovery_through_spot_route (
      const yd::yield_shutdown_recovery_req_t &request)
    {
        const auto target = zlink::routing_id_t::from ("play-a");
        (void) co_await request_control<yd::ensure_spot_reply_t> (
          yd::ensure_spot_req_t{.spot_rid = request.spot_rid},
          yd::ensure_spot_req_t::packet_name, target);
        (void) co_await request_spot<yd::yield_dispatch_reply_t> (
          target, request.spot_rid,
          yd::probe_req_t{.request_id = request.request_id,
                          .marker = "shutdown-recovery-probe"},
          yd::probe_req_t::packet_name);
        auto evidence = co_await request_control<yd::yield_evidence_reply_t> (
          yd::yield_evidence_wait_req_t{.request_id = request.request_id,
                                        .marker = "probe-completed",
                                        .timeout_milliseconds = 20000},
          yd::yield_evidence_wait_req_t::packet_name, target);
        co_return yd::yield_scenario_result_t{
          .operation = "yield.e3-shutdown-recovery",
          .spot_rid = request.spot_rid,
          .evidence = std::move (evidence.evidence)};
    }

    template <typename TRequest>
    zlink::framework::task_t<void>
    send_spot (zlink::routing_id_t target,
               const std::string &spot_rid,
               const TRequest &request,
               const std::string &packet)
    {
        co_await _routes
          .send (yd::control_channel, target,
                 zlink::framework::spot_rid_t::from_string (spot_rid), request)
          .packet_name (packet)
          .async ();
    }

    template <typename TReply, typename TRequest>
    zlink::framework::task_t<TReply>
    request_spot (zlink::routing_id_t target,
                  const std::string &spot_rid,
                  const TRequest &request,
                  const std::string &packet)
    {
        auto reply =
          co_await _routes
            .request (yd::control_channel, target,
                      zlink::framework::spot_rid_t::from_string (spot_rid), request)
            .packet_name (packet)
            .timeout (std::chrono::milliseconds (10000))
            .template async<TReply> ();
        co_return reply;
    }

    static zlink::routing_id_t
    target_or_default (const zlink::framework::stream_dispatch_context_t &dispatch)
    {
        if (auto target = dispatch.metadata ().find (yd::target_node_rid_metadata)) {
            return zlink::routing_id_t::from (std::string (*target));
        }
        return zlink::routing_id_t::from ("play-a");
    }

    static std::string spot_rid (const zlink::framework::stream_dispatch_context_t &dispatch)
    {
        if (auto value = dispatch.metadata ().find (yd::spot_rid_metadata)) {
            return std::string (*value);
        }
        throw zlink::framework::framework_exception_t (
          zlink::framework::framework_error_kind_t::request_protocol_error,
          "spot-rid metadata is required");
    }

    zlink::framework::route_client_t &_routes;
    zlink::framework::session_actor_manager_t &_actors;
    zlink::framework::actor_gateway_t &_gateway;
    std::map<std::string, std::string> _bound_actors;
};

} // namespace zlink::framework::e2e::yield_dispatch::server::session
