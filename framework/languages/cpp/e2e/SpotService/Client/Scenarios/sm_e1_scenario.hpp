/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../Shared/spot_service_contracts.hpp"

#include <zlink/http_client.hpp>

#include <stdexcept>
#include <string>

namespace zlink::framework::e2e::spot_service::client::scenarios
{

inline void run_sm_e1_scenario (const std::string &play_http_endpoint,
                                const std::string &play_b_http_endpoint)
{
    if (play_http_endpoint.empty ()) {
        throw std::runtime_error ("ZLINK_CPP_E2E_PLAY_HTTP_ENDPOINT is required for SM-E1");
    }
    if (play_b_http_endpoint.empty ()) {
        throw std::runtime_error ("ZLINK_CPP_E2E_PLAY_B_HTTP_ENDPOINT is required for SM-E1");
    }

    auto play_a = zlink::http_client::client_t::create ()
                    .base_url (play_http_endpoint)
                    .json ()
                    .build ();
    auto play_b = zlink::http_client::client_t::create ()
                    .base_url (play_b_http_endpoint)
                    .json ()
                    .build ();
    constexpr auto spot_rid = "user:play-b:sm-e1-missing";
    auto created =
      play_b.post ("/spot/create")
        .body (create_spot_req_t{.spot_rid = spot_rid})
        .submit_raw ()
        .result ();
    if (!created) {
        throw std::runtime_error (created.error () ? created.error ()->what ()
                                                   : "SM-E1 create spot HTTP failed");
    }
    if (created.value ().status >= 400) {
        throw std::runtime_error ("SM-E1 create spot HTTP status "
                                  + std::to_string (created.value ().status) + ": "
                                  + created.value ().body);
    }
    const auto create_reply = nlohmann::json::parse (created.value ().body).get<create_spot_res_t> ();
    if (!create_reply.created || create_reply.spot_rid != spot_rid
        || create_reply.owner_node_rid != "play-b") {
        throw std::runtime_error ("SM-E1 create spot reply mismatch");
    }

    auto missing =
      play_a.post ("/spot/missing-route")
        .body (spot_missing_route_req_t{.target_node_rid = "play-b",
                                        .spot_rid = spot_rid,
                                        .value = "missing-route"})
        .submit_raw ()
        .result ();
    if (!missing) {
        throw std::runtime_error (missing.error () ? missing.error ()->what ()
                                                   : "SM-E1 missing route HTTP failed");
    }
    if (missing.value ().status >= 400) {
        throw std::runtime_error ("SM-E1 missing route HTTP status "
                                  + std::to_string (missing.value ().status) + ": "
                                  + missing.value ().body);
    }
    const auto missing_reply =
      nlohmann::json::parse (missing.value ().body).get<spot_missing_route_res_t> ();
    if (!missing_reply.request_failed) {
        throw std::runtime_error ("SM-E1 missing route reply mismatch");
    }
}

} // namespace zlink::framework::e2e::spot_service::client::scenarios
