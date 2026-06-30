/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../Shared/yield_dispatch_contracts.hpp"
#include "../Support/evidence_wait.hpp"
#include "../Support/scenario_assert.hpp"

#include <chrono>
#include <iostream>
#include <string>

namespace zlink::framework::e2e::yield_dispatch::client
{

template <typename TConnector>
std::string run_yd_d2_remote_spot_yield_scenario (TConnector &connector)
{
    const auto owner_spot_rid = unique_id ("yield-remote-owner");
    const auto target_spot_rid = unique_id ("yield-remote-target");
    auto owner =
      connector.request (ensure_spot_req_t{.spot_rid = owner_spot_rid})
        .packet_name (ensure_spot_req_t::packet_name)
        .timeout (std::chrono::milliseconds (15000))
        .template submit<ensure_spot_res_t> ();
    ensure (static_cast<bool> (owner), "YD-D2 owner spot ensure failed");
    auto target =
      connector.request (ensure_spot_req_t{.spot_rid = target_spot_rid})
        .packet_name (ensure_spot_req_t::packet_name)
        .metadata (target_node_rid_metadata, "play-b")
        .timeout (std::chrono::milliseconds (15000))
        .template submit<ensure_spot_res_t> ();
    ensure (static_cast<bool> (target), "YD-D2 target spot ensure failed");
    const auto request_id = unique_id ("YD-D2");
    auto remote_reply =
      connector.request (remote_spot_yield_req_t{.request_id = request_id,
                                                 .target_spot_rid = target_spot_rid,
                                                 .delay_ms = 350})
        .packet_name (remote_spot_yield_req_t::packet_name)
        .metadata (spot_rid_metadata, owner_spot_rid)
        .timeout (std::chrono::milliseconds (30000))
        .template submit<yield_dispatch_res_t> ();
    ensure (static_cast<bool> (remote_reply), "YD-D2 RemoteSpotYieldReq failed");
    ensure (remote_reply.value ().scenario_id == "YD-D2",
            "YD-D2 reply scenario mismatch");
    ensure (remote_reply.value ().node_rid == "play-a",
            "YD-D2 caller continuation node mismatch");
    auto owner_evidence =
      wait_for_evidence_snapshot (connector, request_id, "remote-yield-completed", "play-a",
                                  std::chrono::milliseconds (20000));
    ensure (static_cast<bool> (owner_evidence), "YD-D2 owner evidence wait failed");
    ensure_contains_in_order (owner_evidence.value ().evidence, request_id,
                              {"remote-yield-started",
                               "remote-yield-released",
                               "remote-yield-resumed",
                               "remote-yield-completed"},
                              "YD-D2 owner marker order mismatch");
    ensure (every_request_line_has (owner_evidence.value ().evidence, request_id,
                                    {"rid=play-a"}),
            "YD-D2 owner evidence node mismatch");
    bool saw_target_node = false;
    for (const auto &line : owner_evidence.value ().evidence) {
        if (line.find ("remote-yield-resumed") != std::string::npos
            && line.find ("targetNode=play-b") != std::string::npos) {
            saw_target_node = true;
        }
    }
    ensure (saw_target_node, "YD-D2 target node marker missing");
    auto target_evidence =
      connector.request (yield_evidence_req_t{.request_id = request_id})
        .packet_name (yield_evidence_req_t::packet_name)
        .metadata (target_node_rid_metadata, "play-b")
        .timeout (std::chrono::milliseconds (30000))
        .template submit<yield_evidence_res_t> ();
    ensure (static_cast<bool> (target_evidence), "YD-D2 target evidence request failed");
    bool saw_target_yield = false;
    for (const auto &line : target_evidence.value ().evidence) {
        if (line.find ("yield-started|rid=play-b") != std::string::npos
            && line.find ("spot=" + target_spot_rid) != std::string::npos) {
            saw_target_yield = true;
        }
        ensure (line.find ("remote-yield-resumed|rid=play-b") == std::string::npos,
                "YD-D2 target node owned caller continuation");
    }
    ensure (saw_target_yield, "YD-D2 target play-b marker missing");
    std::cout << "scenario YD-D2 passed\n";
    return request_id;
}

} // namespace zlink::framework::e2e::yield_dispatch::client
