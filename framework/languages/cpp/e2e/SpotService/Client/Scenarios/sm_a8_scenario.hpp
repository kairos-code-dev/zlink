/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../Shared/spot_service_contracts.hpp"

#include <zlink/http_client.hpp>

#include <chrono>
#include <future>
#include <stdexcept>
#include <string>
#include <thread>

namespace zlink::framework::e2e::spot_service::client::scenarios
{

inline state_res_t post_state (zlink::http_client::client_t &api,
                               const std::string &actor_id,
                               const state_req_t &state,
                               const std::string &label)
{
    auto raw = api.post ("/spot/state")
                 .body (spot_state_req_t{.actor_id = actor_id, .state = state})
                 .submit_raw ()
                 .result ();
    if (!raw) {
        throw std::runtime_error (raw.error () ? raw.error ()->what () : label + " HTTP failed");
    }
    if (raw.value ().status >= 400) {
        throw std::runtime_error (label + " HTTP status " + std::to_string (raw.value ().status)
                                  + ": " + raw.value ().body);
    }
    return nlohmann::json::parse (raw.value ().body).get<state_res_t> ();
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

inline void run_sm_a8_scenario (const std::string &play_http_endpoint,
                                const std::string &play_b_http_endpoint)
{
    if (play_http_endpoint.empty () || play_b_http_endpoint.empty ()) {
        throw std::runtime_error (
          "ZLINK_CPP_E2E_PLAY_HTTP_ENDPOINT and ZLINK_CPP_E2E_PLAY_B_HTTP_ENDPOINT are required "
          "for SM-A8");
    }

    constexpr auto actor_id = "sm-a8-actor";
    auto play_a = zlink::http_client::client_t::create ()
                    .base_url (play_http_endpoint)
                    .json ()
                    .build ();
    auto play_b = zlink::http_client::client_t::create ()
                    .base_url (play_b_http_endpoint)
                    .json ()
                    .build ();
    auto joined =
      play_a.post ("/spot/join")
        .body (join_req_t{.key = "sm-a8-worker",
                          .actor_id = actor_id,
                          .display_name = "SM-A8",
                          .level = 8,
                          .tags = {"worker"}})
        .submit_raw ()
        .result ();
    if (!joined) {
        throw std::runtime_error (joined.error () ? joined.error ()->what ()
                                                  : "SM-A8 join HTTP failed");
    }
    if (joined.value ().status >= 400) {
        throw std::runtime_error ("SM-A8 join HTTP status "
                                  + std::to_string (joined.value ().status) + ": "
                                  + joined.value ().body);
    }
    const auto created = nlohmann::json::parse (joined.value ().body).get<join_res_t> ();
    if (created.spot_rid != "user:play-a:sm-a8-worker") {
        throw std::runtime_error ("SM-A8 spot rid mismatch: " + created.spot_rid);
    }

    const auto seeded =
      post_state (play_a, actor_id, state_req_t{.op = "set", .amount = 7}, "SM-A8 seed state");
    if (seeded.value != 7 || seeded.sequence != 1) {
        throw std::runtime_error ("SM-A8 seed state mismatch");
    }

    auto worker_future = std::async (std::launch::async, [play_http_endpoint] {
        auto client = zlink::http_client::client_t::create ()
                        .base_url (play_http_endpoint)
                        .json ()
                        .build ();
        auto raw =
          client.post ("/spot/worker/start")
            .body (spot_worker_req_t{.actor_id = actor_id,
                                     .worker = worker_req_t{.delta = 13, .delay_ms = 600}})
            .submit_raw ()
            .result ();
        if (!raw) {
            throw std::runtime_error (raw.error () ? raw.error ()->what ()
                                                   : "SM-A8 worker HTTP failed");
        }
        if (raw.value ().status >= 400) {
            throw std::runtime_error ("SM-A8 worker HTTP status "
                                      + std::to_string (raw.value ().status) + ": "
                                      + raw.value ().body);
        }
        return nlohmann::json::parse (raw.value ().body).get<worker_res_t> ();
    });

    std::this_thread::sleep_for (std::chrono::milliseconds (200));
    const auto interleaved_started = std::chrono::steady_clock::now ();
    const auto interleaved = post_routed_state (
      play_b,
      spot_state_route_req_t{.key = "sm-a8-worker",
                             .state = state_req_t{.op = "add", .amount = 5}},
      "SM-A8 interleaved routed state");
    const auto interleaved_elapsed = std::chrono::steady_clock::now () - interleaved_started;
    if (interleaved_elapsed >= std::chrono::milliseconds (350)) {
        throw std::runtime_error ("SM-A8 same-spot request was blocked by worker");
    }
    if (interleaved.value != 12 || interleaved.sequence != 2) {
        throw std::runtime_error ("SM-A8 interleaved state mismatch");
    }

    const auto worker = worker_future.get ();
    if (worker.snapshot != 7 || worker.worker_result != 20 || worker.final_value != 25
        || worker.sequence != 3) {
        throw std::runtime_error ("SM-A8 worker result mismatch");
    }
}

} // namespace zlink::framework::e2e::spot_service::client::scenarios
