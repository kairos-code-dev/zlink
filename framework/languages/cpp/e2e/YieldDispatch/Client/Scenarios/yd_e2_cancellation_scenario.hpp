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
std::string run_yd_e2_cancellation_scenario (TConnector &connector)
{
    const auto spot_rid = unique_id ("yield-cancel");
    auto spot =
      connector.request (ensure_spot_req_t{.spot_rid = spot_rid})
        .packet_name (ensure_spot_req_t::packet_name)
        .timeout (std::chrono::milliseconds (30000))
        .template submit<ensure_spot_res_t> ();
    ensure (static_cast<bool> (spot), "YD-E2 ensure spot failed");
    ensure (spot.value ().spot_rid == spot_rid, "YD-E2 ensure spot reply mismatch");

    const auto request_id = unique_id ("YD-E2");
    connector.send (yield_cancel_msg_t{.request_id = request_id,
                                       .delay_ms = 800,
                                       .cancel_after_ms = 100})
        .packet_name (yield_cancel_msg_t::packet_name)
        .metadata (spot_rid_metadata, spot_rid)
        .submit ();

    auto cancel_evidence =
      connector.request (
                yield_evidence_wait_req_t{.request_id = request_id,
                                          .marker = "cancel-yield-completed",
                                          .timeout_milliseconds = 3000})
        .packet_name (yield_evidence_wait_req_t::packet_name)
        .metadata (target_node_rid_metadata, "play-a")
        .timeout (std::chrono::milliseconds (30000))
        .template submit<yield_evidence_res_t> ();
    ensure (static_cast<bool> (cancel_evidence), "YD-E2 cancellation evidence wait failed");
    ensure_contains_in_order (cancel_evidence.value ().evidence, request_id,
                              {"cancel-yield-started",
                               "cancel-yield-released",
                               "cancel-yield-completed"},
                              "YD-E2 cancellation marker order mismatch");

    connector.send (probe_msg_t{.request_id = request_id, .marker = "cancel-probe"})
        .packet_name (probe_msg_t::packet_name)
        .metadata (spot_rid_metadata, spot_rid)
        .submit ();

    auto probe_evidence =
      connector.request (
                yield_evidence_wait_req_t{.request_id = request_id,
                                          .marker = "probe-completed",
                                          .timeout_milliseconds = 3000})
        .packet_name (yield_evidence_wait_req_t::packet_name)
        .metadata (target_node_rid_metadata, "play-a")
        .timeout (std::chrono::milliseconds (30000))
        .template submit<yield_evidence_res_t> ();
    ensure (static_cast<bool> (probe_evidence), "YD-E2 probe evidence wait failed");
    ensure_contains_in_order (probe_evidence.value ().evidence, request_id,
                              {"cancel-yield-completed",
                               "probe-started",
                               "probe-completed"},
                              "YD-E2 cleanup probe marker order mismatch");
    ensure (!contains_request_marker (probe_evidence.value ().evidence, request_id,
                                      "cancel-yield-unexpected-resumed"),
            "YD-E2 cancelled yield resumed unexpectedly");
    ensure (every_request_line_has (probe_evidence.value ().evidence, request_id,
                                    {"rid=play-a"}),
            "YD-E2 evidence node mismatch");

    std::cout << "scenario YD-E2 passed\n";
    return request_id;
}

} // namespace zlink::framework::e2e::yield_dispatch::client
