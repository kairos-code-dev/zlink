/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../Shared/yield_dispatch_contracts.hpp"
#include "../Support/scenario_assert.hpp"

#include <chrono>
#include <iostream>
#include <string>

namespace zlink::framework::e2e::yield_dispatch::client
{

template <typename TConnector>
std::string run_yd_a3_continuation_context_scenario (TConnector &connector,
                                                     TConnector &observer,
                                                     const std::string &spot_rid)
{
    const auto request_id = unique_id ("YD-A3");
    auto context_send =
      connector.send (yield_command_t{.request_id = request_id,
                                      .delay_ms = 50,
                                      .correlation_id = "corr-a3"})
        .packet_name (yield_command_t::packet_name)
        .metadata (spot_rid_metadata, spot_rid)
        .submit ();
    ensure (static_cast<bool> (context_send), "YD-A3 YieldCommand send failed");
    auto evidence =
      observer.request (
                  yield_evidence_wait_req_t{.request_id = request_id,
                                            .marker = "yield-completed",
                                            .timeout_milliseconds = 20000})
        .packet_name (yield_evidence_wait_req_t::packet_name)
        .metadata (target_node_rid_metadata, "play-a")
        .timeout (std::chrono::milliseconds (30000))
        .template submit<yield_evidence_reply_t> ();
    ensure (static_cast<bool> (evidence), "YD-A3 evidence wait failed");
    ensure (contains_in_order (evidence.value ().evidence, request_id,
                               {"yield-started", "yield-released", "yield-resumed",
                                "yield-completed"}),
            "YD-A3 marker order mismatch");
    ensure (every_request_line_has (evidence.value ().evidence, request_id,
                                    {"spot=" + spot_rid, "correlation=corr-a3"}),
            "YD-A3 context evidence mismatch");
    std::cout << "scenario YD-A3 passed\n";
    return request_id;
}

} // namespace zlink::framework::e2e::yield_dispatch::client
