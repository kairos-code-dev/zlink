/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../Shared/spot_service_contracts.hpp"

#include <zlink/http_client.hpp>

#include <stdexcept>
#include <string>

namespace zlink::framework::e2e::spot_service::client::scenarios
{

inline void run_sm_a1_scenario (const std::string &play_http_endpoint)
{
    if (play_http_endpoint.empty ()) {
        throw std::runtime_error ("ZLINK_CPP_E2E_PLAY_HTTP_ENDPOINT is required for SM-A1");
    }

    auto api = zlink::http_client::client_t::create ()
                 .base_url (play_http_endpoint)
                 .json ()
                 .build ();
    auto raw =
      api.post ("/spot/join")
        .body (join_req_t{.key = "a-room",
                          .actor_id = "alice",
                          .display_name = "Alice",
                          .level = 7,
                          .tags = {"alpha", "local"}})
        .submit_raw ()
        .result ();
    if (!raw) {
        throw std::runtime_error (raw.error () ? raw.error ()->what () : "SM-A1 HTTP failed");
    }
    if (raw.value ().status >= 400) {
        throw std::runtime_error ("SM-A1 HTTP status " + std::to_string (raw.value ().status)
                                  + ": " + raw.value ().body);
    }
    auto created = nlohmann::json::parse (raw.value ().body).get<join_res_t> ();

    if (created.spot_rid != "user:play-a:a-room") {
        throw std::runtime_error ("SM-A1 spot rid mismatch: " + created.spot_rid);
    }
    if (created.owner_node_rid != "play-a") {
        throw std::runtime_error ("SM-A1 owner mismatch: " + created.owner_node_rid);
    }
    if (created.actor_id != "alice" || created.display_name != "Alice" || created.level != 7) {
        throw std::runtime_error ("SM-A1 join payload mismatch");
    }
}

} // namespace zlink::framework::e2e::spot_service::client::scenarios
