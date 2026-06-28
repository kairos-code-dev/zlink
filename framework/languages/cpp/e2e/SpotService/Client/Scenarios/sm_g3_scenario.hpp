/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../Shared/spot_service_contracts.hpp"

#include <zlink/framework/codecs/json_stream_connector.hpp>
#include <zlink/http_client.hpp>
#include <zlink/stream_connector.hpp>

#include <array>
#include <chrono>
#include <future>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>

namespace zlink::framework::e2e::spot_service::client::scenarios
{

inline int sm_g3_count_evidence (const evidence_snapshot_t &snapshot,
                                 const std::string &marker,
                                 const std::string &actor_id,
                                 const std::string &spot_rid)
{
    int count = 0;
    for (const auto &entry : snapshot.entries) {
        if (entry.marker == marker && entry.actor_id == actor_id && entry.spot_rid == spot_rid) {
            ++count;
        }
    }
    return count;
}

inline evidence_snapshot_t fetch_sm_g3_evidence_until (zlink::http_client::client_t &client,
                                                       const std::array<std::string, 2> &actor_ids,
                                                       const std::string &spot_rid)
{
    for (int attempt = 0; attempt < 80; ++attempt) {
        auto raw = client.get ("/evidence").submit_raw ().result ();
        if (raw && raw.value ().status < 400) {
            auto snapshot =
              nlohmann::json::parse (raw.value ().body).get<evidence_snapshot_t> ();
            bool complete = true;
            for (const auto &actor_id : actor_ids) {
                complete = complete
                           && sm_g3_count_evidence (snapshot, "ActorJoined", actor_id, spot_rid)
                                == 1
                           && sm_g3_count_evidence (snapshot, "ActorLeft", actor_id, spot_rid)
                                == 1;
            }
            if (complete) {
                return snapshot;
            }
        }
        std::this_thread::sleep_for (std::chrono::milliseconds (100));
    }
    throw std::runtime_error ("SM-G3 expected actor join/leave evidence was not observed");
}

inline void run_sm_g3_actor_flow (const std::string &session_stream_endpoint,
                                  const std::string &spot_key,
                                  const std::string &actor_id)
{
    zlink::stream_connector::connector_options_t options;
    options.endpoint = session_stream_endpoint;
    options.connect_timeout = std::chrono::milliseconds (5000);
    options.request_timeout = std::chrono::milliseconds (5000);
    options.dispatch_mode = zlink::stream_connector::dispatch_mode_t::immediate;

    auto stream = zlink::stream_connector::connector_factory_t::create (options);
    stream.codecs ().add_json ();

    auto connected = stream.connect ();
    if (!connected) {
        throw std::runtime_error ("SM-G3 stream connect failed for " + actor_id);
    }

    std::optional<stream_auth_res_t> auth;
    std::string auth_error = "not attempted";
    for (int attempt = 0; attempt < 20; ++attempt) {
        auto auth_result =
          zlink::stream_connector::codecs::request (
            stream, stream_ensure_auth_req_t{"play-a", actor_id, actor_id + "-display"})
            .packet_name ("StreamEnsureAuthReq")
            .timeout (std::chrono::milliseconds (5000))
            .submit<stream_auth_res_t> ();
        if (auth_result) {
            auth = auth_result.value ();
            break;
        }
        auth_error =
          auth_result.error () ? auth_result.error ()->message : "unknown stream auth error";
        std::this_thread::sleep_for (std::chrono::milliseconds (150));
    }
    if (!auth || auth->actor.actor_id != actor_id || auth->session_node_rid != "session-a") {
        (void) stream.close ();
        throw std::runtime_error ("SM-G3 stream auth failed for " + actor_id + ": "
                                  + auth_error);
    }

    auto joined =
      zlink::stream_connector::codecs::request (
        stream, join_req_t{.key = spot_key,
                           .actor_id = actor_id,
                           .display_name = actor_id + "-display",
                           .level = 3,
                           .tags = {"stream", "SM-G3", "concurrent"}})
        .packet_name ("JoinReq")
        .metadata ("actor-id", actor_id)
        .timeout (std::chrono::milliseconds (5000))
        .submit<join_res_t> ();
    if (!joined || joined.value ().owner_node_rid != "play-a"
        || joined.value ().actor.actor_id != actor_id) {
        (void) stream.close ();
        throw std::runtime_error ("SM-G3 join failed for " + actor_id);
    }

    auto ping = zlink::stream_connector::codecs::request (stream, actor_ping_req_t{actor_id})
                  .packet_name ("ActorPingReq")
                  .metadata ("actor-id", actor_id)
                  .timeout (std::chrono::milliseconds (5000))
                  .submit<actor_ping_res_t> ();
    if (!ping || ping.value ().actor_id != actor_id || ping.value ().node_rid != "play-a") {
        (void) stream.close ();
        throw std::runtime_error ("SM-G3 actor request mismatch for " + actor_id);
    }

    auto left = zlink::stream_connector::codecs::request (stream, leave_req_t{"sm-g3"})
                  .packet_name ("LeaveReq")
                  .metadata ("actor-id", actor_id)
                  .timeout (std::chrono::milliseconds (5000))
                  .submit<leave_res_t> ();
    if (!left || !left.value ().left || left.value ().actor_id != actor_id) {
        (void) stream.close ();
        throw std::runtime_error ("SM-G3 leave reply mismatch for " + actor_id);
    }

    (void) stream.close ();
}

inline void run_sm_g3_scenario (const std::string &play_http_endpoint,
                                const std::string &session_stream_endpoint)
{
    if (play_http_endpoint.empty ()) {
        throw std::runtime_error ("ZLINK_CPP_E2E_PLAY_HTTP_ENDPOINT is required for SM-G3");
    }
    if (session_stream_endpoint.empty ()) {
        throw std::runtime_error ("ZLINK_CPP_E2E_STREAM_ENDPOINT is required for SM-G3");
    }

    constexpr auto spot_key = "sm-g3-concurrent";
    constexpr auto spot_rid = "user:play-a:sm-g3-concurrent";
    const std::array<std::string, 2> actor_ids{"actor-sm-g3-0", "actor-sm-g3-1"};

    auto first =
      std::async (std::launch::async,
                  [&] { run_sm_g3_actor_flow (session_stream_endpoint, spot_key, actor_ids[0]); });
    auto second =
      std::async (std::launch::async,
                  [&] { run_sm_g3_actor_flow (session_stream_endpoint, spot_key, actor_ids[1]); });

    first.get ();
    second.get ();

    auto play_a = zlink::http_client::client_t::create ()
                    .base_url (play_http_endpoint)
                    .json ()
                    .build ();
    const auto evidence = fetch_sm_g3_evidence_until (play_a, actor_ids, spot_rid);
    for (const auto &actor_id : actor_ids) {
        if (sm_g3_count_evidence (evidence, "ActorJoined", actor_id, spot_rid) != 1) {
            throw std::runtime_error ("SM-G3 join evidence count mismatch for " + actor_id);
        }
        if (sm_g3_count_evidence (evidence, "ActorLeft", actor_id, spot_rid) != 1) {
            throw std::runtime_error ("SM-G3 leave evidence count mismatch for " + actor_id);
        }
    }
}

} // namespace zlink::framework::e2e::spot_service::client::scenarios
