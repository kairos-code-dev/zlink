/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../Shared/yield_dispatch_contracts.hpp"
#include "../Support/scenario_assert.hpp"

#include <chrono>
#include <iostream>
#include <string>
#include <thread>

namespace zlink::framework::e2e::yield_dispatch::client
{

template <typename TConnector>
std::string run_yd_d3_route_bridge_yield_scenario (TConnector &connector)
{
    const auto request_id = unique_id ("YD-D3");
    const auto spot_rid = unique_id ("yield-route-bridge");
    auto spot =
      connector.request (ensure_spot_req_t{.spot_rid = spot_rid})
        .packet_name (ensure_spot_req_t::packet_name)
        .metadata (target_node_rid_metadata, "play-b")
        .timeout (std::chrono::milliseconds (30000))
        .template submit<ensure_spot_res_t> ();
    ensure (static_cast<bool> (spot), "YD-D3 ensure spot failed");
    ensure (spot.value ().spot_rid == spot_rid, "YD-D3 ensure spot reply mismatch");

    connector.send (yield_msg_t{.request_id = request_id,
                                .delay_ms = 250,
                                .correlation_id = "route-bridge"})
        .packet_name (yield_msg_t::packet_name)
        .metadata (spot_rid_metadata, spot_rid)
        .metadata (target_node_rid_metadata, "play-b")
        .submit ();
    std::this_thread::sleep_for (std::chrono::milliseconds (75));

    connector.send (probe_msg_t{.request_id = request_id, .marker = "route-bridge-probe"})
        .packet_name (probe_msg_t::packet_name)
        .metadata (spot_rid_metadata, spot_rid)
        .metadata (target_node_rid_metadata, "play-b")
        .submit ();

    auto evidence =
      connector.request (
                yield_evidence_wait_req_t{.request_id = request_id,
                                          .marker = "yield-completed",
                                          .timeout_milliseconds = 20000})
        .packet_name (yield_evidence_wait_req_t::packet_name)
        .metadata (target_node_rid_metadata, "play-b")
        .timeout (std::chrono::milliseconds (30000))
        .template submit<yield_evidence_res_t> ();
    ensure (static_cast<bool> (evidence), "YD-D3 evidence wait failed");
    ensure (contains_in_order (evidence.value ().evidence, request_id,
                               {"yield-started", "yield-released", "probe-started",
                                "probe-completed", "yield-resumed", "yield-completed"}),
            "YD-D3 marker order mismatch");

    bool saw_target_spot = false;
    for (const auto &line : evidence.value ().evidence) {
        if (line.find ("yield-started|rid=play-b") != std::string::npos
            && line.find ("spot=" + spot_rid) != std::string::npos
            && line.find ("handler=spot") != std::string::npos) {
            saw_target_spot = true;
        }
    }
    ensure (saw_target_spot, "YD-D3 target Spot handler marker missing");
    std::cout << "scenario YD-D3 passed\n";
    return request_id;
}

} // namespace zlink::framework::e2e::yield_dispatch::client
