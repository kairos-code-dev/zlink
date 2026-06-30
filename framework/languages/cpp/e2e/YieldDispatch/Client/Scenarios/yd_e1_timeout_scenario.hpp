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
std::string run_yd_e1_timeout_scenario (TConnector &connector)
{
    const auto spot_rid = unique_id ("yield-timeout");
    auto spot =
      connector.request (ensure_spot_req_t{.spot_rid = spot_rid})
        .packet_name (ensure_spot_req_t::packet_name)
        .timeout (std::chrono::milliseconds (30000))
        .template submit<ensure_spot_reply_t> ();
    ensure (static_cast<bool> (spot), "YD-E1 ensure spot failed");
    ensure (spot.value ().spot_rid == spot_rid, "YD-E1 ensure spot reply mismatch");

    const auto request_id = unique_id ("YD-E1");
    auto sent_timeout =
      connector.send (yield_timeout_command_t{.request_id = request_id,
                                              .delay_ms = 700,
                                              .timeout_ms = 100})
        .packet_name (yield_timeout_command_t::packet_name)
        .metadata (spot_rid_metadata, spot_rid)
        .submit ();
    ensure (static_cast<bool> (sent_timeout), "YD-E1 YieldTimeoutCommand failed");

    auto timeout_evidence =
      connector.request (
                yield_evidence_wait_req_t{.request_id = request_id,
                                          .marker = "timeout-yield-completed",
                                          .timeout_milliseconds = 20000})
        .packet_name (yield_evidence_wait_req_t::packet_name)
        .metadata (target_node_rid_metadata, "play-a")
        .timeout (std::chrono::milliseconds (30000))
        .template submit<yield_evidence_reply_t> ();
    ensure (static_cast<bool> (timeout_evidence), "YD-E1 timeout evidence wait failed");
    ensure_contains_in_order (timeout_evidence.value ().evidence, request_id,
                              {"timeout-yield-started",
                               "timeout-yield-released",
                               "timeout-yield-completed"},
                              "YD-E1 timeout marker order mismatch");

    auto sent_probe =
      connector.send (probe_command_t{.request_id = request_id, .marker = "timeout-probe"})
        .packet_name (probe_command_t::packet_name)
        .metadata (spot_rid_metadata, spot_rid)
        .submit ();
    ensure (static_cast<bool> (sent_probe), "YD-E1 ProbeCommand failed");

    auto probe_evidence =
      connector.request (
                yield_evidence_wait_req_t{.request_id = request_id,
                                          .marker = "probe-completed",
                                          .timeout_milliseconds = 20000})
        .packet_name (yield_evidence_wait_req_t::packet_name)
        .metadata (target_node_rid_metadata, "play-a")
        .timeout (std::chrono::milliseconds (30000))
        .template submit<yield_evidence_reply_t> ();
    ensure (static_cast<bool> (probe_evidence), "YD-E1 probe evidence wait failed");
    ensure_contains_in_order (probe_evidence.value ().evidence, request_id,
                              {"timeout-yield-completed",
                               "probe-started",
                               "probe-completed"},
                              "YD-E1 cleanup probe marker order mismatch");
    ensure (every_request_line_has (probe_evidence.value ().evidence, request_id,
                                    {"rid=play-a"}),
            "YD-E1 evidence node mismatch");

    std::cout << "scenario YD-E1 passed\n";
    return request_id;
}

} // namespace zlink::framework::e2e::yield_dispatch::client
