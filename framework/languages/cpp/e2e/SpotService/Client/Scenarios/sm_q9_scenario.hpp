/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../Shared/spot_service_contracts.hpp"

#include <zlink/framework.hpp>
#include <zlink/http_client.hpp>

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace zlink::framework::e2e::spot_service::client::scenarios
{

inline multi_node_create_spot_res_t post_sm_q9_create (
  zlink::http_client::client_t &client,
  const multi_node_create_spot_req_t &request,
  const std::string &label)
{
    auto raw = client.post ("/spot/create-local").body (request).submit_raw ().result ();
    if (!raw || raw.value ().status >= 400) {
        const auto error = raw ? raw.value ().body
                               : (raw.error () ? raw.error ()->what () : "HTTP failed");
        throw std::runtime_error ("SM-Q9 create failed for " + label + ": " + error);
    }
    return nlohmann::json::parse (raw.value ().body).get<multi_node_create_spot_res_t> ();
}

inline state_res_t request_sm_q9_state (zlink::framework::route_client_t &routes,
                                        const std::string &route_channel,
                                        const std::string &target_node_rid,
                                        const std::string &spot_rid,
                                        int delta,
                                        const std::string &label)
{
    std::string last_error = label + " was not attempted";
    for (int attempt = 0; attempt < 40; ++attempt) {
        auto reply = routes
                       .request (route_channel, zlink::routing_id_t::from (target_node_rid),
                                 zlink::framework::spot_rid_t::from_string (spot_rid),
                                 state_req_t{.op = "add", .amount = delta})
                       .packet_name ("StateReq")
                       .timeout (std::chrono::milliseconds (2000))
                       .async<state_res_t> ()
                       .result ();
        if (reply.has_value ()) {
            return reply.value ();
        }
        last_error = reply.error () ? reply.error ()->what () : "unknown route error";
        std::this_thread::sleep_for (std::chrono::milliseconds (100));
    }
    throw std::runtime_error ("SM-Q9 state request failed for " + label + ": " + last_error);
}

inline evidence_snapshot_t fetch_sm_q9_evidence (zlink::http_client::client_t &client,
                                                 const std::string &label)
{
    auto raw = client.get ("/evidence").submit_raw ().result ();
    if (!raw || raw.value ().status >= 400) {
        const auto error = raw ? raw.value ().body
                               : (raw.error () ? raw.error ()->what () : "HTTP failed");
        throw std::runtime_error ("SM-Q9 evidence fetch failed for " + label + ": " + error);
    }
    return nlohmann::json::parse (raw.value ().body).get<evidence_snapshot_t> ();
}

inline int count_sm_q9_state_evidence (const evidence_snapshot_t &snapshot,
                                       const std::string &spot_rid,
                                       const std::string &value)
{
    int count = 0;
    for (const auto &entry : snapshot.entries) {
        if (entry.marker == "MultiStateRequest" && entry.spot_rid == spot_rid
            && entry.value == value) {
            ++count;
        }
    }
    return count;
}

inline void run_sm_q9_scenario (zlink::framework::route_client_t &routes,
                                const std::string &multi_a_http_endpoint,
                                const std::string &multi_b_http_endpoint)
{
    if (multi_a_http_endpoint.empty () || multi_b_http_endpoint.empty ()) {
        throw std::runtime_error ("SM-Q9 requires both multi-node HTTP endpoints");
    }

    auto multi_a = zlink::http_client::client_t::create ()
                     .base_url (multi_a_http_endpoint)
                     .build ();
    auto multi_b = zlink::http_client::client_t::create ()
                     .base_url (multi_b_http_endpoint)
                     .build ();

    constexpr auto spot_a = "spot-sm-q9-a-cpp";
    constexpr auto spot_b = "spot-sm-q9-b-cpp";

    const auto created_a =
      post_sm_q9_create (multi_a, multi_node_create_spot_req_t{.spot_rid = spot_a},
                         "node A");
    const auto first_a = request_sm_q9_state (routes, multi_route_channel_a, "multi-a", spot_a,
                                              11, "node A first");
    const auto direct_a = request_sm_q9_state (routes, multi_route_channel_a, "multi-a", spot_a,
                                               0, "node A direct");
    const auto evidence_a = fetch_sm_q9_evidence (multi_a, "node A");

    if (created_a.node_rid != "multi-a") {
        throw std::runtime_error ("SM-Q9 node A create reply node mismatch");
    }
    if (first_a.value != 11 || direct_a.spot_rid != spot_a
        || direct_a.owner_node_rid != "multi-a" || direct_a.value != 11) {
        throw std::runtime_error ("SM-Q9 node A route-to-spot reply mismatch");
    }
    if (count_sm_q9_state_evidence (evidence_a, spot_a, "11") < 2) {
        throw std::runtime_error ("SM-Q9 node A did not record both route-to-spot requests");
    }

    const auto created_b =
      post_sm_q9_create (multi_b, multi_node_create_spot_req_t{.spot_rid = spot_b},
                         "node B");
    const auto first_b = request_sm_q9_state (routes, multi_route_channel_b, "multi-b", spot_b,
                                              17, "node B first");
    const auto direct_b = request_sm_q9_state (routes, multi_route_channel_b, "multi-b", spot_b,
                                               0, "node B direct");
    const auto evidence_b = fetch_sm_q9_evidence (multi_b, "node B");

    if (created_b.node_rid != "multi-b") {
        throw std::runtime_error ("SM-Q9 node B create reply node mismatch");
    }
    if (first_b.value != 17 || direct_b.spot_rid != spot_b
        || direct_b.owner_node_rid != "multi-b" || direct_b.value != 17) {
        throw std::runtime_error ("SM-Q9 node B route-to-spot reply mismatch");
    }
    if (count_sm_q9_state_evidence (evidence_b, spot_b, "17") < 2) {
        throw std::runtime_error ("SM-Q9 node B did not record both route-to-spot requests");
    }

    std::cout << "operation SpotService.sm-q9 passed\n";
}

} // namespace zlink::framework::e2e::spot_service::client::scenarios
