/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../Shared/yield_dispatch_contracts.hpp"
#include "../Support/scenario_assert.hpp"

#include <chrono>
#include <future>
#include <iostream>
#include <string>
#include <thread>

namespace zlink::framework::e2e::yield_dispatch::client
{

template <typename TConnector>
std::string run_yd_a1_basic_terminator_scenario (TConnector &connector,
                                                 const std::string &spot_rid)
{
    const auto request_id = unique_id ("YD-A1");
    auto hold = std::async (std::launch::async, [&] {
        return connector.request (hold_req_t{.request_id = request_id, .delay_ms = 350})
          .packet_name (hold_req_t::packet_name)
          .metadata (spot_rid_metadata, spot_rid)
          .timeout (std::chrono::milliseconds (10000))
          .template submit<yield_dispatch_reply_t> ();
    });
    std::this_thread::sleep_for (std::chrono::milliseconds (75));
    auto hold_probe =
      connector.request (probe_req_t{.request_id = request_id, .marker = "hold-probe"})
        .packet_name (probe_req_t::packet_name)
        .metadata (spot_rid_metadata, spot_rid)
        .timeout (std::chrono::milliseconds (10000))
        .template submit<yield_dispatch_reply_t> ();
    ensure (static_cast<bool> (hold_probe), "YD-A1 ProbeCommand send failed");
    auto hold_reply = hold.get ();
    ensure (static_cast<bool> (hold_reply), "YD-A1 HoldReq failed");
    auto evidence =
      connector.request (
                 yield_evidence_wait_req_t{.request_id = request_id,
                                           .marker = "probe-completed",
                                           .timeout_milliseconds = 20000})
        .packet_name (yield_evidence_wait_req_t::packet_name)
        .metadata (target_node_rid_metadata, "play-a")
        .timeout (std::chrono::milliseconds (30000))
        .template submit<yield_evidence_reply_t> ();
    ensure (static_cast<bool> (evidence), "YD-A1 evidence wait failed");
    ensure (contains_in_order (evidence.value ().evidence, request_id,
                               {"hold-started", "hold-resumed", "hold-completed",
                                "probe-started"}),
            "YD-A1 marker order mismatch");
    std::cout << "scenario YD-A1 passed\n";
    return request_id;
}

} // namespace zlink::framework::e2e::yield_dispatch::client
