/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../Shared/spot_service_contracts.hpp"

#include <zlink/http_client.hpp>

#include <stdexcept>
#include <string>

namespace zlink::framework::e2e::spot_service::client::scenarios
{

template <typename TReply, typename TRequest>
inline TReply post_json (zlink::http_client::client_t &api,
                         const std::string &path,
                         const TRequest &request,
                         const std::string &label)
{
    auto raw = api.post (path).body (request).submit_raw ().result ();
    if (!raw) {
        throw std::runtime_error (raw.error () ? raw.error ()->what () : label + " HTTP failed");
    }
    if (raw.value ().status >= 400) {
        throw std::runtime_error (label + " HTTP status " + std::to_string (raw.value ().status)
                                  + ": " + raw.value ().body);
    }
    return nlohmann::json::parse (raw.value ().body).template get<TReply> ();
}

inline state_res_t post_routed_state (zlink::http_client::client_t &api,
                                      const spot_state_route_req_t &request,
                                      const std::string &label)
{
    auto raw = api.post ("/spot/state/request").body (request).submit_raw ().result ();
    if (!raw) {
        throw std::runtime_error (raw.error () ? raw.error ()->what () : label + " HTTP failed");
    }
    if (raw.value ().status >= 400) {
        throw std::runtime_error (label + " HTTP status " + std::to_string (raw.value ().status)
                                  + ": " + raw.value ().body);
    }
    return nlohmann::json::parse (raw.value ().body).get<state_res_t> ();
}

inline int find_evidence_index (const evidence_snapshot_t &snapshot,
                                const std::string &marker,
                                const std::string &spot_rid,
                                const std::string &value)
{
    for (std::size_t index = 0; index < snapshot.entries.size (); ++index) {
        const auto &entry = snapshot.entries[index];
        if (entry.marker == marker && entry.spot_rid == spot_rid && entry.value == value) {
            return static_cast<int> (index);
        }
    }
    return -1;
}

inline void run_sm_a8_scenario (const std::string &play_http_endpoint,
                                const std::string &play_b_http_endpoint)
{
    if (play_http_endpoint.empty ()) {
        throw std::runtime_error ("ZLINK_CPP_E2E_PLAY_HTTP_ENDPOINT is required for SM-A8");
    }
    if (play_b_http_endpoint.empty ()) {
        throw std::runtime_error ("ZLINK_CPP_E2E_PLAY_B_HTTP_ENDPOINT is required for SM-A8");
    }

    auto play_a = zlink::http_client::client_t::create ()
                    .base_url (play_http_endpoint)
                    .json ()
                    .build ();
    auto play_b = zlink::http_client::client_t::create ()
                    .base_url (play_b_http_endpoint)
                    .json ()
                    .build ();

    constexpr auto spot_key = "sm-a8-worker";
    const auto spot_rid = user_spot_rid_for_key (spot_key);
    constexpr auto marker = "sm-a8-worker";
    const auto joined = post_json<join_res_t> (
      play_a, "/spot/join",
      join_req_t{.key = spot_key,
                 .actor_id = "sm-a8-actor",
                 .display_name = "SM-A8",
                 .level = 8,
                 .tags = {"worker-routing"}},
      "SM-A8 join spot");
    if (joined.spot_rid != spot_rid || joined.owner_node_rid != "play-a") {
        throw std::runtime_error ("SM-A8 worker spot was not joined on play-a");
    }

    const auto ready = post_routed_state (
      play_b,
      spot_state_route_req_t{.target_node_rid = "play-a",
                             .spot_rid = spot_rid,
                             .state = state_req_t{.op = "add", .amount = 0}},
      "SM-A8 ready state");
    if (ready.spot_rid != spot_rid || ready.owner_node_rid != "play-a") {
        throw std::runtime_error ("SM-A8 worker spot route did not become ready");
    }

    const auto worker = post_json<spot_worker_start_res_t> (
      play_b, "/spot/worker/start",
      spot_worker_start_req_t{.spot_rid = spot_rid, .marker = marker, .delay_ms = 600},
      "SM-A8 worker start");
    if (worker.spot_rid != spot_rid || worker.owner_node_rid != "play-a"
        || worker.marker != marker) {
        throw std::runtime_error ("SM-A8 worker start reply mismatch");
    }

    const auto during = post_routed_state (
      play_b,
      spot_state_route_req_t{.target_node_rid = "play-a",
                             .spot_rid = spot_rid,
                             .state = state_req_t{.op = "add", .amount = 1}},
      "SM-A8 concurrent state");
    if (during.value != 1) {
        throw std::runtime_error ("SM-A8 same-spot request was blocked by worker");
    }

    const auto completed = post_json<spot_worker_complete_res_t> (
      play_a, "/spot/worker/complete",
      spot_worker_complete_req_t{.spot_rid = spot_rid, .marker = marker},
      "SM-A8 worker complete");
    if (!completed.completed || completed.spot_rid != spot_rid || completed.marker != marker) {
        throw std::runtime_error ("SM-A8 worker completion reply mismatch");
    }

    const auto worker_start_index =
      find_evidence_index (completed.evidence, "WorkerStarted", spot_rid, marker);
    const auto state_index = find_evidence_index (completed.evidence, "StateRouted", spot_rid, "1");
    const auto worker_complete_index =
      find_evidence_index (completed.evidence, "WorkerCompleted", spot_rid, marker);
    if (worker_start_index < 0 || state_index <= worker_start_index
        || worker_complete_index <= state_index) {
        throw std::runtime_error ("SM-A8 worker evidence order mismatch");
    }
}

} // namespace zlink::framework::e2e::spot_service::client::scenarios
