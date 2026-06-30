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
std::string run_yd_c2_timer_reentry_scenario (TConnector &connector,
                                              TConnector &observer,
                                              const std::string &spot_rid)
{
    const auto request_id = unique_id ("YD-C2");
    connector.send (timer_start_msg_t{.request_id = request_id,
                                      .timer_name = request_id + "-same",
                                      .mode = "yield-then-next",
                                      .period_ms = 340,
                                      .delay_ms = 350})
        .packet_name (timer_start_msg_t::packet_name)
        .metadata (spot_rid_metadata, spot_rid)
        .submit ();
    auto evidence =
      observer.request (
                yield_evidence_wait_req_t{.request_id = request_id,
                                          .marker = "timer-next-completed",
                                          .timeout_milliseconds = 20000})
        .packet_name (yield_evidence_wait_req_t::packet_name)
        .metadata (target_node_rid_metadata, "play-a")
        .timeout (std::chrono::milliseconds (30000))
        .template submit<yield_evidence_res_t> ();
    ensure_result (evidence, "YD-C2 evidence wait failed");
    ensure_contains_in_order (evidence.value ().evidence, request_id,
                              {"timer-yield-started",
                               "timer-yield-released",
                               "timer-yield-resumed",
                               "timer-yield-completed",
                               "timer-next-started",
                               "timer-next-completed"},
                              "YD-C2 marker order mismatch");
    connector.send (timer_stop_msg_t{.request_id = request_id})
        .packet_name (timer_stop_msg_t::packet_name)
        .metadata (spot_rid_metadata, spot_rid)
        .submit ();
    std::cout << "scenario YD-C2 passed\n";
    return request_id;
}

} // namespace zlink::framework::e2e::yield_dispatch::client
