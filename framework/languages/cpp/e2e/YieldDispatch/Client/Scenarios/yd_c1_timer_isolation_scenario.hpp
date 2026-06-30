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
std::string run_yd_c1_timer_isolation_scenario (TConnector &connector,
                                                TConnector &observer,
                                                const std::string &spot_rid)
{
    const auto request_id = unique_id ("YD-C1");
    auto timer_yield_start =
      connector.send (timer_start_command_t{.request_id = request_id,
                                            .timer_name = request_id + "-yield",
                                            .mode = "yield-on-first",
                                            .period_ms = 500,
                                            .delay_ms = 350})
        .packet_name (timer_start_command_t::packet_name)
        .metadata (spot_rid_metadata, spot_rid)
        .submit ();
    ensure (static_cast<bool> (timer_yield_start), "YD-C1 yield timer start failed");
    auto timer_yield_released =
      observer.request (
                yield_evidence_wait_req_t{.request_id = request_id,
                                          .marker = "timer-yield-released",
                                          .timeout_milliseconds = 20000})
        .packet_name (yield_evidence_wait_req_t::packet_name)
        .metadata (target_node_rid_metadata, "play-a")
        .timeout (std::chrono::milliseconds (30000))
        .template submit<yield_evidence_reply_t> ();
    ensure (static_cast<bool> (timer_yield_released),
            "YD-C1 timer-yield-released wait failed");
    auto timer_fast_start =
      connector.send (timer_start_command_t{.request_id = request_id,
                                            .timer_name = request_id + "-fast",
                                            .mode = "fast",
                                            .period_ms = 50,
                                            .delay_ms = 0})
        .packet_name (timer_start_command_t::packet_name)
        .metadata (spot_rid_metadata, spot_rid)
        .submit ();
    ensure (static_cast<bool> (timer_fast_start), "YD-C1 fast timer start failed");
    auto timer_fast_completed =
      observer.request (
                yield_evidence_wait_req_t{.request_id = request_id,
                                          .marker = "timer-fast-completed",
                                          .timeout_milliseconds = 20000})
        .packet_name (yield_evidence_wait_req_t::packet_name)
        .metadata (target_node_rid_metadata, "play-a")
        .timeout (std::chrono::milliseconds (30000))
        .template submit<yield_evidence_reply_t> ();
    ensure (static_cast<bool> (timer_fast_completed),
            "YD-C1 timer-fast-completed wait failed");
    auto evidence =
      observer.request (
                yield_evidence_wait_req_t{.request_id = request_id,
                                          .marker = "timer-yield-completed",
                                          .timeout_milliseconds = 20000})
        .packet_name (yield_evidence_wait_req_t::packet_name)
        .metadata (target_node_rid_metadata, "play-a")
        .timeout (std::chrono::milliseconds (30000))
        .template submit<yield_evidence_reply_t> ();
    ensure_result (evidence, "YD-C1 evidence wait failed");
    ensure_contains_in_order (evidence.value ().evidence, request_id,
                              {"timer-yield-started",
                               "timer-yield-released",
                               "timer-fast-started",
                               "timer-fast-completed",
                               "timer-yield-resumed",
                               "timer-yield-completed"},
                              "YD-C1 marker order mismatch");
    auto timer_stop =
      connector.send (timer_stop_command_t{.request_id = request_id})
        .packet_name (timer_stop_command_t::packet_name)
        .metadata (spot_rid_metadata, spot_rid)
        .submit ();
    ensure (static_cast<bool> (timer_stop), "YD-C1 timer stop failed");
    std::cout << "scenario YD-C1 passed\n";
    return request_id;
}

} // namespace zlink::framework::e2e::yield_dispatch::client
