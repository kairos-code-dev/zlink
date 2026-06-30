/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../Shared/spot_service_contracts.hpp"

#include <zlink/framework.hpp>
#include <zlink/stream_connector.hpp>
#include <zlink/stream_e2e_client.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace zlink::framework::e2e::spot_service::client::scenarios
{

template <typename TResult> std::string sm_g1_stream_error_text (const TResult &result)
{
    if (result.error ()) {
        return result.error ()->message;
    }
    return "unknown stream error";
}

inline actor_ref_dto_t sm_g1_ensure_actor_ref (zlink::framework::route_client_t &routes,
                                               const std::string &target_node,
                                               const std::string &actor_id,
                                               const std::string &display_name)
{
    zlink::framework::result_t<ensure_actor_res_t> ensured =
      zlink::framework::result_t<ensure_actor_res_t>::failure (
        zlink::framework::framework_error_kind_t::timeout, "EnsureActor not attempted");
    for (int attempt = 0; attempt < 80; ++attempt) {
        ensured = routes
                    .request (route_channel, zlink::routing_id_t::from (target_node),
                              ensure_actor_req_t{actor_id, display_name})
                    .packet_name ("EnsureActor")
                    .timeout (std::chrono::milliseconds (5000))
                    .async<ensure_actor_res_t> ()
                    .result ();
        if (ensured.has_value ()) {
            break;
        }
        std::this_thread::sleep_for (std::chrono::milliseconds (250));
    }
    if (!ensured.has_value ()) {
        throw std::runtime_error ("SM-G1 EnsureActor failed for " + actor_id + ": "
                                  + (ensured.error () ? ensured.error ()->what ()
                                                     : "unknown"));
    }
    return ensured.value ().actor;
}

inline zlink::stream_connector::connector_t sm_g1_make_stream_connector (
  const std::string &stream_endpoint)
{
    zlink::stream_connector::connector_options_t options;
    options.endpoint = stream_endpoint;
    options.connect_timeout = std::chrono::milliseconds (5000);
    options.request_timeout = std::chrono::milliseconds (5000);
    options.dispatch_mode = zlink::stream_connector::dispatch_mode_t::immediate;
    return zlink::stream_connector::connector_factory_t::create (options);
}

inline void sm_g1_write_signal_file (const std::string &path)
{
    if (path.empty ()) {
        throw std::runtime_error ("SM-G1 signal path is empty");
    }
    std::ofstream file (path);
    file << "ready\n";
}

inline void sm_g1_wait_signal_file (const std::string &path, const std::string &label)
{
    if (path.empty ()) {
        throw std::runtime_error ("SM-G1 " + label + " signal path is empty");
    }
    const auto deadline = std::chrono::steady_clock::now () + std::chrono::seconds (30);
    while (std::chrono::steady_clock::now () < deadline) {
        if (std::filesystem::exists (path)) {
            return;
        }
        std::this_thread::sleep_for (std::chrono::milliseconds (100));
    }
    throw std::runtime_error ("SM-G1 timed out waiting for " + label);
}

inline void run_sm_g1_crash_observation_scenario (zlink::framework::route_client_t &routes,
                                                  const std::string &stream_endpoint,
                                                  const std::string &crash_ready_file,
                                                  const std::string &crash_go_file,
                                                  const std::string &crash_observed_file)
{
    const auto play_a_actor_id = std::string ("crash-g1-play-a");
    const auto play_b_actor_id = std::string ("crash-g1-play-b");
    auto play_a_actor = sm_g1_ensure_actor_ref (routes, "play-a", play_a_actor_id,
                                                play_a_actor_id + "-display");
    auto play_b_actor = sm_g1_ensure_actor_ref (routes, "play-b", play_b_actor_id,
                                                play_b_actor_id + "-display");

    auto play_a_core = sm_g1_make_stream_connector (stream_endpoint);
    play_a_core.codecs ().add_json ();
    auto play_a_stream = zlink::stream_e2e_client::use (play_a_core);
    auto play_a_connected = play_a_stream.connect ().submit ();
    if (!static_cast<bool> (play_a_connected)) {
        throw std::runtime_error ("SM-G1 play-a stream connect failed");
    }

    auto play_a_auth =
      play_a_stream.request (stream_auth_req_t{"play-a", play_a_actor_id,
                                               play_a_actor_id + "-display", play_a_actor})
        .packet_name ("StreamAuthReq")
        .timeout (std::chrono::milliseconds (3000))
        .async<stream_auth_res_t> ()
        .result ();
    if (!static_cast<bool> (play_a_auth)) {
        throw std::runtime_error ("SM-G1 play-a auth failed: "
                                  + sm_g1_stream_error_text (play_a_auth));
    }

    bool play_a_joined = false;
    std::string play_a_join_error = "play-a join not attempted";
    for (int attempt = 0; attempt < 10; ++attempt) {
        auto play_a_join =
          play_a_stream.request (join_req_t{.key = "a-crash-g1",
                                            .actor_id = play_a_actor_id,
                                            .display_name = play_a_actor_id + "-display",
                                            .level = 301,
                                            .tags = {"stream", "SM-G1", "play-a"}})
            .packet_name ("JoinReq")
            .metadata ("actor-id", play_a_actor_id)
            .timeout (std::chrono::milliseconds (5000))
            .async<join_res_t> ()
            .result ();
        if (play_a_join) {
            play_a_joined = true;
            break;
        }
        play_a_join_error = sm_g1_stream_error_text (play_a_join);
        std::this_thread::sleep_for (std::chrono::milliseconds (300));
    }
    if (!play_a_joined) {
        throw std::runtime_error ("SM-G1 play-a join failed: " + play_a_join_error);
    }

    auto play_a_state = play_a_stream.request (state_req_t{.op = "add", .amount = 31})
                          .packet_name ("StateReq")
                          .metadata ("actor-id", play_a_actor_id)
                          .timeout (std::chrono::milliseconds (5000))
                          .async<state_res_t> ()
                          .result ();
    if (!static_cast<bool> (play_a_state) || play_a_state.value ().value != 31) {
        throw std::runtime_error ("SM-G1 play-a initial state mismatch");
    }

    auto play_b_auth =
      play_a_stream.request (stream_auth_req_t{"play-b", play_b_actor_id,
                                               play_b_actor_id + "-display", play_b_actor})
        .packet_name ("StreamAuthReq")
        .timeout (std::chrono::milliseconds (3000))
        .async<stream_auth_res_t> ()
        .result ();
    if (!static_cast<bool> (play_b_auth)) {
        throw std::runtime_error ("SM-G1 play-b auth failed: "
                                  + sm_g1_stream_error_text (play_b_auth));
    }

    bool play_b_joined = false;
    std::string play_b_join_error = "play-b join not attempted";
    for (int attempt = 0; attempt < 10; ++attempt) {
        auto play_b_join =
          play_a_stream.request (join_req_t{.key = "b-crash-g1",
                                            .actor_id = play_b_actor_id,
                                            .display_name = play_b_actor_id + "-display",
                                            .level = 302,
                                            .tags = {"stream", "SM-G1", "play-b"}})
            .packet_name ("JoinReq")
            .metadata ("actor-id", play_b_actor_id)
            .timeout (std::chrono::milliseconds (5000))
            .async<join_res_t> ()
            .result ();
        if (play_b_join) {
            play_b_joined = true;
            break;
        }
        play_b_join_error = sm_g1_stream_error_text (play_b_join);
        std::this_thread::sleep_for (std::chrono::milliseconds (300));
    }
    if (!play_b_joined) {
        throw std::runtime_error ("SM-G1 play-b join failed: " + play_b_join_error);
    }

    auto play_b_state = play_a_stream.request (state_req_t{.op = "add", .amount = 41})
                          .packet_name ("StateReq")
                          .metadata ("actor-id", play_b_actor_id)
                          .timeout (std::chrono::milliseconds (5000))
                          .async<state_res_t> ()
                          .result ();
    if (!static_cast<bool> (play_b_state) || play_b_state.value ().value != 41) {
        throw std::runtime_error ("SM-G1 play-b initial state mismatch");
    }

    sm_g1_write_signal_file (crash_ready_file);
    sm_g1_wait_signal_file (crash_go_file, "play-a crash");

    auto play_b_after_crash = play_a_stream.request (state_req_t{.op = "add", .amount = 1})
                                .packet_name ("StateReq")
                                .metadata ("actor-id", play_b_actor_id)
                                .timeout (std::chrono::milliseconds (5000))
                                .async<state_res_t> ()
                                .result ();
    if (!static_cast<bool> (play_b_after_crash)
        || play_b_after_crash.value ().value != 42) {
        throw std::runtime_error ("SM-G1 play-b state did not survive play-a crash");
    }

    auto failed_after_crash = play_a_stream.request (state_req_t{.op = "add", .amount = 1})
                                .packet_name ("StateReq")
                                .metadata ("actor-id", play_a_actor_id)
                                .timeout (std::chrono::milliseconds (3000))
                                .async<state_res_t> ()
                                .result ();
    if (static_cast<bool> (failed_after_crash)) {
        throw std::runtime_error ("SM-G1 play-a request unexpectedly succeeded after crash");
    }

    sm_g1_write_signal_file (crash_observed_file);

    (void) play_a_stream.close ().submit ();
    std::cout << "scenario SM-G1 crash observed passed\n";
}

inline void run_sm_g1_crash_recovery_scenario (zlink::framework::route_client_t &routes,
                                               const std::string &stream_endpoint)
{
    const auto recovered_actor_id = std::string ("crash-g1-play-b");

    auto recovered_actor = sm_g1_ensure_actor_ref (routes, "play-b", recovered_actor_id,
                                                   recovered_actor_id + "-display");
    auto recovered_core = sm_g1_make_stream_connector (stream_endpoint);
    recovered_core.codecs ().add_json ();
    auto recovered_stream = zlink::stream_e2e_client::use (recovered_core);
    auto recovered_connected = recovered_stream.connect ().submit ();
    if (!static_cast<bool> (recovered_connected)) {
        throw std::runtime_error ("SM-G1 recovered stream connect failed");
    }

    auto recovered_auth =
      recovered_stream.request (stream_auth_req_t{"play-b", recovered_actor_id,
                                                  recovered_actor_id + "-display",
                                                  recovered_actor})
        .packet_name ("StreamAuthReq")
        .timeout (std::chrono::milliseconds (3000))
        .async<stream_auth_res_t> ()
        .result ();
    if (!static_cast<bool> (recovered_auth)) {
        throw std::runtime_error ("SM-G1 recovered auth failed: "
                                  + sm_g1_stream_error_text (recovered_auth));
    }

    auto recovered_state = recovered_stream.request (state_req_t{.op = "add", .amount = 7})
                             .packet_name ("StateReq")
                             .timeout (std::chrono::milliseconds (5000))
                             .async<state_res_t> ()
                             .result ();
    if (!static_cast<bool> (recovered_state) || recovered_state.value ().value != 49) {
        throw std::runtime_error ("SM-G1 recovered state mismatch");
    }

    (void) recovered_stream.close ().submit ();
    std::cout << "scenario SM-G1 passed\n";
}

} // namespace zlink::framework::e2e::spot_service::client::scenarios
