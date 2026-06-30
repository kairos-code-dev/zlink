/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../Shared/spot_service_contracts.hpp"

#include <zlink/framework/codecs/json_stream_connector.hpp>
#include <zlink/stream_connector.hpp>

#include <atomic>
#include <chrono>
#include <stdexcept>
#include <string>
#include <thread>

namespace zlink::framework::e2e::spot_service::client::scenarios
{

inline zlink::stream_connector::connector_t sm_d10_connect_stream (
  const std::string &endpoint,
  std::size_t max_received_messages,
  std::atomic_bool *drop_seen = nullptr)
{
    zlink::stream_connector::connector_options_t options;
    options.endpoint = endpoint;
    options.connect_timeout = std::chrono::milliseconds (5000);
    options.request_timeout = std::chrono::milliseconds (5000);
    options.dispatch_mode = zlink::stream_connector::dispatch_mode_t::immediate;
    options.max_received_messages = max_received_messages;

    auto stream = zlink::stream_connector::connector_factory_t::create (options);
    if (drop_seen != nullptr) {
        stream.on_error ([drop_seen] (const zlink::stream_connector::error_t &error) {
            if (error.code == zlink::stream_connector::error_code_t::received_message_dropped) {
                drop_seen->store (true);
            }
        });
    }
    auto connected = stream.connect ();
    if (!connected) {
        throw std::runtime_error ("SM-D10 stream connect failed");
    }
    return stream;
}

inline void sm_d10_auth (zlink::stream_connector::connector_t &stream,
                         const std::string &target_node,
                         const std::string &actor_id,
                         const std::string &display_name)
{
    auto auth =
      stream.request (stream_ensure_auth_req_t{target_node, actor_id, display_name})
        .packet_name ("StreamEnsureAuthReq")
        .timeout (std::chrono::milliseconds (5000))
        .submit<stream_auth_res_t> ();
    if (!auth || auth.value ().actor.actor_id != actor_id
        || auth.value ().session_node_rid.empty ()) {
        throw std::runtime_error ("SM-D10 auth failed for " + actor_id);
    }
}

inline void run_sm_d10_scenario (const std::string &session_a_stream_endpoint,
                                 const std::string &session_b_stream_endpoint)
{
    if (session_a_stream_endpoint.empty () || session_b_stream_endpoint.empty ()) {
        throw std::runtime_error (
          "ZLINK_CPP_E2E_STREAM_ENDPOINT and ZLINK_CPP_E2E_ALT_STREAM_ENDPOINT are required for SM-D10");
    }

    constexpr auto congested_actor = "actor-sm-d10-congested";
    constexpr auto isolated_actor = "actor-sm-d10-isolated";
    std::atomic_bool congested_drop_seen{false};

    auto congested = sm_d10_connect_stream (session_a_stream_endpoint, 1, &congested_drop_seen);
    sm_d10_auth (congested, "play-a", congested_actor, "SM-D10 Congested");

    auto isolated = sm_d10_connect_stream (session_b_stream_endpoint, 1024);
    sm_d10_auth (isolated, "play-b", isolated_actor, "SM-D10 Isolated");

    for (int index = 0; index < 8; ++index) {
        const auto value = "burst-" + std::to_string (index);
        auto reply = congested.request (actor_push_req_t{value})
                       .packet_name ("PushReq")
                       .metadata ("actor-id", congested_actor)
                       .timeout (std::chrono::milliseconds (5000))
                       .submit<actor_push_res_t> ();
        if (!reply || !reply.value ().pushed || reply.value ().actor_id != congested_actor) {
            throw std::runtime_error ("SM-D10 congested push reply mismatch");
        }
    }

    const auto drop_deadline = std::chrono::steady_clock::now () + std::chrono::seconds (2);
    while (!congested_drop_seen.load () && std::chrono::steady_clock::now () < drop_deadline) {
        std::this_thread::sleep_for (std::chrono::milliseconds (10));
    }
    if (!congested_drop_seen.load ()) {
        throw std::runtime_error ("SM-D10 expected received_message_dropped callback");
    }
    if (congested.pending_dispatch_count () > 1) {
        throw std::runtime_error ("SM-D10 congested receive queue exceeded configured bound");
    }

    auto retained = congested.wait_for<actor_push_notify_t> (std::chrono::milliseconds (2000))
                      .submit ();
    if (!retained || retained.value ().actor_id != congested_actor
        || retained.value ().value.rfind ("burst-", 0) != 0) {
        throw std::runtime_error ("SM-D10 retained congested push notify mismatch");
    }

    auto after_backpressure = congested.request (actor_ping_req_t{"after-backpressure"})
                                .packet_name ("ActorPingReq")
                                .metadata ("actor-id", congested_actor)
                                .timeout (std::chrono::milliseconds (5000))
                                .submit<actor_ping_res_t> ();
    if (!after_backpressure || after_backpressure.value ().actor_id != congested_actor
        || after_backpressure.value ().value != "after-backpressure") {
        throw std::runtime_error ("SM-D10 congested session stopped routing");
    }

    auto isolated_wait =
      isolated.wait_for<actor_push_notify_t> (std::chrono::milliseconds (10000))
        .to_future ("SM-D10 isolated push notify missing");
    auto isolated_reply = isolated.request (actor_push_req_t{"isolated-push"})
                            .packet_name ("PushReq")
                            .metadata ("actor-id", isolated_actor)
                            .timeout (std::chrono::milliseconds (5000))
                            .submit<actor_push_res_t> ();
    if (!isolated_reply || !isolated_reply.value ().pushed
        || isolated_reply.value ().actor_id != isolated_actor) {
        throw std::runtime_error ("SM-D10 isolated push reply mismatch");
    }
    const auto isolated_notify = isolated_wait.get ();
    if (isolated_notify.actor_id != isolated_actor
        || isolated_notify.value != "isolated-push") {
        throw std::runtime_error ("SM-D10 isolated push notify mismatch");
    }

    (void) congested.close ();
    (void) isolated.close ();
}

} // namespace zlink::framework::e2e::spot_service::client::scenarios
