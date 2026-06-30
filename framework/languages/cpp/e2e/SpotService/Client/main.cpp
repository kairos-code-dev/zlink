/* SPDX-License-Identifier: MPL-2.0 */

#include "../Shared/spot_service_contracts.hpp"
#include "Scenarios/sm_a1_scenario.hpp"
#include "Scenarios/sm_a2_scenario.hpp"
#include "Scenarios/sm_a3_scenario.hpp"
#include "Scenarios/sm_a4_scenario.hpp"
#include "Scenarios/sm_a6_scenario.hpp"
#include "Scenarios/sm_a7_scenario.hpp"
#include "Scenarios/sm_a8_scenario.hpp"
#include "Scenarios/sm_b1_scenario.hpp"
#include "Scenarios/sm_b2_scenario.hpp"
#include "Scenarios/sm_b3_scenario.hpp"
#include "Scenarios/sm_b4_scenario.hpp"
#include "Scenarios/sm_b5_scenario.hpp"
#include "Scenarios/sm_b6_scenario.hpp"
#include "Scenarios/sm_b7_scenario.hpp"
#include "Scenarios/sm_b8_scenario.hpp"
#include "Scenarios/sm_c1_scenario.hpp"
#include "Scenarios/sm_c2_scenario.hpp"
#include "Scenarios/sm_c3_scenario.hpp"
#include "Scenarios/sm_c4_scenario.hpp"
#include "Scenarios/sm_d1_scenario.hpp"
#include "Scenarios/sm_d2_scenario.hpp"
#include "Scenarios/sm_d3_scenario.hpp"
#include "Scenarios/sm_d4_scenario.hpp"
#include "Scenarios/sm_d5_scenario.hpp"
#include "Scenarios/sm_d6_scenario.hpp"
#include "Scenarios/sm_d7_scenario.hpp"
#include "Scenarios/sm_d8_scenario.hpp"
#include "Scenarios/sm_d9_scenario.hpp"
#include "Scenarios/sm_d10_scenario.hpp"
#include "Scenarios/sm_d11_scenario.hpp"
#include "Scenarios/sm_d12_scenario.hpp"
#include "Scenarios/sm_d13_scenario.hpp"
#include "Scenarios/sm_d14_scenario.hpp"
#include "Scenarios/sm_e1_scenario.hpp"
#include "Scenarios/sm_e2_scenario.hpp"
#include "Scenarios/sm_e3_scenario.hpp"
#include "Scenarios/sm_e4_scenario.hpp"
#include "Scenarios/sm_f1_scenario.hpp"
#include "Scenarios/sm_f2_scenario.hpp"
#include "Scenarios/sm_f4_scenario.hpp"
#include "Scenarios/sm_g1_scenario.hpp"
#include "Scenarios/sm_g2_scenario.hpp"
#include "Scenarios/sm_g3_scenario.hpp"
#include "Scenarios/sm_g4_scenario.hpp"

#include "runtime/actors/actor_gateway_runtime.hpp"

#include <zlink/framework.hpp>
#include <zlink/stream_connector.hpp>
#include <zlink/stream_e2e_client.hpp>
#include <zlink/stream_e2e_client/codecs/auto_codec.hpp>

#include <chrono>
#include <cstdlib>
#include <future>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
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

void ensure (bool condition, const std::string &message)
{
    if (!condition) {
        throw std::runtime_error (message);
    }
}

zlink::framework::actor_ref_t to_actor_ref (const e2e::actor_ref_dto_t &actor)
{
    return zlink::framework::actor_ref_t (
      zlink::framework::node_rid_t::from_string (actor.node_rid), actor.actor_type, actor.actor_id,
      actor.generation);
}

template <typename T> zlink::message_t encode_json (const T &value)
{
    return zlink::message_t::from (nlohmann::json (value).dump ());
}

zlink::framework::detail::stream_header_t request_header (std::string packet_name)
{
    return zlink::framework::detail::stream_header_t (
      zlink::framework::detail::stream_message_kind_t::request, zlink::framework::stream_codec_t::json,
      zlink::framework::detail::stream_header_flags_t::none, std::nullopt, std::move (packet_name));
}

template <typename TResult> std::string stream_error_text (const TResult &result)
{
    if (result.error ()) {
        return result.error ()->message;
    }
    return "unknown stream error";
}

class client_channel_state_t
{
  public:
    void record (std::string marker, std::string value)
    {
        std::lock_guard lock (_mutex);
        entries.push_back ({std::move (marker), std::move (value)});
    }

    bool has (const std::string &marker, const std::string &value) const
    {
        std::lock_guard lock (_mutex);
        for (const auto &entry : entries) {
            if (entry.first == marker && entry.second == value) {
                return true;
            }
        }
        return false;
    }

  private:
    mutable std::mutex _mutex;
    std::vector<std::pair<std::string, std::string>> entries;
};

class channel_echo_handler_t
{
  public:
    using dependency_types = zlink::framework::dependency_list_t<client_channel_state_t>;
    using request_type = e2e::channel_echo_req_t;
    using reply_type = e2e::channel_echo_res_t;

    explicit channel_echo_handler_t (client_channel_state_t &state) : _state (state) {}

    e2e::channel_echo_res_t handle (const e2e::channel_echo_req_t &request)
    {
        _state.record ("ChannelEcho", request.value);
        return {.value = "channel:" + request.value, .handled_by = "client-api"};
    }

  private:
    client_channel_state_t &_state;
};

class channel_command_handler_t
{
  public:
    using dependency_types = zlink::framework::dependency_list_t<client_channel_state_t>;
    using message_type = e2e::channel_command_t;

    explicit channel_command_handler_t (client_channel_state_t &state) : _state (state) {}

    void handle (const e2e::channel_command_t &command)
    {
        _state.record ("ChannelCommand", command.command_id);
    }

  private:
    client_channel_state_t &_state;
};

class scenario_service_t final : public zlink::framework::hosted_service_t
{
  public:
    scenario_service_t (zlink::framework::app_t &app,
                        std::string stream_endpoint,
                        std::string alternate_stream_endpoint,
                        std::string play_http_endpoint,
                        std::string scenario_mode,
                        std::string crash_ready_file,
                        std::string crash_go_file,
                        std::string crash_observed_file) :
        _app (app),
        _stream_endpoint (std::move (stream_endpoint)),
        _alternate_stream_endpoint (std::move (alternate_stream_endpoint)),
        _play_http_endpoint (std::move (play_http_endpoint)),
        _scenario_mode (std::move (scenario_mode)),
        _crash_ready_file (std::move (crash_ready_file)),
        _crash_go_file (std::move (crash_go_file)),
        _crash_observed_file (std::move (crash_observed_file))
    {
    }

    void start (zlink::framework::service_provider_t &services) override
    {
        try {
            auto scope = services.create_scope ();
            auto &routes = scope.get_required<zlink::framework::route_client_t> ();
            auto &channels = scope.get_required<zlink::framework::channel_client_t> ();
            auto &actors = scope.get_required<zlink::framework::session_actor_manager_t> ();
            auto &channel_state = scope.get_required<client_channel_state_t> ();
            auto &publisher = scope.get_required<zlink::framework::spot_publisher_client_t> ();
            run (routes, channels, actors, channel_state, publisher);
            passed = true;
        }
        catch (const std::exception &error) {
            std::cerr << "spot-service scenario failed: " << error.what () << std::endl;
        }
        _app.stop ();
        std::cout << std::flush;
        std::cerr << std::flush;
        std::_Exit (passed ? EXIT_SUCCESS : EXIT_FAILURE);
    }

    void stop () noexcept override {}

    bool passed = false;

  private:
    zlink::framework::session_actor_t bind_actor (zlink::framework::route_client_t &routes,
                                                  zlink::framework::session_actor_manager_t &actors,
                                                  const std::string &target_node,
                                                  const std::string &actor_id,
                                                  const std::string &display_name)
    {
        zlink::framework::result_t<e2e::ensure_actor_res_t> ensured =
          zlink::framework::result_t<e2e::ensure_actor_res_t>::failure (
            zlink::framework::framework_error_kind_t::timeout, "EnsureActor not attempted");
        for (int attempt = 0; attempt < 20; ++attempt) {
            ensured = routes
                        .request (e2e::route_channel, zlink::routing_id_t::from (target_node),
                                  e2e::ensure_actor_req_t{actor_id, display_name})
                        .packet_name ("EnsureActor")
                        .timeout (std::chrono::milliseconds (3000))
                        .async<e2e::ensure_actor_res_t> ()
                        .result ();
            if (ensured.has_value ()) {
                break;
            }
            std::this_thread::sleep_for (std::chrono::milliseconds (100));
        }
        ensure (ensured.has_value (),
                "EnsureActor failed for " + actor_id + ": "
                  + (ensured.error () ? ensured.error ()->what () : "unknown"));
        auto bound = actors.bind (to_actor_ref (ensured.value ().actor)).async ().result ();
        ensure (bound.has_value (), "bind actor failed for " + actor_id);
        return bound.value ();
    }

    template <typename TReply, typename TRequest>
    TReply relay_request (zlink::framework::session_actor_t &actor,
                          const std::string &packet_name,
                          const TRequest &request)
    {
        auto header = request_header (packet_name);
        zlink::framework::detail::enter_stream_relay_dispatch (header);
        auto reply =
          actor.relay_request (zlink::message_t::from_json (request)).async ().result ();
        zlink::framework::detail::exit_stream_relay_dispatch ();
        ensure (reply.has_value (), packet_name + " relay failed: "
                                      + (reply.error () ? reply.error ()->what () : "unknown"));
        return reply.value ().template parse_json<TReply> ();
    }

    template <typename TReply, typename TRequest>
    TReply relay_request_with_retry (zlink::framework::session_actor_t &actor,
                                     const std::string &packet_name,
                                     const TRequest &request)
    {
        const auto deadline = std::chrono::steady_clock::now () + std::chrono::seconds (10);
        std::string last_error = "request was not attempted";
        while (std::chrono::steady_clock::now () < deadline) {
            try {
                return relay_request<TReply> (actor, packet_name, request);
            }
            catch (const std::exception &ex) {
                last_error = ex.what ();
                std::this_thread::sleep_for (std::chrono::milliseconds (100));
            }
        }
        throw std::runtime_error (packet_name + " relay failed after retry: " + last_error);
    }

    void run (zlink::framework::route_client_t &routes,
              zlink::framework::channel_client_t &channels,
              zlink::framework::session_actor_manager_t &actors,
              client_channel_state_t &channel_state,
              zlink::framework::spot_publisher_client_t &publisher)
    {
        if (_scenario_mode == "route-ready") {
            (void) ensure_actor_ref (routes, "play-a", "route-ready-play-a",
                                     "Route Ready Play A");
            (void) ensure_actor_ref (routes, "play-b", "route-ready-play-b",
                                     "Route Ready Play B");
            std::cout << "scenario route-ready passed\n";
            return;
        }
        if (_scenario_mode == "route-ready-play-b") {
            (void) ensure_actor_ref (routes, "play-b", "route-ready-play-b",
                                     "Route Ready Play B");
            std::cout << "scenario route-ready-play-b passed\n";
            return;
        }
        if (_scenario_mode == "stream" || _scenario_mode == "stream-rest") {
            run_stream_session_scenario (routes, "SM-D1", "play-a", "stream-local", "a-stream-room",
                                         "stream-local-push");
            run_stream_session_scenario (routes, "SM-D2", "play-b", "stream-remote",
                                         "b-stream-room", "stream-remote-push");
            run_multi_stream_session_scenario (routes);
            run_stream_and_channel_mixed_scenario (routes, channels);
            run_stream_reconnect_migration_scenario (routes);
            run_stream_auth_dispatch_scenario (routes);
            run_stream_disconnect_notification_scenario (routes);
            if (_scenario_mode == "stream") {
                zlink::framework::e2e::spot_service::client::scenarios::run_sm_d6_scenario (
                  _stream_endpoint, _alternate_stream_endpoint, _play_http_endpoint);
                std::cout << "scenario SM-D6 passed\n";
            }
            return;
        }
        if (_scenario_mode == "crash-setup") {
            zlink::framework::e2e::spot_service::client::scenarios::
              run_sm_g1_crash_observation_scenario (
                routes, _stream_endpoint, _crash_ready_file, _crash_go_file,
                _crash_observed_file);
            return;
        }
        if (_scenario_mode == "crash-recover") {
            zlink::framework::e2e::spot_service::client::scenarios::
              run_sm_g1_crash_recovery_scenario (routes, _stream_endpoint);
            return;
        }
        auto refresh_actor = [&actors] (const std::string &actor_id) {
            auto refreshed = actors.find (actor_id);
            ensure (refreshed.has_value (), "actor was not found after join: " + actor_id);
            return refreshed.value ();
        };

        auto local = bind_actor (routes, actors, "play-a", "alice", "Alice");
        auto local_join =
          relay_request<e2e::join_res_t> (local, "JoinReq",
                                          e2e::join_req_t{.key = "a-room",
                                                          .actor_id = "alice",
                                                          .display_name = "Alice",
                                                          .level = 7,
                                                          .tags = {"alpha", "local"}});
        ensure (local_join.owner_node_rid == "play-a",
                "SM-A1/SM-B1 owner mismatch: owner=" + local_join.owner_node_rid
                  + " spot=" + local_join.spot_rid);
        ensure (local_join.spot_rid == "user:play-a:a-room",
                "SM-A1 spot rid mismatch: owner=" + local_join.owner_node_rid
                  + " spot=" + local_join.spot_rid);
        ensure (local_join.actor_id == "alice" && local_join.display_name == "Alice"
                  && local_join.level == 7 && local_join.tags.size () == 2
                  && local_join.tags[0] == "alpha" && local_join.tags[1] == "local",
                "SM-A1/SM-B1 join payload fidelity mismatch");
        std::cout << "scenario SM-A1 passed\n";
        if (_scenario_mode == "sm-a1") {
            return;
        }
        std::cout << "scenario SM-B1 passed\n";
        local = refresh_actor ("alice");

        auto state1 =
          relay_request<e2e::state_res_t> (local, "StateReq", e2e::state_req_t{"add", 3});
        auto state2 =
          relay_request<e2e::state_res_t> (local, "StateReq", e2e::state_req_t{"add", 4});
        ensure (state1.value == 3 && state1.sequence == 1, "SM-A2 first mutation mismatch");
        ensure (state2.value == 7 && state2.sequence == 2, "SM-A2 second mutation mismatch");
        std::cout << "scenario SM-A2 passed\n";

        auto mismatch = relay_request<e2e::type_mismatch_res_t> (
          local, "TypeMismatchReq", e2e::type_mismatch_req_t{"same-rid"});
        ensure (mismatch.rejected && mismatch.error_kind == "spot_type_mismatch",
                "SM-A7 did not reject spot type mismatch");
        ensure (mismatch.spot_name == e2e::user_spot && mismatch.value == 7,
                "SM-A7 original spot changed after type mismatch");
        std::cout << "scenario SM-A7 passed\n";

        auto lifecycle =
          routes
            .request (e2e::route_channel, zlink::routing_id_t::from (std::string ("play-a")),
                      e2e::lifecycle_req_t{"lifecycle-room"})
            .packet_name ("LifecycleReq")
            .timeout (std::chrono::milliseconds (3000))
            .async<e2e::lifecycle_res_t> ()
            .result ();
        ensure (lifecycle.has_value (), "SM-A6 lifecycle request failed");
        ensure (lifecycle.value ().spot_rid == "user:play-a:lifecycle-room"
                  && lifecycle.value ().created && lifecycle.value ().closed,
                "SM-A6 lifecycle reply mismatch");
        std::cout << "scenario SM-A6 passed\n";

        auto missing_actor_header = request_header ("MissingActorPacket");
        zlink::framework::detail::enter_stream_relay_dispatch (missing_actor_header);
        auto missing_actor_packet =
          local
            .relay_request (
              zlink::message_t::from_json (e2e::state_req_t{"add", 1}))
            .async ()
            .result ();
        zlink::framework::detail::exit_stream_relay_dispatch ();
        ensure (!missing_actor_packet.has_value (),
                "SM-B5 missing actor packet unexpectedly succeeded");
        std::cout << "scenario SM-B5 passed\n";

        auto same_key_actor = bind_actor (routes, actors, "play-a", "alice-2", "Alice Two");
        auto same_key_join =
          relay_request<e2e::join_res_t> (same_key_actor, "JoinReq",
                                          e2e::join_req_t{.key = "a-room",
                                                          .actor_id = "alice-2",
                                                          .display_name = "Alice Two",
                                                          .level = 2,
                                                          .tags = {"same-key"}});
        ensure (same_key_join.owner_node_rid == "play-a"
                  && same_key_join.spot_rid == local_join.spot_rid,
                "SM-A4 same key did not keep the same owner");
        same_key_actor = refresh_actor ("alice-2");

        auto remote = bind_actor (routes, actors, "play-b", "bob", "Bob");
        const auto remote_join_request =
          e2e::join_req_t{.key = "b-room",
                          .actor_id = "bob",
                          .display_name = "Bob",
                          .level = 9,
                          .tags = {"beta", "remote"}};
        auto remote_spot_ready =
          routes
            .request (e2e::route_channel, zlink::routing_id_t::from (std::string ("play-b")),
                      remote_join_request)
            .packet_name ("EnsureUserSpot")
            .timeout (std::chrono::milliseconds (3000))
            .async<e2e::join_res_t> ()
            .result ();
        ensure (remote_spot_ready.has_value (),
                std::string ("SM-B2 remote user SPOT ensure failed: ")
                  + (remote_spot_ready.error () ? remote_spot_ready.error ()->what () : "unknown"));
        auto remote_join_result =
          remote.context ()
            .join_spot (
              zlink::framework::spot_rid_t::from_string (remote_spot_ready.value ().spot_rid),
              remote_join_request)
            .async<e2e::join_res_t> ()
            .result ();
        ensure (remote_join_result.has_value (),
                std::string ("SM-B2 remote user SPOT join failed: ")
                  + (remote_join_result.error () ? remote_join_result.error ()->what () : "unknown"));
        auto remote_join = remote_join_result.value ().reply;
        ensure (remote_join.owner_node_rid == "play-b", "SM-B2 owner mismatch");
        ensure (remote_join.spot_rid == "user:play-b:b-room", "SM-A4 remote spot rid mismatch");
        remote = refresh_actor ("bob");
        auto remote_state =
          relay_request<e2e::state_res_t> (remote, "StateReq", e2e::state_req_t{"add", 11});
        ensure (remote_state.owner_node_rid == "play-b" && remote_state.value == 11,
                "SM-B4 remote actor request mismatch");
        std::cout << "scenario SM-A3 passed\n";
        std::cout << "scenario SM-A4 passed\n";
        std::cout << "scenario SM-B2 passed\n";
        const auto remote_spot = zlink::framework::spot_rid_t::from_string (remote_join.spot_rid);
        if (_scenario_mode == "sm-f1") {
            zlink::framework::e2e::spot_service::client::scenarios::run_sm_f1_scenario (
              routes, remote_spot);
            return;
        }
        if (_scenario_mode == "sm-f2") {
            zlink::framework::e2e::spot_service::client::scenarios::run_sm_f1_scenario (
              routes, remote_spot);
            zlink::framework::e2e::spot_service::client::scenarios::run_sm_f2_scenario (
              routes, remote_spot);
            return;
        }
        if (_scenario_mode == "sm-f4") {
            zlink::framework::e2e::spot_service::client::scenarios::run_sm_f4_scenario (
              routes, remote_spot);
            return;
        }

        auto worker_future = std::async (std::launch::async, [&] {
            return relay_request<e2e::worker_res_t> (same_key_actor, "WorkerReq",
                                                     e2e::worker_req_t{13, 600});
        });
        std::this_thread::sleep_for (std::chrono::milliseconds (200));
        const auto interleaved_started = std::chrono::steady_clock::now ();
        auto interleaved_state =
          relay_request<e2e::state_res_t> (local, "StateReq", e2e::state_req_t{"add", 5});
        const auto interleaved_elapsed = std::chrono::steady_clock::now () - interleaved_started;
        ensure (interleaved_elapsed < std::chrono::milliseconds (350),
                "SM-A8 same-spot request was blocked by worker");
        ensure (interleaved_state.value == 12 && interleaved_state.sequence == 3,
                "SM-A8 interleaved state mismatch");
        auto worker = worker_future.get ();
        const auto worker_mismatch =
          std::string ("SM-A8 worker result mismatch: snapshot=") + std::to_string (worker.snapshot)
          + " worker_result=" + std::to_string (worker.worker_result) + " final_value="
          + std::to_string (worker.final_value) + " sequence=" + std::to_string (worker.sequence);
        ensure (worker.snapshot == 7 && worker.worker_result == 20 && worker.final_value == 25
                  && worker.sequence == 4,
                worker_mismatch);
        std::cout << "scenario SM-A8 passed\n";

        auto left =
          relay_request<e2e::leave_res_t> (local, "LeaveReq", e2e::leave_req_t{"client-left"});
        ensure (left.left && left.actor_id == "alice", "SM-B6 leave reply mismatch");
        std::cout << "scenario SM-B6 passed\n";
        local = refresh_actor ("alice");

        auto destroyed = relay_request<e2e::destroy_actor_res_t> (
          local, "DestroyActorReq", e2e::destroy_actor_req_t{"client-destroyed"});
        ensure (destroyed.destroyed && destroyed.actor_id == "alice",
                "SM-B8 destroy reply mismatch");
        auto after_destroy_header = request_header ("StateReq");
        zlink::framework::detail::enter_stream_relay_dispatch (after_destroy_header);
        auto after_destroy =
          local
            .relay_request (
              zlink::message_t::from_json (e2e::state_req_t{"add", 1}))
            .async ()
            .result ();
        zlink::framework::detail::exit_stream_relay_dispatch ();
        ensure (!after_destroy.has_value (), "SM-B8 destroyed actor unexpectedly accepted request");
        std::cout << "scenario SM-B8 passed\n";

        auto outbound = relay_request<e2e::outbound_res_t> (same_key_actor, "OutboundReq",
                                                            e2e::outbound_req_t{"from-spot"});
        ensure (outbound.channel_reply == "channel:from-spot", "SM-C2 channel reply mismatch");
        ensure (outbound.command_sent && outbound.published, "SM-C2 outbound flags mismatch");
        ensure (channel_state.has ("ChannelEcho", "from-spot"),
                "SM-C2 channel request evidence missing");
        ensure (channel_state.has ("ChannelCommand", "cmd-alice-2-from-spot"),
                "SM-C2 channel send evidence missing");
        std::cout << "scenario SM-C2 passed\n";

        auto spot_to_spot = relay_request_with_retry<e2e::spot_to_spot_res_t> (
          same_key_actor, "SpotToSpotReq", e2e::spot_to_spot_req_t{"b-room", "spot-to-spot"});
        ensure (spot_to_spot.request_reply == "spot-to-spot:reply",
                "SM-C3 spot request reply mismatch");
        ensure (spot_to_spot.command_sent && spot_to_spot.published && spot_to_spot.missing_rejected
                  && spot_to_spot.timeout_rejected,
                "SM-C3 flags mismatch");
        std::cout << "scenario SM-C3 passed\n";

        zlink::framework::e2e::spot_service::client::scenarios::run_sm_f1_scenario (
          routes, remote_spot);
        if (_scenario_mode == "sm-f1") {
            return;
        }
        zlink::framework::e2e::spot_service::client::scenarios::run_sm_f2_scenario (
          routes, remote_spot);
        if (_scenario_mode == "sm-f2") {
            return;
        }
        std::cout << "scenario SM-C1 passed\n";

        auto normal_route_after_spot =
          routes
            .request (e2e::route_channel, zlink::routing_id_t::from (std::string ("play-b")),
                      e2e::ensure_actor_req_t{"route-mixed-f3", "Route Mixed"})
            .packet_name ("EnsureActor")
            .timeout (std::chrono::milliseconds (3000))
            .async<e2e::ensure_actor_res_t> ()
            .result ();
        ensure (normal_route_after_spot.has_value (),
                "SM-F3 normal route packet failed after spot route");
        auto spot_route_after_normal =
          routes
            .request (e2e::route_channel, zlink::routing_id_t::from (std::string ("play-b")),
                      remote_spot, e2e::direct_spot_req_t{"external-client", "route-mixed"})
            .packet_name ("DirectSpotReq")
            .timeout (std::chrono::milliseconds (3000))
            .async<e2e::direct_spot_res_t> ()
            .result ();
        ensure (spot_route_after_normal.has_value ()
                  && spot_route_after_normal.value ().value == "route-mixed:reply",
                "SM-F3 spot route packet failed after normal route");
        std::cout << "scenario SM-F3 passed\n";

        auto missing_spot_handler =
          routes
            .request (e2e::route_channel, zlink::routing_id_t::from (std::string ("play-b")),
                      remote_spot, e2e::unhandled_spot_req_t{"missing"})
            .packet_name ("MissingSpotReq")
            .timeout (std::chrono::milliseconds (1000))
            .async<e2e::direct_spot_res_t> ()
            .result ();
        ensure (!missing_spot_handler.has_value (),
                "SM-E1 missing spot route handler unexpectedly succeeded");
        auto slow_spot_timeout =
          routes
            .request (e2e::route_channel, zlink::routing_id_t::from (std::string ("play-b")),
                      remote_spot, e2e::slow_spot_req_t{"external-timeout"})
            .packet_name ("SlowSpotReq")
            .timeout (std::chrono::milliseconds (50))
            .async<e2e::direct_spot_res_t> ()
            .result ();
        ensure (!slow_spot_timeout.has_value (),
                "SM-C1 slow spot route request unexpectedly succeeded");
        zlink::framework::e2e::spot_service::client::scenarios::run_sm_f4_scenario (
          routes, remote_spot);
        if (_scenario_mode == "sm-f4") {
            return;
        }
        std::cout << "scenario SM-E1 passed\n";
        std::cout << "scenario SM-F5 passed\n";

        auto publish_only = publisher
                              .publish (e2e::publisher_channel, e2e::mesh_topic,
                                        e2e::mesh_event_t{"evt-publisher-client", "publish-only"})
                              .result ();
        ensure (publish_only.has_value (), "SM-C4 publisher client publish failed");
        std::this_thread::sleep_for (std::chrono::milliseconds (100));
        auto publish_retry = publisher
                               .publish (e2e::publisher_channel, e2e::mesh_topic,
                                         e2e::mesh_event_t{"evt-publisher-client", "publish-only"})
                               .result ();
        ensure (publish_retry.has_value (), "SM-C4 publisher client retry publish failed");
        std::cout << "scenario SM-C4 passed\n";

        if (_scenario_mode == "combined") {
            run_stream_session_scenario (routes, "SM-D1", "play-a", "stream-local", "a-stream-room",
                                         "stream-local-push");
            run_stream_session_scenario (routes, "SM-D2", "play-b", "stream-remote",
                                         "b-stream-room", "stream-remote-push");
            run_multi_stream_session_scenario (routes);
            run_stream_and_channel_mixed_scenario (routes, channels);
            run_stream_reconnect_migration_scenario (routes);
            run_stream_auth_dispatch_scenario (routes);
            run_stream_disconnect_notification_scenario (routes);
            zlink::framework::e2e::spot_service::client::scenarios::run_sm_d6_scenario (
              _stream_endpoint, _alternate_stream_endpoint, _play_http_endpoint);
            std::cout << "scenario SM-D6 passed\n";
            return;
        }

        if (_scenario_mode != "base") {
            run_stream_session_scenario (routes, "SM-D1", "play-a", "stream-local", "a-stream-room",
                                         "stream-local-push");
            run_stream_session_scenario (routes, "SM-D2", "play-b", "stream-remote",
                                         "b-stream-room", "stream-remote-push");
        }
    }

    zlink::stream_connector::connector_t make_stream_connector (std::string endpoint = {}) const
    {
        zlink::stream_connector::connector_options_t options;
        options.endpoint = endpoint.empty () ? _stream_endpoint : std::move (endpoint);
        options.connect_timeout = std::chrono::milliseconds (5000);
        options.request_timeout = std::chrono::milliseconds (5000);
        options.dispatch_mode = zlink::stream_connector::dispatch_mode_t::immediate;
        return zlink::stream_connector::connector_factory_t::create (options);
    }

    e2e::actor_ref_dto_t ensure_actor_ref (zlink::framework::route_client_t &routes,
                                           const std::string &target_node,
                                           const std::string &actor_id,
                                           const std::string &display_name)
    {
        zlink::framework::result_t<e2e::ensure_actor_res_t> ensured =
          zlink::framework::result_t<e2e::ensure_actor_res_t>::failure (
            zlink::framework::framework_error_kind_t::timeout, "EnsureActor not attempted");
        for (int attempt = 0; attempt < 20; ++attempt) {
            ensured = routes
                        .request (e2e::route_channel, zlink::routing_id_t::from (target_node),
                                  e2e::ensure_actor_req_t{actor_id, display_name})
                        .packet_name ("EnsureActor")
                        .timeout (std::chrono::milliseconds (5000))
                        .async<e2e::ensure_actor_res_t> ()
                        .result ();
            if (ensured.has_value ()) {
                break;
            }
            std::this_thread::sleep_for (std::chrono::milliseconds (100));
        }
        ensure (ensured.has_value (),
                "stream EnsureActor failed for " + actor_id + ": "
                  + (ensured.error () ? ensured.error ()->what () : "unknown"));
        return ensured.value ().actor;
    }

    void run_stream_session_scenario (zlink::framework::route_client_t &routes,
                                      const std::string &scenario_id,
                                      const std::string &target_node,
                                      const std::string &actor_id,
                                      const std::string &key,
                                      const std::string &push_value)
    {
        const auto display_name = actor_id + "-display";
        auto actor = ensure_actor_ref (routes, target_node, actor_id, display_name);
        auto core = make_stream_connector ();
        core.codecs ().add_json ();
        auto stream = zlink::stream_e2e_client::use (core);

        auto connected = stream.connect ().submit ();
        ensure (static_cast<bool> (connected), scenario_id + " stream connect failed");

        auto auth = stream.send (e2e::stream_auth_req_t{target_node, actor_id, display_name, actor})
                      .packet_name ("StreamAuthReq")
                      .submit ();
        ensure (static_cast<bool> (auth),
                scenario_id + " stream auth failed: " + stream_error_text (auth));
        std::this_thread::sleep_for (std::chrono::milliseconds (200));

        auto joined = stream.send (e2e::join_req_t{.key = key,
                                                .actor_id = actor_id,
                                                .display_name = actor_id + "-display",
                                                .level = target_node == "play-a" ? 31 : 41,
                                                .tags = {"stream", scenario_id}})
                        .packet_name ("JoinReq")
                        .submit ();
        ensure (static_cast<bool> (joined),
                scenario_id + " stream join send failed: " + stream_error_text (joined));
        std::this_thread::sleep_for (std::chrono::milliseconds (200));

        auto state =
          stream.send (e2e::state_req_t{.op = "add", .amount = target_node == "play-a" ? 13 : 17})
            .packet_name ("StateReq")
            .submit ();
        ensure (static_cast<bool> (state),
                scenario_id + " stream state send failed: " + stream_error_text (state));

        auto push_wait =
          stream.wait_for<e2e::actor_push_notify_t> (std::chrono::milliseconds (10000))
            .to_future (scenario_id + " push notify missing");
        auto pushed =
          stream.send (e2e::actor_push_req_t{push_value})
            .packet_name ("PushReq")
            .submit ();
        ensure (static_cast<bool> (pushed),
                scenario_id + " push trigger failed: " + stream_error_text (pushed));
        (void) push_wait.get ();

        (void) stream.close ().submit ();
        std::cout << "scenario " << scenario_id << " passed\n";
    }

    void run_stream_auth_dispatch_scenario (zlink::framework::route_client_t &routes)
    {
        const auto actor_id = std::string ("stream-auth-d7");
        auto actor = ensure_actor_ref (routes, "play-a", actor_id, actor_id + "-display");

        {
            auto core = make_stream_connector ();
            core.codecs ().add_json ();
            auto unauthenticated = zlink::stream_e2e_client::use (core);
            auto connected = unauthenticated.connect ().submit ();
            ensure (static_cast<bool> (connected), "SM-D7 unauthenticated stream connect failed");

            auto before_auth = unauthenticated.request (e2e::state_req_t{.op = "add", .amount = 1})
                                 .packet_name ("StateReq")
                                 .timeout (std::chrono::milliseconds (3000))
                                 .async<e2e::state_res_t> ()
                                 .result ();
            ensure (!static_cast<bool> (before_auth),
                    "SM-D7 unauthenticated dispatch unexpectedly succeeded");
            (void) unauthenticated.close ().submit ();
        }

        {
            auto invalid_actor = actor;
            invalid_actor.actor_id.clear ();
            auto core = make_stream_connector ();
            core.codecs ().add_json ();
            auto invalid = zlink::stream_e2e_client::use (core);
            auto connected = invalid.connect ().submit ();
            ensure (static_cast<bool> (connected), "SM-D7 invalid-auth stream connect failed");

            auto rejected = invalid.request (e2e::stream_auth_req_t{"play-a", actor_id,
                                                              actor_id + "-display", invalid_actor})
                              .packet_name ("StreamAuthReq")
                              .timeout (std::chrono::milliseconds (3000))
                              .async<e2e::stream_auth_res_t> ()
                              .result ();
            ensure (!static_cast<bool> (rejected), "SM-D7 invalid auth unexpectedly succeeded");
            (void) invalid.close ().submit ();
        }

        auto core = make_stream_connector ();
        core.codecs ().add_json ();
        std::mutex observations_mutex;
        std::vector<zlink::stream_connector::inbound_observation_t> observations;
        auto observer = core.observe_inbound (
          [&] (const zlink::stream_connector::inbound_observation_t &observation) {
              std::lock_guard lock (observations_mutex);
              observations.push_back (observation);
          });
        auto stream = zlink::stream_e2e_client::use (core);
        auto connected = stream.connect ().submit ();
        ensure (static_cast<bool> (connected), "SM-D7 stream connect failed");

        auto auth =
          stream.request (e2e::stream_auth_req_t{"play-a", actor_id, actor_id + "-display", actor})
            .packet_name ("StreamAuthReq")
            .timeout (std::chrono::milliseconds (3000))
            .async<e2e::stream_auth_res_t> ()
            .result ();
        ensure (static_cast<bool> (auth), "SM-D7 stream auth failed: " + stream_error_text (auth));
        ensure (auth.value ().actor.actor_id == actor_id
                  && auth.value ().session_node_rid == "session-a",
                "SM-D7 stream auth reply mismatch");

        auto joined = stream.request (e2e::join_req_t{.key = "a-stream-auth",
                                                .actor_id = actor_id,
                                                .display_name = actor_id + "-display",
                                                .level = 71,
                                                .tags = {"stream", "SM-D7"}})
                        .packet_name ("JoinReq")
                        .timeout (std::chrono::milliseconds (5000))
                        .async<e2e::join_res_t> ()
                        .result ();
        ensure (static_cast<bool> (joined),
                "SM-D7 stream join dispatch failed: " + stream_error_text (joined));
        ensure (joined.value ().actor_id == actor_id && joined.value ().owner_node_rid == "play-a",
                "SM-D7 stream join dispatch reply mismatch");

        auto state = stream.request (e2e::state_req_t{.op = "add", .amount = 7})
                       .packet_name ("StateReq")
                       .timeout (std::chrono::milliseconds (5000))
                       .async<e2e::state_res_t> ()
                       .result ();
        ensure (static_cast<bool> (state),
                "SM-D7 stream state dispatch failed: " + stream_error_text (state));
        ensure (state.value ().owner_node_rid == "play-a" && state.value ().value == 7,
                "SM-D7 stream state dispatch reply mismatch");

        const auto observation_deadline =
          std::chrono::steady_clock::now () + std::chrono::seconds (2);
        for (;;) {
            {
                std::lock_guard lock (observations_mutex);
                if (observations.size () >= 3) {
                    break;
                }
            }
            if (std::chrono::steady_clock::now () >= observation_deadline) {
                break;
            }
            std::this_thread::sleep_for (std::chrono::milliseconds (10));
        }

        {
            std::lock_guard lock (observations_mutex);
            auto observation_summary = [&] {
                std::string summary;
                for (const auto &observation : observations) {
                    if (!summary.empty ()) {
                        summary += ",";
                    }
                    summary += observation.name + "#"
                               + std::to_string (
                                 static_cast<int> (observation.kind))
                               + "#"
                               + (observation.request_seq
                                    ? std::to_string (*observation.request_seq)
                                    : std::string ("none"));
                }
                return summary;
            };
            auto has_response = [&] (std::uint64_t request_seq) {
                for (const auto &observation : observations) {
                    if (observation.kind == zlink::stream_connector::message_kind_t::response
                        && observation.name == "reply"
                        && observation.request_seq == request_seq) {
                        return true;
                    }
                }
                return false;
            };
            ensure (has_response (1),
                    "SM-D9 auth response observation missing: " + observation_summary ());
            ensure (has_response (2),
                    "SM-D9 join response observation missing: " + observation_summary ());
            ensure (has_response (3),
                    "SM-D9 state response observation missing: " + observation_summary ());
        }

        (void) stream.close ().submit ();
        std::cout << "scenario SM-D7 passed\n";
        std::cout << "scenario SM-D9 passed\n";
    }

    void run_bound_session_push_targeting_scenario (zlink::framework::route_client_t &routes)
    {
        const auto actor_id = std::string ("stream-push-d6");
        auto actor = ensure_actor_ref (routes, "play-a", actor_id, actor_id + "-display");

        auto bound_core = make_stream_connector ();
        bound_core.codecs ().add_json ();
        auto bound = zlink::stream_e2e_client::use (bound_core);
        auto bound_connected = bound.connect ().submit ();
        ensure (static_cast<bool> (bound_connected), "SM-D6 bound stream connect failed");

        auto unbound_core = make_stream_connector ();
        unbound_core.codecs ().add_json ();
        auto unbound = zlink::stream_e2e_client::use (unbound_core);
        auto unbound_connected = unbound.connect ().submit ();
        ensure (static_cast<bool> (unbound_connected), "SM-D6 unbound stream connect failed");

        auto auth =
          bound.request (e2e::stream_auth_req_t{"play-a", actor_id, actor_id + "-display", actor})
            .packet_name ("StreamAuthReq")
            .timeout (std::chrono::milliseconds (3000))
            .async<e2e::stream_auth_res_t> ()
            .result ();
        ensure (static_cast<bool> (auth), "SM-D6 stream auth failed: " + stream_error_text (auth));

        auto joined = bound.request (e2e::join_req_t{.key = "a-stream-push",
                                               .actor_id = actor_id,
                                               .display_name = actor_id + "-display",
                                               .level = 81,
                                               .tags = {"stream", "SM-D6"}})
                        .packet_name ("JoinReq")
                        .timeout (std::chrono::milliseconds (5000))
                        .async<e2e::join_res_t> ()
                        .result ();
        ensure (static_cast<bool> (joined),
                "SM-D6 stream join dispatch failed: " + stream_error_text (joined));

        auto bound_wait =
          bound.wait_for<e2e::actor_push_notify_t> (std::chrono::milliseconds (10000))
            .to_future ("SM-D6 bound push notify missing");
        auto unbound_wait =
          unbound.wait_for<e2e::actor_push_notify_t> (std::chrono::milliseconds (500)).async ();
        auto pushed = bound.request (e2e::actor_push_req_t{"stream-push-d6-value"})
                        .packet_name ("PushReq")
                        .timeout (std::chrono::milliseconds (5000))
                        .async<e2e::actor_push_res_t> ()
                        .result ();
        ensure (static_cast<bool> (pushed),
                "SM-D6 push trigger failed: " + stream_error_text (pushed));

        auto notify = bound_wait.get ();
        ensure (notify.actor_id == actor_id && notify.value == "stream-push-d6-value",
                "SM-D6 bound push notify mismatch");

        auto unbound_notify = unbound_wait.result ();
        ensure (!static_cast<bool> (unbound_notify), "SM-D6 unbound stream received push");

        (void) bound.close ().submit ();
        (void) unbound.close ().submit ();
        std::cout << "scenario SM-D6 passed\n";
    }

    void run_stream_and_channel_mixed_scenario (zlink::framework::route_client_t &routes,
                                                zlink::framework::channel_client_t &channels)
    {
        const auto actor_id = std::string ("stream-channel-mixed-d11");
        auto actor = ensure_actor_ref (routes, "play-a", actor_id, actor_id + "-display");

        auto core = make_stream_connector ();
        core.codecs ().add_json ();
        auto stream = zlink::stream_e2e_client::use (core);
        auto connected = stream.connect ().submit ();
        ensure (static_cast<bool> (connected), "SM-D11 stream connect failed");

        auto auth =
          stream.request (e2e::stream_auth_req_t{"play-a", actor_id, actor_id + "-display", actor})
            .packet_name ("StreamAuthReq")
            .timeout (std::chrono::milliseconds (3000))
            .async<e2e::stream_auth_res_t> ()
            .result ();
        ensure (static_cast<bool> (auth), "SM-D11 stream auth failed: " + stream_error_text (auth));

        auto joined = stream.request (e2e::join_req_t{.key = "a-stream-channel-mixed",
                                                .actor_id = actor_id,
                                                .display_name = actor_id + "-display",
                                                .level = 91,
                                                .tags = {"stream", "SM-D11"}})
                        .packet_name ("JoinReq")
                        .timeout (std::chrono::milliseconds (5000))
                        .async<e2e::join_res_t> ()
                        .result ();
        ensure (static_cast<bool> (joined),
                "SM-D11 stream join failed: " + stream_error_text (joined));

        auto stream_request = std::async (std::launch::async, [&] {
            return stream.request (e2e::state_req_t{.op = "add", .amount = 11})
              .packet_name ("StateReq")
              .timeout (std::chrono::milliseconds (5000))
              .async<e2e::state_res_t> ()
              .result ();
        });
        auto channel_request = std::async (std::launch::async, [&] {
            return channels
              .request (e2e::api_channel, e2e::channel_echo_req_t{"sm-d11-channel"})
              .timeout (std::chrono::milliseconds (5000))
              .async<e2e::channel_echo_res_t> ()
              .result ();
        });

        const auto stream_reply = stream_request.get ();
        const auto channel_reply = channel_request.get ();
        ensure (static_cast<bool> (stream_reply),
                "SM-D11 stream request failed: " + stream_error_text (stream_reply));
        ensure (stream_reply.value ().value == 11, "SM-D11 stream reply mismatch");
        ensure (channel_reply.has_value (), "SM-D11 channel request failed");
        ensure (channel_reply.value ().value == "channel:sm-d11-channel",
                "SM-D11 channel reply mismatch");

        (void) stream.close ().submit ();
        std::cout << "scenario SM-D11 passed\n";
    }

    void run_stream_disconnect_notification_scenario (zlink::framework::route_client_t &routes)
    {
        const auto notified_actor_id = std::string ("stream-disconnect-d5-notified");
        const auto muted_actor_id = std::string ("stream-disconnect-d5-muted");
        auto notified_actor =
          ensure_actor_ref (routes, "play-a", notified_actor_id, notified_actor_id + "-display");
        auto muted_actor =
          ensure_actor_ref (routes, "play-a", muted_actor_id, muted_actor_id + "-display");

        auto core = make_stream_connector ();
        core.codecs ().add_json ();
        auto stream = zlink::stream_e2e_client::use (core);

        auto connected = stream.connect ().submit ();
        ensure (static_cast<bool> (connected), "SM-D5 stream connect failed");

        auto notified_auth =
          stream.request (e2e::stream_auth_req_t{"play-a", notified_actor_id,
                                           notified_actor_id + "-display", notified_actor})
            .packet_name ("StreamAuthReq")
            .timeout (std::chrono::milliseconds (3000))
            .async<e2e::stream_auth_res_t> ()
            .result ();
        ensure (static_cast<bool> (notified_auth),
                "SM-D5 notified actor auth failed: " + stream_error_text (notified_auth));

        auto muted_auth =
          stream.request (e2e::stream_auth_req_t{"play-a", muted_actor_id, muted_actor_id + "-display",
                                           muted_actor})
            .packet_name ("StreamAuthReq")
            .timeout (std::chrono::milliseconds (3000))
            .async<e2e::stream_auth_res_t> ()
            .result ();
        ensure (static_cast<bool> (muted_auth),
                "SM-D5 muted actor auth failed: " + stream_error_text (muted_auth));

        auto notified_join =
          stream.request (e2e::join_req_t{.key = "a-stream-disconnect-notified",
                                    .actor_id = notified_actor_id,
                                    .display_name = notified_actor_id + "-display",
                                    .level = 91,
                                    .tags = {"stream", "SM-D5", "notified"}})
            .packet_name ("JoinReq")
            .metadata ("actor-id", notified_actor_id)
            .timeout (std::chrono::milliseconds (5000))
            .async<e2e::join_res_t> ()
            .result ();
        ensure (static_cast<bool> (notified_join),
                "SM-D5 notified actor join failed: " + stream_error_text (notified_join));

        auto muted_join = stream.request (e2e::join_req_t{.key = "a-stream-disconnect-muted",
                                                    .actor_id = muted_actor_id,
                                                    .display_name = muted_actor_id + "-display",
                                                    .level = 92,
                                                    .tags = {"stream", "SM-D5", "muted"}})
                            .packet_name ("JoinReq")
                            .metadata ("actor-id", muted_actor_id)
                            .timeout (std::chrono::milliseconds (5000))
                            .async<e2e::join_res_t> ()
                            .result ();
        ensure (static_cast<bool> (muted_join),
                "SM-D5 muted actor join failed: " + stream_error_text (muted_join));

        (void) stream.close ().submit ();
        std::this_thread::sleep_for (std::chrono::milliseconds (1000));
        std::cout << "scenario SM-D5 passed\n";
    }

    void run_stream_reconnect_migration_scenario (zlink::framework::route_client_t &routes)
    {
        ensure (!_alternate_stream_endpoint.empty (),
                "SM-D12 alternate stream endpoint is missing");
        const auto actor_id = std::string ("stream-reconnect-d12");
        const auto display_name = actor_id + "-display";
        auto actor = ensure_actor_ref (routes, "play-a", actor_id, display_name);

        auto first_core = make_stream_connector ();
        first_core.codecs ().add_json ();
        auto first = zlink::stream_e2e_client::use (first_core);
        auto first_connected = first.connect ().submit ();
        ensure (static_cast<bool> (first_connected), "SM-D12 first stream connect failed");

        auto first_auth = first.request (e2e::stream_auth_req_t{"play-a", actor_id, display_name, actor})
                            .packet_name ("StreamAuthReq")
                            .timeout (std::chrono::milliseconds (3000))
                            .async<e2e::stream_auth_res_t> ()
                            .result ();
        ensure (static_cast<bool> (first_auth),
                "SM-D12 first auth failed: " + stream_error_text (first_auth));

        auto joined = first.request (e2e::join_req_t{.key = "a-stream-reconnect",
                                               .actor_id = actor_id,
                                               .display_name = display_name,
                                               .level = 121,
                                               .tags = {"stream", "SM-D12", "session-a"}})
                        .packet_name ("JoinReq")
                        .timeout (std::chrono::milliseconds (5000))
                        .async<e2e::join_res_t> ()
                        .result ();
        ensure (static_cast<bool> (joined),
                "SM-D12 first join failed: " + stream_error_text (joined));
        ensure (joined.value ().owner_node_rid == "play-a", "SM-D12 first owner mismatch");
        actor = joined.value ().actor;

        auto first_state = first.request (e2e::state_req_t{.op = "add", .amount = 11})
                             .packet_name ("StateReq")
                             .timeout (std::chrono::milliseconds (5000))
                             .async<e2e::state_res_t> ()
                             .result ();
        ensure (static_cast<bool> (first_state),
                "SM-D12 first state failed: " + stream_error_text (first_state));
        ensure (first_state.value ().value == 11, "SM-D12 first state value mismatch");
        (void) first.close ().submit ();
        std::this_thread::sleep_for (std::chrono::milliseconds (200));
        actor = ensure_actor_ref (routes, "play-a", actor_id, display_name);

        auto second_core = make_stream_connector (_alternate_stream_endpoint);
        second_core.codecs ().add_json ();
        auto second = zlink::stream_e2e_client::use (second_core);
        auto second_connected = second.connect ().submit ();
        ensure (static_cast<bool> (second_connected), "SM-D12 second stream connect failed");

        auto second_auth =
          second.request (e2e::stream_auth_req_t{"play-a", actor_id, display_name, actor})
            .packet_name ("StreamAuthReq")
            .timeout (std::chrono::milliseconds (3000))
            .async<e2e::stream_auth_res_t> ()
            .result ();
        ensure (static_cast<bool> (second_auth),
                "SM-D12 second auth failed: " + stream_error_text (second_auth));

        auto snapshot = second.request (e2e::state_req_t{.op = "add", .amount = 0})
                          .packet_name ("StateReq")
                          .timeout (std::chrono::milliseconds (5000))
                          .async<e2e::state_res_t> ()
                          .result ();
        ensure (static_cast<bool> (snapshot),
                "SM-D12 snapshot failed: " + stream_error_text (snapshot));
        ensure (snapshot.value ().owner_node_rid == "play-a" && snapshot.value ().value == 11,
                "SM-D12 snapshot value mismatch");

        auto resumed = second.request (e2e::state_req_t{.op = "add", .amount = 5})
                         .packet_name ("StateReq")
                         .timeout (std::chrono::milliseconds (5000))
                         .async<e2e::state_res_t> ()
                         .result ();
        ensure (static_cast<bool> (resumed),
                "SM-D12 resumed state failed: " + stream_error_text (resumed));
        ensure (resumed.value ().value == 16, "SM-D12 resumed state value mismatch");

        auto push_wait =
          second.wait_for<e2e::actor_push_notify_t> (std::chrono::milliseconds (10000))
            .to_future ("SM-D12 push notify missing after rebind");
        auto pushed = second.request (e2e::actor_push_req_t{"stream-reconnect-d12-push"})
                        .packet_name ("PushReq")
                        .timeout (std::chrono::milliseconds (5000))
                        .async<e2e::actor_push_res_t> ()
                        .result ();
        ensure (static_cast<bool> (pushed),
                "SM-D12 push trigger failed: " + stream_error_text (pushed));
        auto notify = push_wait.get ();
        ensure (notify.actor_id == actor_id && notify.value == "stream-reconnect-d12-push",
                "SM-D12 push notify mismatch");

        (void) second.close ().submit ();
        std::cout << "scenario SM-D12 passed\n";
    }

    void run_multi_stream_session_scenario (zlink::framework::route_client_t &routes)
    {
        const auto first_actor_id = std::string ("stream-multi-a");
        const auto second_actor_id = std::string ("stream-multi-b");
        auto first_actor =
          ensure_actor_ref (routes, "play-a", first_actor_id, first_actor_id + "-display");
        auto second_actor =
          ensure_actor_ref (routes, "play-b", second_actor_id, second_actor_id + "-display");

        auto core = make_stream_connector ();
        core.codecs ().add_json ();
        auto stream = zlink::stream_e2e_client::use (core);

        auto connected = stream.connect ().submit ();
        ensure (static_cast<bool> (connected), "SM-D4 stream connect failed");

        auto auth_first =
          stream.request (e2e::stream_auth_req_t{"play-a", first_actor_id, first_actor_id + "-display",
                                           first_actor})
            .packet_name ("StreamAuthReq")
            .timeout (std::chrono::milliseconds (3000))
            .async<e2e::stream_auth_res_t> ()
            .result ();
        ensure (static_cast<bool> (auth_first),
                "SM-D4 first stream auth failed: " + stream_error_text (auth_first));
        std::this_thread::sleep_for (std::chrono::milliseconds (200));

        auto auth_second =
          stream.request (e2e::stream_auth_req_t{"play-b", second_actor_id, second_actor_id + "-display",
                                           second_actor})
            .packet_name ("StreamAuthReq")
            .timeout (std::chrono::milliseconds (3000))
            .async<e2e::stream_auth_res_t> ()
            .result ();
        ensure (static_cast<bool> (auth_second),
                "SM-D4 second stream auth failed: " + stream_error_text (auth_second));
        std::this_thread::sleep_for (std::chrono::milliseconds (200));

        auto join_first = stream.request (e2e::join_req_t{.key = "a-stream-multi",
                                                    .actor_id = first_actor_id,
                                                    .display_name = first_actor_id + "-display",
                                                    .level = 51,
                                                    .tags = {"stream", "SM-D4", "first"}})
                            .packet_name ("JoinReq")
                            .metadata ("actor-id", first_actor_id)
                            .timeout (std::chrono::milliseconds (5000))
                            .async<e2e::join_res_t> ()
                            .result ();
        ensure (static_cast<bool> (join_first),
                "SM-D4 first stream join failed: " + stream_error_text (join_first));

        auto join_second = stream.request (e2e::join_req_t{.key = "b-stream-multi",
                                                     .actor_id = second_actor_id,
                                                     .display_name = second_actor_id + "-display",
                                                     .level = 61,
                                                     .tags = {"stream", "SM-D4", "second"}})
                             .packet_name ("JoinReq")
                             .metadata ("actor-id", second_actor_id)
                             .timeout (std::chrono::milliseconds (5000))
                             .async<e2e::join_res_t> ()
                             .result ();
        ensure (static_cast<bool> (join_second),
                "SM-D4 second stream join failed: " + stream_error_text (join_second));

        auto state_first = stream.request (e2e::state_req_t{.op = "add", .amount = 23})
                             .packet_name ("StateReq")
                             .metadata ("actor-id", first_actor_id)
                             .timeout (std::chrono::milliseconds (5000))
                             .async<e2e::state_res_t> ()
                             .result ();
        ensure (static_cast<bool> (state_first),
                "SM-D4 first state send failed: " + stream_error_text (state_first));

        auto state_second = stream.request (e2e::state_req_t{.op = "add", .amount = 29})
                              .packet_name ("StateReq")
                              .metadata ("actor-id", second_actor_id)
                              .timeout (std::chrono::milliseconds (5000))
                              .async<e2e::state_res_t> ()
                              .result ();
        ensure (static_cast<bool> (state_second),
                "SM-D4 second state send failed: " + stream_error_text (state_second));

        auto first_push_wait =
          stream.wait_for<e2e::actor_push_notify_t> (std::chrono::milliseconds (10000))
            .to_future ("SM-D4 first push notify missing");
        auto first_pushed = stream.request (e2e::actor_push_req_t{"stream-multi-a-push"})
                              .packet_name ("PushReq")
                              .metadata ("actor-id", first_actor_id)
                              .timeout (std::chrono::milliseconds (5000))
                              .async<e2e::actor_push_res_t> ()
                              .result ();
        ensure (static_cast<bool> (first_pushed),
                "SM-D4 first push trigger failed: " + stream_error_text (first_pushed));
        auto first_push = first_push_wait.get ();
        ensure (first_push.actor_id == first_actor_id && first_push.value == "stream-multi-a-push",
                "SM-D4 first push routed to wrong actor: " + first_push.actor_id + "/"
                  + first_push.value);

        auto second_push_wait =
          stream.wait_for<e2e::actor_push_notify_t> (std::chrono::milliseconds (10000))
            .to_future ("SM-D4 second push notify missing");
        auto second_pushed = stream.request (e2e::actor_push_req_t{"stream-multi-b-push"})
                               .packet_name ("PushReq")
                               .metadata ("actor-id", second_actor_id)
                               .timeout (std::chrono::milliseconds (5000))
                               .async<e2e::actor_push_res_t> ()
                               .result ();
        ensure (static_cast<bool> (second_pushed),
                "SM-D4 second push trigger failed: " + stream_error_text (second_pushed));
        auto second_push = second_push_wait.get ();
        ensure (second_push.actor_id == second_actor_id
                  && second_push.value == "stream-multi-b-push",
                "SM-D4 second push routed to wrong actor: " + second_push.actor_id + "/"
                  + second_push.value);

        auto missing_actor_id = stream.request (e2e::state_req_t{.op = "add", .amount = 1})
                                  .packet_name ("StateReq")
                                  .timeout (std::chrono::milliseconds (3000))
                                  .async<e2e::state_res_t> ()
                                  .result ();
        ensure (!static_cast<bool> (missing_actor_id),
                "SM-D4 request without actor-id unexpectedly succeeded");

        (void) stream.close ().submit ();
        std::cout << "scenario SM-D4 passed\n";
    }

    zlink::framework::app_t &_app;
    std::string _stream_endpoint;
    std::string _alternate_stream_endpoint;
    std::string _play_http_endpoint;
    std::string _scenario_mode;
    std::string _crash_ready_file;
    std::string _crash_go_file;
    std::string _crash_observed_file;
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
                    e2e::state_res_t,
                    e2e::actor_ping_req_t,
                    e2e::slow_actor_ping_req_t,
                    e2e::actor_ping_res_t,
                    e2e::leave_req_t,
                    e2e::leave_res_t,
                    e2e::destroy_actor_req_t,
                    e2e::destroy_actor_res_t,
                    e2e::disconnect_req_t,
                    e2e::disconnect_res_t,
                    e2e::channel_echo_req_t,
                    e2e::channel_echo_res_t,
                    e2e::channel_command_t,
                    e2e::mesh_event_t,
                    e2e::outbound_req_t,
                    e2e::outbound_res_t,
                    e2e::worker_req_t,
                    e2e::worker_res_t,
                    e2e::direct_spot_req_t,
                    e2e::direct_spot_res_t,
                    e2e::direct_spot_command_t,
                    e2e::slow_spot_req_t,
                    e2e::unhandled_spot_req_t,
                    e2e::spot_to_spot_req_t,
                    e2e::spot_to_spot_res_t,
                    e2e::type_mismatch_req_t,
                    e2e::type_mismatch_res_t,
                    e2e::lifecycle_req_t,
                    e2e::lifecycle_res_t,
                    e2e::stream_auth_req_t,
                    e2e::stream_ensure_auth_req_t,
                    e2e::stream_auth_res_t,
                    e2e::actor_push_req_t,
                    e2e::actor_push_res_t,
                    e2e::actor_push_notify_t> ();
}

} // namespace

int main (int argc, char **argv)
{
    const auto log_dir = env_or ("ZLINK_CPP_E2E_LOG_DIR", "logs");
    const auto route_endpoint = env_or ("ZLINK_CPP_E2E_ROUTE_ENDPOINT");
    const auto route_a_endpoint = env_or ("ZLINK_CPP_E2E_ROUTE_A_ENDPOINT");
    const auto route_b_endpoint = env_or ("ZLINK_CPP_E2E_ROUTE_B_ENDPOINT");
    const auto spot_router_endpoint = env_or ("ZLINK_CPP_E2E_SPOT_ROUTER_ENDPOINT");
    const auto pubsub_endpoint = env_or ("ZLINK_CPP_E2E_PUBSUB_ENDPOINT");
    const auto publisher_endpoint = env_or ("ZLINK_CPP_E2E_PUBLISHER_ENDPOINT");
    const auto api_endpoint = env_or ("ZLINK_CPP_E2E_API_ENDPOINT");
    const auto stream_endpoint = env_or ("ZLINK_CPP_E2E_STREAM_ENDPOINT");
    const auto alternate_stream_endpoint = env_or ("ZLINK_CPP_E2E_ALT_STREAM_ENDPOINT");
    const auto tls_stream_endpoint = env_or ("ZLINK_CPP_E2E_TLS_STREAM_ENDPOINT");
    const auto scenario_mode = env_or ("ZLINK_CPP_E2E_SCENARIO_MODE");
    const auto play_http_endpoint = env_or ("ZLINK_CPP_E2E_PLAY_HTTP_ENDPOINT");
    const auto play_b_http_endpoint = env_or ("ZLINK_CPP_E2E_PLAY_B_HTTP_ENDPOINT");
    const auto session_http_endpoint = env_or ("ZLINK_CPP_E2E_SESSION_HTTP_ENDPOINT");
    const auto gateway_http_endpoint = env_or ("ZLINK_CPP_E2E_GATEWAY_HTTP_ENDPOINT");
    const auto registry_router = env_or ("ZLINK_CPP_E2E_REGISTRY_ROUTER");
    const auto client_rid = env_or ("ZLINK_CPP_E2E_CLIENT_RID", "client-session");
    const auto crash_ready_file = env_or ("ZLINK_CPP_E2E_CRASH_READY_FILE");
    const auto crash_go_file = env_or ("ZLINK_CPP_E2E_CRASH_GO_FILE");
    const auto crash_observed_file = env_or ("ZLINK_CPP_E2E_CRASH_OBSERVED_FILE");

    if (scenario_mode == "sm-a1") {
        try {
            zlink::framework::e2e::spot_service::client::scenarios::run_sm_a1_scenario (
              play_http_endpoint);
            std::cout << "scenario SM-A1 passed\n";
            return 0;
        }
        catch (const std::exception &error) {
            std::cerr << "spot-service scenario failed: " << error.what () << std::endl;
            return 1;
        }
    }
    if (scenario_mode == "sm-a2") {
        try {
            zlink::framework::e2e::spot_service::client::scenarios::run_sm_a2_scenario (
              play_http_endpoint);
            std::cout << "scenario SM-A2 passed\n";
            return 0;
        }
        catch (const std::exception &error) {
            std::cerr << "spot-service scenario failed: " << error.what () << std::endl;
            return 1;
        }
    }
    if (scenario_mode == "sm-a3") {
        try {
            zlink::framework::e2e::spot_service::client::scenarios::run_sm_a3_scenario (
              play_http_endpoint, play_b_http_endpoint);
            std::cout << "scenario SM-A3 passed\n";
            return 0;
        }
        catch (const std::exception &error) {
            std::cerr << "spot-service scenario failed: " << error.what () << std::endl;
            return 1;
        }
    }
    if (scenario_mode == "sm-a4") {
        try {
            zlink::framework::e2e::spot_service::client::scenarios::run_sm_a4_scenario (
              play_http_endpoint, play_b_http_endpoint);
            std::cout << "scenario SM-A4 passed\n";
            return 0;
        }
        catch (const std::exception &error) {
            std::cerr << "spot-service scenario failed: " << error.what () << std::endl;
            return 1;
        }
    }
    if (scenario_mode == "sm-a6") {
        try {
            zlink::framework::e2e::spot_service::client::scenarios::run_sm_a6_scenario (
              play_http_endpoint);
            std::cout << "scenario SM-A6 passed\n";
            return 0;
        }
        catch (const std::exception &error) {
            std::cerr << "spot-service scenario failed: " << error.what () << std::endl;
            return 1;
        }
    }
    if (scenario_mode == "sm-a7") {
        try {
            zlink::framework::e2e::spot_service::client::scenarios::run_sm_a7_scenario (
              play_http_endpoint);
            std::cout << "scenario SM-A7 passed\n";
            return 0;
        }
        catch (const std::exception &error) {
            std::cerr << "spot-service scenario failed: " << error.what () << std::endl;
            return 1;
        }
    }
    if (scenario_mode == "sm-a8") {
        try {
            zlink::framework::e2e::spot_service::client::scenarios::run_sm_a8_scenario (
              play_http_endpoint, play_b_http_endpoint);
            std::cout << "scenario SM-A8 passed\n";
            return 0;
        }
        catch (const std::exception &error) {
            std::cerr << "spot-service scenario failed: " << error.what () << std::endl;
            return 1;
        }
    }
    if (scenario_mode == "sm-b1") {
        try {
            zlink::framework::e2e::spot_service::client::scenarios::run_sm_b1_scenario (
              play_http_endpoint);
            std::cout << "scenario SM-B1 passed\n";
            return 0;
        }
        catch (const std::exception &error) {
            std::cerr << "spot-service scenario failed: " << error.what () << std::endl;
            return 1;
        }
    }
    if (scenario_mode == "sm-b2") {
        try {
            zlink::framework::e2e::spot_service::client::scenarios::run_sm_b2_scenario (
              play_http_endpoint);
            std::cout << "scenario SM-B2 passed\n";
            return 0;
        }
        catch (const std::exception &error) {
            std::cerr << "spot-service scenario failed: " << error.what () << std::endl;
            return 1;
        }
    }
    if (scenario_mode == "sm-b3") {
        try {
            zlink::framework::e2e::spot_service::client::scenarios::run_sm_b3_scenario (
              play_http_endpoint);
            std::cout << "scenario SM-B3 passed\n";
            return 0;
        }
        catch (const std::exception &error) {
            std::cerr << "spot-service scenario failed: " << error.what () << std::endl;
            return 1;
        }
    }
    if (scenario_mode == "sm-b4") {
        try {
            zlink::framework::e2e::spot_service::client::scenarios::run_sm_b4_scenario (
              play_http_endpoint);
            std::cout << "scenario SM-B4 passed\n";
            return 0;
        }
        catch (const std::exception &error) {
            std::cerr << "spot-service scenario failed: " << error.what () << std::endl;
            return 1;
        }
    }
    if (scenario_mode == "sm-b5") {
        try {
            zlink::framework::e2e::spot_service::client::scenarios::run_sm_b5_scenario (
              play_http_endpoint);
            std::cout << "scenario SM-B5 passed\n";
            return 0;
        }
        catch (const std::exception &error) {
            std::cerr << "spot-service scenario failed: " << error.what () << std::endl;
            return 1;
        }
    }
    if (scenario_mode == "sm-b6") {
        try {
            zlink::framework::e2e::spot_service::client::scenarios::run_sm_b6_scenario (
              play_http_endpoint, stream_endpoint);
            std::cout << "scenario SM-B6 passed\n";
            return 0;
        }
        catch (const std::exception &error) {
            std::cerr << "spot-service scenario failed: " << error.what () << std::endl;
            return 1;
        }
    }
    if (scenario_mode == "sm-b7") {
        try {
            zlink::framework::e2e::spot_service::client::scenarios::run_sm_b7_scenario (
              play_http_endpoint, stream_endpoint);
            std::cout << "scenario SM-B7 passed\n";
            return 0;
        }
        catch (const std::exception &error) {
            std::cerr << "spot-service scenario failed: " << error.what () << std::endl;
            return 1;
        }
    }
    if (scenario_mode == "sm-b8") {
        try {
            zlink::framework::e2e::spot_service::client::scenarios::run_sm_b8_scenario (
              play_http_endpoint, stream_endpoint);
            std::cout << "scenario SM-B8 passed\n";
            return 0;
        }
        catch (const std::exception &error) {
            std::cerr << "spot-service scenario failed: " << error.what () << std::endl;
            return 1;
        }
    }
    if (scenario_mode == "sm-c1") {
        try {
            zlink::framework::e2e::spot_service::client::scenarios::run_sm_c1_scenario (
              play_http_endpoint, play_b_http_endpoint);
            std::cout << "scenario SM-C1 passed\n";
            return 0;
        }
        catch (const std::exception &error) {
            std::cerr << "spot-service scenario failed: " << error.what () << std::endl;
            return 1;
        }
    }
    if (scenario_mode == "sm-c2") {
        try {
            zlink::framework::e2e::spot_service::client::scenarios::run_sm_c2_scenario (
              play_http_endpoint, play_b_http_endpoint);
            std::cout << "scenario SM-C2 passed\n";
            return 0;
        }
        catch (const std::exception &error) {
            std::cerr << "spot-service scenario failed: " << error.what () << std::endl;
            return 1;
        }
    }
    if (scenario_mode == "sm-c3") {
        try {
            zlink::framework::e2e::spot_service::client::scenarios::run_sm_c3_scenario (
              play_http_endpoint, play_b_http_endpoint);
            std::cout << "scenario SM-C3 passed\n";
            return 0;
        }
        catch (const std::exception &error) {
            std::cerr << "spot-service scenario failed: " << error.what () << std::endl;
            return 1;
        }
    }
    if (scenario_mode == "sm-c4") {
        try {
            zlink::framework::e2e::spot_service::client::scenarios::run_sm_c4_scenario (
              play_http_endpoint, gateway_http_endpoint);
            std::cout << "scenario SM-C4 passed\n";
            return 0;
        }
        catch (const std::exception &error) {
            std::cerr << "spot-service scenario failed: " << error.what () << std::endl;
            return 1;
        }
    }
    if (scenario_mode == "sm-d1") {
        try {
            zlink::framework::e2e::spot_service::client::scenarios::run_sm_d1_scenario (
              play_http_endpoint, stream_endpoint);
            std::cout << "scenario SM-D1 passed\n";
            return 0;
        }
        catch (const std::exception &error) {
            std::cerr << "spot-service scenario failed: " << error.what () << std::endl;
            return 1;
        }
    }
    if (scenario_mode == "sm-d2") {
        try {
            zlink::framework::e2e::spot_service::client::scenarios::run_sm_d2_scenario (
              play_b_http_endpoint, stream_endpoint);
            std::cout << "scenario SM-D2 passed\n";
            return 0;
        }
        catch (const std::exception &error) {
            std::cerr << "spot-service scenario failed: " << error.what () << std::endl;
            return 1;
        }
    }
    if (scenario_mode == "sm-d3") {
        try {
            zlink::framework::e2e::spot_service::client::scenarios::run_sm_d3_scenario (
              play_http_endpoint, stream_endpoint);
            std::cout << "scenario SM-D3 passed\n";
            return 0;
        }
        catch (const std::exception &error) {
            std::cerr << "spot-service scenario failed: " << error.what () << std::endl;
            return 1;
        }
    }
    if (scenario_mode == "sm-d4") {
        try {
            zlink::framework::e2e::spot_service::client::scenarios::run_sm_d4_scenario (
              stream_endpoint);
            std::cout << "scenario SM-D4 passed\n";
            return 0;
        }
        catch (const std::exception &error) {
            std::cerr << "spot-service scenario failed: " << error.what () << std::endl;
            return 1;
        }
    }
    if (scenario_mode == "sm-d5") {
        try {
            zlink::framework::e2e::spot_service::client::scenarios::run_sm_d5_scenario (
              stream_endpoint);
            std::cout << "scenario SM-D5 passed\n";
            return 0;
        }
        catch (const std::exception &error) {
            std::cerr << "spot-service scenario failed: " << error.what () << std::endl;
            return 1;
        }
    }
    if (scenario_mode == "sm-d6") {
        try {
            zlink::framework::e2e::spot_service::client::scenarios::run_sm_d6_scenario (
              stream_endpoint, alternate_stream_endpoint, play_http_endpoint);
            std::cout << "scenario SM-D6 passed\n";
            return 0;
        }
        catch (const std::exception &error) {
            std::cerr << "spot-service scenario failed: " << error.what () << std::endl;
            return 1;
        }
    }
    if (scenario_mode == "sm-d7") {
        try {
            zlink::framework::e2e::spot_service::client::scenarios::run_sm_d7_scenario (
              stream_endpoint);
            std::cout << "scenario SM-D7 passed\n";
            return 0;
        }
        catch (const std::exception &error) {
            std::cerr << "spot-service scenario failed: " << error.what () << std::endl;
            return 1;
        }
    }
    if (scenario_mode == "sm-d8") {
        try {
            zlink::framework::e2e::spot_service::client::scenarios::run_sm_d8_scenario (
              stream_endpoint);
            std::cout << "scenario SM-D8 passed\n";
            return 0;
        }
        catch (const std::exception &error) {
            std::cerr << "spot-service scenario failed: " << error.what () << std::endl;
            return 1;
        }
    }
    if (scenario_mode == "sm-d9") {
        try {
            zlink::framework::e2e::spot_service::client::scenarios::run_sm_d9_scenario (
              stream_endpoint);
            std::cout << "scenario SM-D9 passed\n";
            return 0;
        }
        catch (const std::exception &error) {
            std::cerr << "spot-service scenario failed: " << error.what () << std::endl;
            return 1;
        }
    }
    if (scenario_mode == "sm-d10") {
        try {
            zlink::framework::e2e::spot_service::client::scenarios::run_sm_d10_scenario (
              stream_endpoint, alternate_stream_endpoint);
            std::cout << "scenario SM-D10 passed\n";
            return 0;
        }
        catch (const std::exception &error) {
            std::cerr << "spot-service scenario failed: " << error.what () << std::endl;
            return 1;
        }
    }
    if (scenario_mode == "sm-d11") {
        try {
            zlink::framework::e2e::spot_service::client::scenarios::run_sm_d11_scenario (
              session_http_endpoint, stream_endpoint);
            std::cout << "scenario SM-D11 passed\n";
            return 0;
        }
        catch (const std::exception &error) {
            std::cerr << "spot-service scenario failed: " << error.what () << std::endl;
            return 1;
        }
    }
    if (scenario_mode == "sm-d12") {
        try {
            zlink::framework::e2e::spot_service::client::scenarios::run_sm_d12_scenario (
              stream_endpoint, alternate_stream_endpoint);
            std::cout << "scenario SM-D12 passed\n";
            return 0;
        }
        catch (const std::exception &error) {
            std::cerr << "spot-service scenario failed: " << error.what () << std::endl;
            return 1;
        }
    }
    if (scenario_mode == "sm-d13") {
        try {
            zlink::framework::e2e::spot_service::client::scenarios::run_sm_d13_scenario (
              stream_endpoint);
            std::cout << "scenario SM-D13 passed\n";
            return 0;
        }
        catch (const std::exception &error) {
            std::cerr << "spot-service scenario failed: " << error.what () << std::endl;
            return 1;
        }
    }
    if (scenario_mode == "sm-d14") {
        try {
            zlink::framework::e2e::spot_service::client::scenarios::run_sm_d14_scenario (
              tls_stream_endpoint);
            std::cout << "scenario SM-D14 passed\n";
            return 0;
        }
        catch (const std::exception &error) {
            std::cerr << "spot-service scenario failed: " << error.what () << std::endl;
            return 1;
        }
    }
    if (scenario_mode == "sm-e1") {
        try {
            zlink::framework::e2e::spot_service::client::scenarios::run_sm_e1_scenario (
              play_http_endpoint, play_b_http_endpoint);
            std::cout << "scenario SM-E1 passed\n";
            return 0;
        }
        catch (const std::exception &error) {
            std::cerr << "spot-service scenario failed: " << error.what () << std::endl;
            return 1;
        }
    }
    if (scenario_mode == "sm-e2") {
        try {
            zlink::framework::e2e::spot_service::client::scenarios::run_sm_e2_scenario (
              play_http_endpoint);
            std::cout << "scenario SM-E2 passed\n";
            return 0;
        }
        catch (const std::exception &error) {
            std::cerr << "spot-service scenario failed: " << error.what () << std::endl;
            return 1;
        }
    }
    if (scenario_mode == "sm-e3") {
        try {
            zlink::framework::e2e::spot_service::client::scenarios::run_sm_e3_scenario (
              play_http_endpoint);
            std::cout << "scenario SM-E3 passed\n";
            return 0;
        }
        catch (const std::exception &error) {
            std::cerr << "spot-service scenario failed: " << error.what () << std::endl;
            return 1;
        }
    }
    if (scenario_mode == "sm-e4") {
        try {
            zlink::framework::e2e::spot_service::client::scenarios::run_sm_e4_scenario (
              play_http_endpoint);
            std::cout << "scenario SM-E4 passed\n";
            return 0;
        }
        catch (const std::exception &error) {
            std::cerr << "spot-service scenario failed: " << error.what () << std::endl;
            return 1;
        }
    }
    if (scenario_mode == "sm-g2") {
        try {
            zlink::framework::e2e::spot_service::client::scenarios::run_sm_g2_scenario (
              play_http_endpoint, play_b_http_endpoint);
            std::cout << "scenario SM-G2 passed\n";
            return 0;
        }
        catch (const std::exception &error) {
            std::cerr << "spot-service scenario failed: " << error.what () << std::endl;
            return 1;
        }
    }
    if (scenario_mode == "sm-g3") {
        try {
            zlink::framework::e2e::spot_service::client::scenarios::run_sm_g3_scenario (
              play_http_endpoint, stream_endpoint);
            std::cout << "scenario SM-G3 passed\n";
            return 0;
        }
        catch (const std::exception &error) {
            std::cerr << "spot-service scenario failed: " << error.what () << std::endl;
            return 1;
        }
    }
    if (scenario_mode == "sm-g4") {
        try {
            zlink::framework::e2e::spot_service::client::scenarios::run_sm_g4_scenario (
              stream_endpoint);
            std::cout << "scenario SM-G4 passed\n";
            return 0;
        }
        catch (const std::exception &error) {
            std::cerr << "spot-service scenario failed: " << error.what () << std::endl;
            return 1;
        }
    }
    auto app = zlink::framework::app_t::create ();
    auto scenario = std::make_unique<scenario_service_t> (
      app, stream_endpoint, alternate_stream_endpoint, play_http_endpoint, scenario_mode,
      crash_ready_file, crash_go_file, crash_observed_file);
    auto *scenario_result = scenario.get ();
    app.logging ()
      .use_file (log_dir + "/client.log")
      .set_min_level (zlink::framework::log_level_t::debug);
    app.add_zlink_framework ([&] (zlink::framework::zlink_framework_options_t &options) {
        options.configure_dispatch ()
          .message_flow (zlink::framework::message_flow_log_mode_t::key_transitions)
          .trace_log_file (log_dir + "/client-flow.log")
          .trace_label ("cpp-sm-client");
        configure_codecs (options.codecs ());
        options.services ().add_singleton<client_channel_state_t> (
          std::make_unique<client_channel_state_t> ());
        options.use_discovery ().add_registry_endpoint (registry_router);
        auto api_channel = options.add_client_server_channel (e2e::api_channel)
                             .enable_server (api_endpoint)
                             .set_routing_id (
                               zlink::routing_id_t::from (std::string ("client-api")));
        if (scenario_mode == "stream") {
            api_channel.enable_client (api_endpoint);
        }
        api_channel.use_handler_group (e2e::handler_group);
        options.add_fanout_channel (e2e::publisher_channel).enable_publisher (publisher_endpoint);
        auto route = options.add_route_mesh (e2e::route_channel)
                       .enable_server (route_endpoint)
                       .set_routing_id (zlink::routing_id_t::from (client_rid))
                       .enable_client ();
        if (!route_a_endpoint.empty ()) {
            route.enable_client (route_a_endpoint);
        }
        if (!route_b_endpoint.empty ()) {
            route.enable_client (route_b_endpoint);
        }
        options.add_spot_mesh (e2e::spot_mesh)
          .use_registry_spot_resolver (e2e::route_channel)
          .set_routing_id (zlink::routing_id_t::from (client_rid))
          .enable_router (spot_router_endpoint)
          .enable_pub_sub (pubsub_endpoint);
        options.handlers ()
          .group (e2e::handler_group)
          .add<channel_echo_handler_t> ()
          .add_send<channel_command_handler_t> ();
    });
    app.add_hosted_service (std::move (scenario));
    const auto code = app.run (argc, argv);
    return code == 0 && scenario_result->passed ? 0 : 1;
}
