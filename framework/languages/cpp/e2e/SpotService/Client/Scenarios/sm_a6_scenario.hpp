/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../Shared/spot_service_contracts.hpp"

#include <zlink/http_client.hpp>

#include <stdexcept>
#include <string>

namespace zlink::framework::e2e::spot_service::client::scenarios
{

inline void ensure_http_success (const zlink::http_client::raw_http_response_t &response,
                                 const std::string &operation)
{
    if (response.status >= 400) {
        throw std::runtime_error (operation + " HTTP status " + std::to_string (response.status)
                                  + ": " + response.body);
    }
}

inline void run_sm_a6_scenario (const std::string &play_http_endpoint)
{
    if (play_http_endpoint.empty ()) {
        throw std::runtime_error ("ZLINK_CPP_E2E_PLAY_HTTP_ENDPOINT is required for SM-A6");
    }

    auto play_a = zlink::http_client::client_t::create ()
                    .base_url (play_http_endpoint)
                    .json ()
                    .build ();
    auto lifecycle =
      play_a.post ("/spot/lifecycle")
        .body (lifecycle_req_t{"sm-a6-life"})
        .submit_raw ()
        .result ();
    if (!lifecycle) {
        throw std::runtime_error (lifecycle.error () ? lifecycle.error ()->what ()
                                                     : "SM-A6 lifecycle HTTP failed");
    }
    ensure_http_success (lifecycle.value (), "SM-A6 lifecycle");
    const auto lifecycle_reply =
      nlohmann::json::parse (lifecycle.value ().body).get<lifecycle_res_t> ();
    if (lifecycle_reply.spot_rid != user_spot_rid_for_key ("sm-a6-life")
        || !lifecycle_reply.created || !lifecycle_reply.closed) {
        throw std::runtime_error ("SM-A6 lifecycle reply mismatch");
    }

    auto joined =
      play_a.post ("/spot/join")
        .body (join_req_t{.key = "sm-a6-busy",
                          .actor_id = "sm-a6-actor",
                          .display_name = "SM-A6",
                          .level = 6,
                          .tags = {"close-reject"}})
        .submit_raw ()
        .result ();
    if (!joined) {
        throw std::runtime_error (joined.error () ? joined.error ()->what ()
                                                  : "SM-A6 join HTTP failed");
    }
    ensure_http_success (joined.value (), "SM-A6 join");
    const auto join_reply = nlohmann::json::parse (joined.value ().body).get<join_res_t> ();
    if (join_reply.spot_rid != user_spot_rid_for_key ("sm-a6-busy")
        || join_reply.owner_node_rid != "play-a") {
        throw std::runtime_error ("SM-A6 busy spot join mismatch");
    }

    auto close_busy =
      play_a.post ("/spot/close")
        .body (close_spot_req_t{.key = "sm-a6-busy"})
        .submit_raw ()
        .result ();
    if (!close_busy) {
        throw std::runtime_error (close_busy.error () ? close_busy.error ()->what ()
                                                      : "SM-A6 close HTTP failed");
    }
    ensure_http_success (close_busy.value (), "SM-A6 close");
    const auto close_reply =
      nlohmann::json::parse (close_busy.value ().body).get<close_spot_res_t> ();
    if (close_reply.spot_rid != user_spot_rid_for_key ("sm-a6-busy") || close_reply.closed) {
        throw std::runtime_error ("SM-A6 joined actor close was not rejected");
    }
}

} // namespace zlink::framework::e2e::spot_service::client::scenarios
