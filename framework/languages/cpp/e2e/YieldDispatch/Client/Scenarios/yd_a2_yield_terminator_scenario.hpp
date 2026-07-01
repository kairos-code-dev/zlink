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
std::string run_yd_a2_yield_terminator_scenario (TConnector &connector,
                                                 TConnector &observer,
                                                 const std::string &spot_rid)
{
    const auto request_id = unique_id ("YD-A2");
    connector.send (yield_msg_t{.request_id = request_id,
                                .delay_ms = 350,
                                .correlation_id = "corr-a2"})
      .packet_name (yield_msg_t::packet_name)
      .metadata (spot_rid_metadata, spot_rid)
      .submit ();
    std::this_thread::sleep_for (std::chrono::milliseconds (75));
    auto released =
      observer.request (
                yield_evidence_wait_req_t{.request_id = request_id,
                                          .marker = "yield-released",
                                          .timeout_milliseconds = 3000})
        .packet_name (yield_evidence_wait_req_t::packet_name)
        .metadata (target_node_rid_metadata, "play-a")
        .timeout (std::chrono::milliseconds (30000))
        .template submit<yield_evidence_res_t> ();
    ensure (static_cast<bool> (released), "YD-A2 yield-released wait failed");
    observer.send (probe_msg_t{.request_id = request_id, .marker = "yield-probe"})
        .packet_name (probe_msg_t::packet_name)
        .metadata (spot_rid_metadata, spot_rid)
        .submit ();
    auto evidence =
      observer.request (
                yield_evidence_wait_req_t{.request_id = request_id,
                                          .marker = "yield-completed",
                                          .timeout_milliseconds = 3000})
        .packet_name (yield_evidence_wait_req_t::packet_name)
        .metadata (target_node_rid_metadata, "play-a")
        .timeout (std::chrono::milliseconds (30000))
        .template submit<yield_evidence_res_t> ();
    ensure (static_cast<bool> (evidence), "YD-A2 evidence wait failed");
    ensure (contains_in_order (evidence.value ().evidence, request_id,
                               {"yield-started", "yield-released", "probe-started",
                                "probe-completed", "yield-resumed", "yield-completed"}),
            "YD-A2 marker order mismatch");
    std::cout << "scenario YD-A2 passed\n";
    return request_id;
}

} // namespace zlink::framework::e2e::yield_dispatch::client
