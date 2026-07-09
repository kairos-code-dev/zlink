/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../Shared/spot_service_contracts.hpp"

#include <zlink/http_client.hpp>

#include <chrono>
#include <stdexcept>
#include <string>
#include <thread>

namespace zlink::framework::e2e::spot_service::client::scenarios
{

inline void run_sm_c5_scenario (const std::string &play_http_endpoint,
                                const std::string &play_b_http_endpoint)
{
    if (play_http_endpoint.empty () || play_b_http_endpoint.empty ()) {
        throw std::runtime_error ("SM-C5 requires play-a and play-b HTTP endpoints");
    }
    auto play_a = zlink::http_client::client_t::create ().base_url (play_http_endpoint).build ();
    auto play_b = zlink::http_client::client_t::create ().base_url (play_b_http_endpoint).build ();
    constexpr auto source_spot_rid = "spot-sm-c5-source-cpp";
    constexpr auto target_spot_rid = "spot-sm-c5-target-cpp";
    constexpr auto marker = "sm-c5-cpp";

    auto source =
      play_a.post ("/spot/create")
        .body (create_spot_req_t{.spot_rid = source_spot_rid})
        .submit_raw ()
        .result ();
    auto target =
      play_b.post ("/spot/create")
        .body (create_spot_req_t{.spot_rid = target_spot_rid})
        .submit_raw ()
        .result ();
    if (!source || source.value ().status >= 400 || !target || target.value ().status >= 400) {
        throw std::runtime_error ("SM-C5 spot create failed");
    }

    const auto deadline = std::chrono::steady_clock::now () + std::chrono::seconds (10);
    while (std::chrono::steady_clock::now () < deadline) {
        auto routed =
          play_b.post ("/spot/to-spot/request-cross")
            .body (spot_to_spot_route_req_t{.source_node_rid = "play-a",
                                            .source_spot_rid = source_spot_rid,
                                            .target_node_rid = "play-b",
                                            .target_spot_rid = target_spot_rid,
                                            .marker = marker})
            .submit_raw ()
            .result ();
        if (!routed || routed.value ().status >= 400) {
            std::this_thread::sleep_for (std::chrono::milliseconds (100));
            continue;
        }

        auto observed =
          play_b.post ("/evidence/wait")
            .body (evidence_wait_req_t{.contains_all = {target_spot_rid,
                                                        "sm-c3-publish-" + std::string (marker)},
                                       .timeout_milliseconds = 1000})
            .submit_raw ()
            .result ();
        if (observed && observed.value ().status < 400) {
            return;
        }
        std::this_thread::sleep_for (std::chrono::milliseconds (100));
    }
    throw std::runtime_error ("SM-C5 cross-node SpotMesh publish evidence missing");
}

} // namespace zlink::framework::e2e::spot_service::client::scenarios
