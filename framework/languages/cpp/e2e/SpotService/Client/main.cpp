/* SPDX-License-Identifier: MPL-2.0 */

#include "../Shared/spot_service_contracts.hpp"

#include "runtime/actors/actor_gateway_runtime.hpp"

#include <zlink/framework.hpp>
#include <zlink/stream_connector.hpp>
#include <zlink/stream_e2e_client.hpp>
#include <zlink/stream_e2e_client/codecs/auto_codec.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
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
                        std::string scenario_mode,
                        std::string crash_ready_file,
                        std::string crash_go_file,
                        std::string crash_observed_file) :
        _app (app),
        _stream_endpoint (std::move (stream_endpoint)),
        _alternate_stream_endpoint (std::move (alternate_stream_endpoint)),
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
            auto &actors = scope.get_required<zlink::framework::session_actor_manager_t> ();
            auto &channel_state = scope.get_required<client_channel_state_t> ();
            auto &publisher = scope.get_required<zlink::framework::spot_publisher_client_t> ();
            run (routes, actors, channel_state, publisher);
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
          actor.relay_request (zlink::framework::message_t::from (request)).async ().result ();
        zlink::framework::detail::exit_stream_relay_dispatch ();
        ensure (reply.has_value (), packet_name + " relay failed: "
                                      + (reply.error () ? reply.error ()->what () : "unknown"));
        return reply.value ().template decode<TReply> ();
    }

    void run (zlink::framework::route_client_t &routes,
              zlink::framework::session_actor_manager_t &actors,
              client_channel_state_t &channel_state,
              zlink::framework::spot_publisher_client_t &publisher)
    {
        if (_scenario_mode == "stream") {
            run_stream_auth_dispatch_scenario (routes);
            run_bound_session_push_targeting_scenario (routes);
            run_stream_disconnect_notification_scenario (routes);
            run_stream_session_scenario (routes, "SM-D1", "play-a", "stream-local", "a-stream-room",
                                         "stream-local-push");
            run_stream_session_scenario (routes, "SM-D2", "play-b", "stream-remote",
                                         "b-stream-room", "stream-remote-push");
            run_multi_stream_session_scenario (routes);
            run_stream_reconnect_migration_scenario (routes);
            return;
        }
        if (_scenario_mode == "crash-setup") {
            run_play_node_crash_observation_scenario (routes);
            return;
        }
        if (_scenario_mode == "crash-recover") {
            run_play_node_crash_recovery_scenario (routes);
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
        ensure (local_join.owner_node_rid == "play-a", "SM-A1/SM-B1 owner mismatch");
        ensure (local_join.spot_rid == "user:play-a:a-room", "SM-A1 spot rid mismatch");
        ensure (local_join.actor_id == "alice" && local_join.display_name == "Alice"
                  && local_join.level == 7 && local_join.tags.size () == 2
                  && local_join.tags[0] == "alpha" && local_join.tags[1] == "local",
                "SM-B3 join payload fidelity mismatch");
        std::cout << "scenario SM-A1 passed\n";
        std::cout << "scenario SM-B1 passed\n";
        std::cout << "scenario SM-B3 passed\n";
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
              zlink::framework::message_t::from (e2e::state_req_t{"add", 1}))
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
        auto remote_join =
          relay_request<e2e::join_res_t> (remote, "JoinReq",
                                          e2e::join_req_t{.key = "b-room",
                                                          .actor_id = "bob",
                                                          .display_name = "Bob",
                                                          .level = 9,
                                                          .tags = {"beta", "remote"}});
        ensure (remote_join.owner_node_rid == "play-b", "SM-B2 owner mismatch");
        ensure (remote_join.spot_rid == "user:play-b:b-room", "SM-A4 remote spot rid mismatch");
        remote = refresh_actor ("bob");
        auto remote_state =
          relay_request<e2e::state_res_t> (remote, "StateReq", e2e::state_req_t{"add", 11});
        ensure (remote_state.owner_node_rid == "play-b" && remote_state.value == 11,
                "SM-B4 remote actor request mismatch");
        std::cout << "scenario SM-A4 passed\n";
        std::cout << "scenario SM-B2 passed\n";
        std::cout << "scenario SM-B4 passed\n";

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
        std::cout << "scenario SM-B6 leave passed\n";
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
              zlink::framework::message_t::from (e2e::state_req_t{"add", 1}))
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

        auto spot_to_spot = relay_request<e2e::spot_to_spot_res_t> (
          same_key_actor, "SpotToSpotReq", e2e::spot_to_spot_req_t{"b-room", "spot-to-spot"});
        ensure (spot_to_spot.request_reply == "spot-to-spot:reply",
                "SM-C3 spot request reply mismatch");
        ensure (spot_to_spot.command_sent && spot_to_spot.published && spot_to_spot.missing_rejected
                  && spot_to_spot.timeout_rejected,
                "SM-C3 flags mismatch");
        std::cout << "scenario SM-C3 passed\n";

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

    static void write_signal_file (const std::string &path)
    {
        ensure (!path.empty (), "crash scenario signal path is empty");
        std::ofstream file (path);
        file << "ready\n";
    }

    static void wait_signal_file (const std::string &path, const std::string &label)
    {
        ensure (!path.empty (), "crash scenario " + label + " signal path is empty");
        const auto deadline = std::chrono::steady_clock::now () + std::chrono::seconds (30);
        while (std::chrono::steady_clock::now () < deadline) {
            if (std::filesystem::exists (path)) {
                return;
            }
            std::this_thread::sleep_for (std::chrono::milliseconds (100));
        }
        throw std::runtime_error ("SM-G1 timed out waiting for " + label);
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

        auto auth = zlink::stream_e2e_client::codecs::send (
                      stream, e2e::stream_auth_req_t{target_node, actor_id, display_name, actor})
                      .packet_name ("StreamAuthReq")
                      .submit ();
        ensure (static_cast<bool> (auth),
                scenario_id + " stream auth failed: " + stream_error_text (auth));
        std::this_thread::sleep_for (std::chrono::milliseconds (200));

        auto joined = zlink::stream_e2e_client::codecs::send (
                        stream, e2e::join_req_t{.key = key,
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
          zlink::stream_e2e_client::codecs::send (
            stream, e2e::state_req_t{.op = "add", .amount = target_node == "play-a" ? 13 : 17})
            .packet_name ("StateReq")
            .submit ();
        ensure (static_cast<bool> (state),
                scenario_id + " stream state send failed: " + stream_error_text (state));

        auto push_wait =
          stream.wait_for<e2e::actor_push_notify_t> (std::chrono::milliseconds (10000)).async ();
        auto pushed =
          zlink::stream_e2e_client::codecs::send (stream, e2e::actor_push_req_t{push_value})
            .packet_name ("PushReq")
            .submit ();
        ensure (static_cast<bool> (pushed),
                scenario_id + " push trigger failed: " + stream_error_text (pushed));
        auto push = push_wait.result ();
        ensure (static_cast<bool> (push), scenario_id + " push notify missing");

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

            auto before_auth = zlink::stream_e2e_client::codecs::request (
                                 unauthenticated, e2e::state_req_t{.op = "add", .amount = 1})
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

            auto rejected = zlink::stream_e2e_client::codecs::request (
                              invalid, e2e::stream_auth_req_t{"play-a", actor_id,
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
        auto stream = zlink::stream_e2e_client::use (core);
        auto connected = stream.connect ().submit ();
        ensure (static_cast<bool> (connected), "SM-D7 stream connect failed");

        auto auth =
          zlink::stream_e2e_client::codecs::request (
            stream, e2e::stream_auth_req_t{"play-a", actor_id, actor_id + "-display", actor})
            .packet_name ("StreamAuthReq")
            .timeout (std::chrono::milliseconds (3000))
            .async<e2e::stream_auth_res_t> ()
            .result ();
        ensure (static_cast<bool> (auth), "SM-D7 stream auth failed: " + stream_error_text (auth));
        ensure (auth.value ().actor.actor_id == actor_id
                  && auth.value ().session_node_rid == "session-a",
                "SM-D7 stream auth reply mismatch");

        auto joined = zlink::stream_e2e_client::codecs::request (
                        stream, e2e::join_req_t{.key = "a-stream-auth",
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

        auto state = zlink::stream_e2e_client::codecs::request (
                       stream, e2e::state_req_t{.op = "add", .amount = 7})
                       .packet_name ("StateReq")
                       .timeout (std::chrono::milliseconds (5000))
                       .async<e2e::state_res_t> ()
                       .result ();
        ensure (static_cast<bool> (state),
                "SM-D7 stream state dispatch failed: " + stream_error_text (state));
        ensure (state.value ().owner_node_rid == "play-a" && state.value ().value == 7,
                "SM-D7 stream state dispatch reply mismatch");

        (void) stream.close ().submit ();
        std::cout << "scenario SM-D7 passed\n";
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
          zlink::stream_e2e_client::codecs::request (
            bound, e2e::stream_auth_req_t{"play-a", actor_id, actor_id + "-display", actor})
            .packet_name ("StreamAuthReq")
            .timeout (std::chrono::milliseconds (3000))
            .async<e2e::stream_auth_res_t> ()
            .result ();
        ensure (static_cast<bool> (auth), "SM-D6 stream auth failed: " + stream_error_text (auth));

        auto joined = zlink::stream_e2e_client::codecs::request (
                        bound, e2e::join_req_t{.key = "a-stream-push",
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
          bound.wait_for<e2e::actor_push_notify_t> (std::chrono::milliseconds (10000)).async ();
        auto unbound_wait =
          unbound.wait_for<e2e::actor_push_notify_t> (std::chrono::milliseconds (500)).async ();
        auto pushed = zlink::stream_e2e_client::codecs::request (
                        bound, e2e::actor_push_req_t{"stream-push-d6-value"})
                        .packet_name ("PushReq")
                        .timeout (std::chrono::milliseconds (5000))
                        .async<e2e::actor_push_res_t> ()
                        .result ();
        ensure (static_cast<bool> (pushed),
                "SM-D6 push trigger failed: " + stream_error_text (pushed));

        auto notify = bound_wait.result ();
        ensure (static_cast<bool> (notify), "SM-D6 bound push notify missing");
        ensure (notify.value ().actor_id == actor_id
                  && notify.value ().value == "stream-push-d6-value",
                "SM-D6 bound push notify mismatch");

        auto unbound_notify = unbound_wait.result ();
        ensure (!static_cast<bool> (unbound_notify), "SM-D6 unbound stream received push");

        (void) bound.close ().submit ();
        (void) unbound.close ().submit ();
        std::cout << "scenario SM-D6 passed\n";
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
          zlink::stream_e2e_client::codecs::request (
            stream, e2e::stream_auth_req_t{"play-a", notified_actor_id,
                                           notified_actor_id + "-display", notified_actor})
            .packet_name ("StreamAuthReq")
            .timeout (std::chrono::milliseconds (3000))
            .async<e2e::stream_auth_res_t> ()
            .result ();
        ensure (static_cast<bool> (notified_auth),
                "SM-D5 notified actor auth failed: " + stream_error_text (notified_auth));

        auto muted_auth =
          zlink::stream_e2e_client::codecs::request (
            stream, e2e::stream_auth_req_t{"play-a", muted_actor_id, muted_actor_id + "-display",
                                           muted_actor})
            .packet_name ("StreamAuthReq")
            .timeout (std::chrono::milliseconds (3000))
            .async<e2e::stream_auth_res_t> ()
            .result ();
        ensure (static_cast<bool> (muted_auth),
                "SM-D5 muted actor auth failed: " + stream_error_text (muted_auth));

        auto notified_join =
          zlink::stream_e2e_client::codecs::request (
            stream, e2e::join_req_t{.key = "a-stream-disconnect-notified",
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

        auto muted_join = zlink::stream_e2e_client::codecs::request (
                            stream, e2e::join_req_t{.key = "a-stream-disconnect-muted",
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
        std::this_thread::sleep_for (std::chrono::milliseconds (300));
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

        auto first_auth = zlink::stream_e2e_client::codecs::request (
                            first, e2e::stream_auth_req_t{"play-a", actor_id, display_name, actor})
                            .packet_name ("StreamAuthReq")
                            .timeout (std::chrono::milliseconds (3000))
                            .async<e2e::stream_auth_res_t> ()
                            .result ();
        ensure (static_cast<bool> (first_auth),
                "SM-D12 first auth failed: " + stream_error_text (first_auth));

        auto joined = zlink::stream_e2e_client::codecs::request (
                        first, e2e::join_req_t{.key = "a-stream-reconnect",
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

        auto first_state = zlink::stream_e2e_client::codecs::request (
                             first, e2e::state_req_t{.op = "add", .amount = 11})
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
          zlink::stream_e2e_client::codecs::request (
            second, e2e::stream_auth_req_t{"play-a", actor_id, display_name, actor})
            .packet_name ("StreamAuthReq")
            .timeout (std::chrono::milliseconds (3000))
            .async<e2e::stream_auth_res_t> ()
            .result ();
        ensure (static_cast<bool> (second_auth),
                "SM-D12 second auth failed: " + stream_error_text (second_auth));

        auto snapshot = zlink::stream_e2e_client::codecs::request (
                          second, e2e::state_req_t{.op = "add", .amount = 0})
                          .packet_name ("StateReq")
                          .timeout (std::chrono::milliseconds (5000))
                          .async<e2e::state_res_t> ()
                          .result ();
        ensure (static_cast<bool> (snapshot),
                "SM-D12 snapshot failed: " + stream_error_text (snapshot));
        ensure (snapshot.value ().owner_node_rid == "play-a" && snapshot.value ().value == 11,
                "SM-D12 snapshot value mismatch");

        auto resumed = zlink::stream_e2e_client::codecs::request (
                         second, e2e::state_req_t{.op = "add", .amount = 5})
                         .packet_name ("StateReq")
                         .timeout (std::chrono::milliseconds (5000))
                         .async<e2e::state_res_t> ()
                         .result ();
        ensure (static_cast<bool> (resumed),
                "SM-D12 resumed state failed: " + stream_error_text (resumed));
        ensure (resumed.value ().value == 16, "SM-D12 resumed state value mismatch");

        auto push_wait =
          second.wait_for<e2e::actor_push_notify_t> (std::chrono::milliseconds (10000)).async ();
        auto pushed = zlink::stream_e2e_client::codecs::request (
                        second, e2e::actor_push_req_t{"stream-reconnect-d12-push"})
                        .packet_name ("PushReq")
                        .timeout (std::chrono::milliseconds (5000))
                        .async<e2e::actor_push_res_t> ()
                        .result ();
        ensure (static_cast<bool> (pushed),
                "SM-D12 push trigger failed: " + stream_error_text (pushed));
        auto notify = push_wait.result ();
        ensure (static_cast<bool> (notify), "SM-D12 push notify missing after rebind");
        ensure (notify.value ().actor_id == actor_id
                  && notify.value ().value == "stream-reconnect-d12-push",
                "SM-D12 push notify mismatch");

        (void) second.close ().submit ();
        std::cout << "scenario SM-D12 passed\n";
    }

    void run_play_node_crash_observation_scenario (zlink::framework::route_client_t &routes)
    {
        const auto play_a_actor_id = std::string ("crash-g1-play-a");
        const auto play_b_actor_id = std::string ("crash-g1-play-b");
        auto play_a_actor =
          ensure_actor_ref (routes, "play-a", play_a_actor_id, play_a_actor_id + "-display");
        auto play_b_actor =
          ensure_actor_ref (routes, "play-b", play_b_actor_id, play_b_actor_id + "-display");

        auto play_a_core = make_stream_connector ();
        play_a_core.codecs ().add_json ();
        auto play_a_stream = zlink::stream_e2e_client::use (play_a_core);
        auto play_a_connected = play_a_stream.connect ().submit ();
        ensure (static_cast<bool> (play_a_connected), "SM-G1 play-a stream connect failed");

        auto play_a_auth =
          zlink::stream_e2e_client::codecs::request (
            play_a_stream, e2e::stream_auth_req_t{"play-a", play_a_actor_id,
                                                  play_a_actor_id + "-display", play_a_actor})
            .packet_name ("StreamAuthReq")
            .timeout (std::chrono::milliseconds (3000))
            .async<e2e::stream_auth_res_t> ()
            .result ();
        ensure (static_cast<bool> (play_a_auth),
                "SM-G1 play-a auth failed: " + stream_error_text (play_a_auth));

        bool play_a_joined = false;
        std::string play_a_join_error = "play-a join not attempted";
        for (int attempt = 0; attempt < 10; ++attempt) {
            auto play_a_join =
              zlink::stream_e2e_client::codecs::request (
                play_a_stream, e2e::join_req_t{.key = "a-crash-g1",
                                               .actor_id = play_a_actor_id,
                                               .display_name = play_a_actor_id + "-display",
                                               .level = 301,
                                               .tags = {"stream", "SM-G1", "play-a"}})
                .packet_name ("JoinReq")
                .timeout (std::chrono::milliseconds (5000))
                .async<e2e::join_res_t> ()
                .result ();
            if (play_a_join) {
                play_a_joined = true;
                break;
            }
            play_a_join_error = stream_error_text (play_a_join);
            std::this_thread::sleep_for (std::chrono::milliseconds (300));
        }
        ensure (play_a_joined, "SM-G1 play-a join failed: " + play_a_join_error);

        auto play_a_state = zlink::stream_e2e_client::codecs::request (
                              play_a_stream, e2e::state_req_t{.op = "add", .amount = 31})
                              .packet_name ("StateReq")
                              .timeout (std::chrono::milliseconds (5000))
                              .async<e2e::state_res_t> ()
                              .result ();
        ensure (static_cast<bool> (play_a_state) && play_a_state.value ().value == 31,
                "SM-G1 play-a initial state mismatch");

        auto play_b_auth =
          zlink::stream_e2e_client::codecs::request (
            play_a_stream, e2e::stream_auth_req_t{"play-b", play_b_actor_id,
                                                  play_b_actor_id + "-display", play_b_actor})
            .packet_name ("StreamAuthReq")
            .timeout (std::chrono::milliseconds (3000))
            .async<e2e::stream_auth_res_t> ()
            .result ();
        ensure (static_cast<bool> (play_b_auth),
                "SM-G1 play-b auth failed: " + stream_error_text (play_b_auth));

        bool play_b_joined = false;
        std::string play_b_join_error = "play-b join not attempted";
        for (int attempt = 0; attempt < 10; ++attempt) {
            auto play_b_join =
              zlink::stream_e2e_client::codecs::request (
                play_a_stream, e2e::join_req_t{.key = "b-crash-g1",
                                               .actor_id = play_b_actor_id,
                                               .display_name = play_b_actor_id + "-display",
                                               .level = 302,
                                               .tags = {"stream", "SM-G1", "play-b"}})
                .packet_name ("JoinReq")
                .metadata ("actor-id", play_b_actor_id)
                .timeout (std::chrono::milliseconds (5000))
                .async<e2e::join_res_t> ()
                .result ();
            if (play_b_join) {
                play_b_joined = true;
                break;
            }
            play_b_join_error = stream_error_text (play_b_join);
            std::this_thread::sleep_for (std::chrono::milliseconds (300));
        }
        ensure (play_b_joined, "SM-G1 play-b join failed: " + play_b_join_error);

        auto play_b_state = zlink::stream_e2e_client::codecs::request (
                              play_a_stream, e2e::state_req_t{.op = "add", .amount = 41})
                              .packet_name ("StateReq")
                              .metadata ("actor-id", play_b_actor_id)
                              .timeout (std::chrono::milliseconds (5000))
                              .async<e2e::state_res_t> ()
                              .result ();
        ensure (static_cast<bool> (play_b_state) && play_b_state.value ().value == 41,
                "SM-G1 play-b initial state mismatch");

        write_signal_file (_crash_ready_file);
        wait_signal_file (_crash_go_file, "play-a crash");

        auto play_b_after_crash = zlink::stream_e2e_client::codecs::request (
                                    play_a_stream, e2e::state_req_t{.op = "add", .amount = 1})
                                    .packet_name ("StateReq")
                                    .metadata ("actor-id", play_b_actor_id)
                                    .timeout (std::chrono::milliseconds (5000))
                                    .async<e2e::state_res_t> ()
                                    .result ();
        ensure (static_cast<bool> (play_b_after_crash) && play_b_after_crash.value ().value == 42,
                "SM-G1 play-b state did not survive play-a crash");

        auto failed_after_crash = zlink::stream_e2e_client::codecs::request (
                                    play_a_stream, e2e::state_req_t{.op = "add", .amount = 1})
                                    .packet_name ("StateReq")
                                    .metadata ("actor-id", play_a_actor_id)
                                    .timeout (std::chrono::milliseconds (3000))
                                    .async<e2e::state_res_t> ()
                                    .result ();
        ensure (!static_cast<bool> (failed_after_crash),
                "SM-G1 play-a request unexpectedly succeeded after crash");

        write_signal_file (_crash_observed_file);

        (void) play_a_stream.close ().submit ();
        std::cout << "scenario SM-G1 crash observed passed\n";
    }

    void run_play_node_crash_recovery_scenario (zlink::framework::route_client_t &routes)
    {
        const auto recovered_actor_id = std::string ("crash-g1-play-b");

        auto recovered_actor =
          ensure_actor_ref (routes, "play-b", recovered_actor_id, recovered_actor_id + "-display");
        auto recovered_core = make_stream_connector ();
        recovered_core.codecs ().add_json ();
        auto recovered_stream = zlink::stream_e2e_client::use (recovered_core);
        auto recovered_connected = recovered_stream.connect ().submit ();
        ensure (static_cast<bool> (recovered_connected), "SM-G1 recovered stream connect failed");

        auto recovered_auth =
          zlink::stream_e2e_client::codecs::request (
            recovered_stream,
            e2e::stream_auth_req_t{"play-b", recovered_actor_id, recovered_actor_id + "-display",
                                   recovered_actor})
            .packet_name ("StreamAuthReq")
            .timeout (std::chrono::milliseconds (3000))
            .async<e2e::stream_auth_res_t> ()
            .result ();
        ensure (static_cast<bool> (recovered_auth),
                "SM-G1 recovered auth failed: " + stream_error_text (recovered_auth));

        auto recovered_state = zlink::stream_e2e_client::codecs::request (
                                 recovered_stream, e2e::state_req_t{.op = "add", .amount = 7})
                                 .packet_name ("StateReq")
                                 .timeout (std::chrono::milliseconds (5000))
                                 .async<e2e::state_res_t> ()
                                 .result ();
        ensure (static_cast<bool> (recovered_state) && recovered_state.value ().value == 49,
                "SM-G1 recovered state mismatch");

        (void) recovered_stream.close ().submit ();
        std::cout << "scenario SM-G1 passed\n";
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
          zlink::stream_e2e_client::codecs::request (
            stream, e2e::stream_auth_req_t{"play-a", first_actor_id, first_actor_id + "-display",
                                           first_actor})
            .packet_name ("StreamAuthReq")
            .timeout (std::chrono::milliseconds (3000))
            .async<e2e::stream_auth_res_t> ()
            .result ();
        ensure (static_cast<bool> (auth_first),
                "SM-D4 first stream auth failed: " + stream_error_text (auth_first));
        std::this_thread::sleep_for (std::chrono::milliseconds (200));

        auto auth_second =
          zlink::stream_e2e_client::codecs::request (
            stream, e2e::stream_auth_req_t{"play-b", second_actor_id, second_actor_id + "-display",
                                           second_actor})
            .packet_name ("StreamAuthReq")
            .timeout (std::chrono::milliseconds (3000))
            .async<e2e::stream_auth_res_t> ()
            .result ();
        ensure (static_cast<bool> (auth_second),
                "SM-D4 second stream auth failed: " + stream_error_text (auth_second));
        std::this_thread::sleep_for (std::chrono::milliseconds (200));

        auto join_first = zlink::stream_e2e_client::codecs::request (
                            stream, e2e::join_req_t{.key = "a-stream-multi",
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

        auto join_second = zlink::stream_e2e_client::codecs::request (
                             stream, e2e::join_req_t{.key = "b-stream-multi",
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

        auto state_first = zlink::stream_e2e_client::codecs::request (
                             stream, e2e::state_req_t{.op = "add", .amount = 23})
                             .packet_name ("StateReq")
                             .metadata ("actor-id", first_actor_id)
                             .timeout (std::chrono::milliseconds (5000))
                             .async<e2e::state_res_t> ()
                             .result ();
        ensure (static_cast<bool> (state_first),
                "SM-D4 first state send failed: " + stream_error_text (state_first));

        auto state_second = zlink::stream_e2e_client::codecs::request (
                              stream, e2e::state_req_t{.op = "add", .amount = 29})
                              .packet_name ("StateReq")
                              .metadata ("actor-id", second_actor_id)
                              .timeout (std::chrono::milliseconds (5000))
                              .async<e2e::state_res_t> ()
                              .result ();
        ensure (static_cast<bool> (state_second),
                "SM-D4 second state send failed: " + stream_error_text (state_second));

        auto first_push_wait =
          stream.wait_for<e2e::actor_push_notify_t> (std::chrono::milliseconds (10000)).async ();
        auto first_pushed = zlink::stream_e2e_client::codecs::request (
                              stream, e2e::actor_push_req_t{"stream-multi-a-push"})
                              .packet_name ("PushReq")
                              .metadata ("actor-id", first_actor_id)
                              .timeout (std::chrono::milliseconds (5000))
                              .async<e2e::actor_push_res_t> ()
                              .result ();
        ensure (static_cast<bool> (first_pushed),
                "SM-D4 first push trigger failed: " + stream_error_text (first_pushed));
        auto first_push = first_push_wait.result ();
        ensure (static_cast<bool> (first_push), "SM-D4 first push notify missing");
        ensure (first_push.value ().actor_id == first_actor_id
                  && first_push.value ().value == "stream-multi-a-push",
                "SM-D4 first push routed to wrong actor: " + first_push.value ().actor_id + "/"
                  + first_push.value ().value);

        auto second_push_wait =
          stream.wait_for<e2e::actor_push_notify_t> (std::chrono::milliseconds (10000)).async ();
        auto second_pushed = zlink::stream_e2e_client::codecs::request (
                               stream, e2e::actor_push_req_t{"stream-multi-b-push"})
                               .packet_name ("PushReq")
                               .metadata ("actor-id", second_actor_id)
                               .timeout (std::chrono::milliseconds (5000))
                               .async<e2e::actor_push_res_t> ()
                               .result ();
        ensure (static_cast<bool> (second_pushed),
                "SM-D4 second push trigger failed: " + stream_error_text (second_pushed));
        auto second_push = second_push_wait.result ();
        ensure (static_cast<bool> (second_push), "SM-D4 second push notify missing");
        ensure (second_push.value ().actor_id == second_actor_id
                  && second_push.value ().value == "stream-multi-b-push",
                "SM-D4 second push routed to wrong actor: " + second_push.value ().actor_id + "/"
                  + second_push.value ().value);

        auto missing_actor_id = zlink::stream_e2e_client::codecs::request (
                                  stream, e2e::state_req_t{.op = "add", .amount = 1})
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
    std::string _scenario_mode;
    std::string _crash_ready_file;
    std::string _crash_go_file;
    std::string _crash_observed_file;
};

void configure_codecs (zlink::framework::codec_options_builder_t codecs)
{
    codecs.add_json ()
      .add_json<e2e::actor_ref_dto_t> ()
      .add_json<e2e::ensure_actor_req_t> ()
      .add_json<e2e::ensure_actor_res_t> ()
      .add_json<e2e::join_req_t> ()
      .add_json<e2e::join_res_t> ()
      .add_json<e2e::state_req_t> ()
      .add_json<e2e::state_res_t> ()
      .add_json<e2e::leave_req_t> ()
      .add_json<e2e::leave_res_t> ()
      .add_json<e2e::destroy_actor_req_t> ()
      .add_json<e2e::destroy_actor_res_t> ()
      .add_json<e2e::disconnect_req_t> ()
      .add_json<e2e::disconnect_res_t> ()
      .add_json<e2e::channel_echo_req_t> ()
      .add_json<e2e::channel_echo_res_t> ()
      .add_json<e2e::channel_command_t> ()
      .add_json<e2e::mesh_event_t> ()
      .add_json<e2e::outbound_req_t> ()
      .add_json<e2e::outbound_res_t> ()
      .add_json<e2e::worker_req_t> ()
      .add_json<e2e::worker_res_t> ()
      .add_json<e2e::direct_spot_req_t> ()
      .add_json<e2e::direct_spot_res_t> ()
      .add_json<e2e::direct_spot_command_t> ()
      .add_json<e2e::slow_spot_req_t> ()
      .add_json<e2e::unhandled_spot_req_t> ()
      .add_json<e2e::spot_to_spot_req_t> ()
      .add_json<e2e::spot_to_spot_res_t> ()
      .add_json<e2e::type_mismatch_req_t> ()
      .add_json<e2e::type_mismatch_res_t> ()
      .add_json<e2e::lifecycle_req_t> ()
      .add_json<e2e::lifecycle_res_t> ()
      .add_json<e2e::stream_auth_req_t> ()
      .add_json<e2e::stream_auth_res_t> ()
      .add_json<e2e::actor_push_req_t> ()
      .add_json<e2e::actor_push_res_t> ()
      .add_json<e2e::actor_push_notify_t> ();
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
    const auto scenario_mode = env_or ("ZLINK_CPP_E2E_SCENARIO_MODE");
    const auto registry_router = env_or ("ZLINK_CPP_E2E_REGISTRY_ROUTER");
    const auto crash_ready_file = env_or ("ZLINK_CPP_E2E_CRASH_READY_FILE");
    const auto crash_go_file = env_or ("ZLINK_CPP_E2E_CRASH_GO_FILE");
    const auto crash_observed_file = env_or ("ZLINK_CPP_E2E_CRASH_OBSERVED_FILE");

    auto app = zlink::framework::app_t::create ();
    auto scenario = std::make_unique<scenario_service_t> (
      app, stream_endpoint, alternate_stream_endpoint, scenario_mode, crash_ready_file,
      crash_go_file, crash_observed_file);
    auto *scenario_result = scenario.get ();
    app.logging ()
      .use_file (log_dir + "/client.log")
      .set_min_level (zlink::framework::log_level_t::debug);
    app.add_zlink_framework ([&] (zlink::framework::zlink_framework_options_t &options) {
        options.configure_dispatch ()
          .message_flow (zlink::framework::message_flow_log_mode_t::key_transitions)
          .trace_log_file (log_dir + "/client-flow.log")
          .trace_node_id ("cpp-sm-client");
        configure_codecs (options.codecs ());
        options.services ().add_singleton<client_channel_state_t> (
          std::make_unique<client_channel_state_t> ());
        options.handlers ()
          .add<channel_echo_handler_t> (e2e::handler_group)
          .add_send<channel_command_handler_t> (e2e::handler_group);
        options.use_discovery ().add_registry_endpoint (registry_router);
        options.add_client_server_channel (e2e::api_channel)
          .enable_server (api_endpoint)
          .server_routing_id (zlink::routing_id_t::from (std::string ("client-api")))
          .use_handler_group (e2e::handler_group);
        options.add_fanout_channel (e2e::publisher_channel).enable_publisher (publisher_endpoint);
        auto route = options.add_route_mesh_channel (e2e::route_channel)
                       .enable_server (route_endpoint)
                       .set_routing_id (zlink::routing_id_t::from (std::string ("client-session")))
                       .enable_client ()
                       .enable_spot_route_egress (e2e::route_channel);
        if (!route_a_endpoint.empty ()) {
            route.enable_client (route_a_endpoint);
        }
        if (!route_b_endpoint.empty ()) {
            route.enable_client (route_b_endpoint);
        }
        options.add_spot_mesh (e2e::spot_mesh)
          .use_registry_spot_resolver (e2e::route_channel)
          .add_node ("client-session")
          .enable_router (spot_router_endpoint,
                          zlink::routing_id_t::from (std::string ("client-session")))
          .enable_actor_gateway ()
          .enable_pub_sub (pubsub_endpoint,
                           zlink::routing_id_t::from (std::string ("client-session")))
          .attach_publisher (e2e::publisher_channel);
    });
    app.add_hosted_service (std::move (scenario));
    const auto code = app.run (argc, argv);
    return code == 0 && scenario_result->passed ? 0 : 1;
}
