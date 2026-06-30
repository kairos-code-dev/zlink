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
std::string run_yd_a4_worker_yield_scenario (TConnector &connector,
                                             TConnector &observer,
                                             const std::string &spot_rid)
{
    const auto request_id = unique_id ("YD-A4");
    auto worker = std::async (std::launch::async, [&] {
        return connector.request (worker_yield_req_t{.request_id = request_id, .delay_ms = 350})
          .packet_name (worker_yield_req_t::packet_name)
          .metadata (spot_rid_metadata, spot_rid)
          .timeout (std::chrono::milliseconds (10000))
          .template submit<yield_dispatch_reply_t> ();
    });
    std::this_thread::sleep_for (std::chrono::milliseconds (75));
    auto worker_released =
      observer.request (
                  yield_evidence_wait_req_t{.request_id = request_id,
                                            .marker = "worker-yield-released",
                                            .timeout_milliseconds = 20000})
        .packet_name (yield_evidence_wait_req_t::packet_name)
        .metadata (target_node_rid_metadata, "play-a")
        .timeout (std::chrono::milliseconds (30000))
        .template submit<yield_evidence_reply_t> ();
    ensure (static_cast<bool> (worker_released), "YD-A4 worker-yield-released wait failed");
    auto worker_probe =
      observer.request (probe_req_t{.request_id = request_id, .marker = "worker-probe"})
        .packet_name (probe_req_t::packet_name)
        .metadata (spot_rid_metadata, spot_rid)
        .timeout (std::chrono::milliseconds (10000))
        .template submit<yield_dispatch_reply_t> ();
    ensure (static_cast<bool> (worker_probe), "YD-A4 ProbeReq failed");
    auto worker_reply = worker.get ();
    ensure (static_cast<bool> (worker_reply), "YD-A4 WorkerYieldReq failed");
    auto evidence =
      observer.request (
                  yield_evidence_wait_req_t{.request_id = request_id,
                                            .marker = "worker-yield-completed",
                                            .timeout_milliseconds = 20000})
        .packet_name (yield_evidence_wait_req_t::packet_name)
        .metadata (target_node_rid_metadata, "play-a")
        .timeout (std::chrono::milliseconds (30000))
        .template submit<yield_evidence_reply_t> ();
    ensure (static_cast<bool> (evidence), "YD-A4 evidence wait failed");
    ensure (contains_in_order (evidence.value ().evidence, request_id,
                               {"worker-yield-started", "worker-yield-released",
                                "probe-started", "probe-completed",
                                "worker-yield-resumed", "worker-yield-completed"}),
            "YD-A4 marker order mismatch");
    std::cout << "scenario YD-A4 passed\n";
    return request_id;
}

} // namespace zlink::framework::e2e::yield_dispatch::client
