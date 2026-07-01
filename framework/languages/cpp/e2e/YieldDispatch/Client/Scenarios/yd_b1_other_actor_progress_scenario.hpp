/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../Shared/yield_dispatch_contracts.hpp"
#include "../Support/scenario_assert.hpp"
#include "yield_actor_scenario_context.hpp"

#include <zlink/stream_connector.hpp>

#include <chrono>
#include <future>
#include <iostream>
#include <string>
#include <thread>

namespace zlink::framework::e2e::yield_dispatch::client
{

template <typename TConnector>
std::string run_yd_b1_other_actor_progress_scenario (
  TConnector &connector,
  const yield_actor_scenario_context_t &actors)
{
    const auto request_id = unique_id ("YD-B1");
    std::promise<zlink::stream_connector::result_t<actor_yield_res_t>> actor_yield_promise;
    auto actor_yield = actor_yield_promise.get_future ();
    connector.request (actor_yield_req_t{.request_id = request_id, .delay_ms = 350})
      .packet_name (actor_yield_req_t::packet_name)
      .metadata (actor_id_metadata, actors.actor_a)
      .timeout (std::chrono::milliseconds (30000))
      .template submit<actor_yield_res_t> (
        [&] (zlink::stream_connector::result_t<actor_yield_res_t> result) {
            actor_yield_promise.set_value (std::move (result));
        });
    std::this_thread::sleep_for (std::chrono::milliseconds (75));
    auto actor_fast =
      connector.request (actor_fast_req_t{.request_id = request_id, .marker = "b1-fast"})
        .packet_name (actor_fast_req_t::packet_name)
        .metadata (actor_id_metadata, actors.actor_b)
        .timeout (std::chrono::milliseconds (30000))
        .template submit<actor_yield_res_t> ();
    ensure (static_cast<bool> (actor_fast), "YD-B1 ActorFastReq failed");
    auto actor_yield_reply = actor_yield.get ();
    ensure (static_cast<bool> (actor_yield_reply), "YD-B1 ActorYieldReq failed");
    auto evidence =
      connector.request (yield_evidence_req_t{.request_id = request_id})
        .packet_name (yield_evidence_req_t::packet_name)
        .metadata (target_node_rid_metadata, "play-a")
        .timeout (std::chrono::milliseconds (30000))
        .template submit<yield_evidence_res_t> ();
    ensure (static_cast<bool> (evidence), "YD-B1 evidence request failed");
    ensure (contains_in_order (evidence.value ().evidence, request_id,
                               {"actor-yield-started", "actor-yield-released",
                                "actor-fast-started", "actor-fast-completed",
                                "actor-yield-resumed", "actor-yield-completed"}),
            "YD-B1 marker order mismatch");
    std::cout << "scenario YD-B1 passed\n";
    return request_id;
}

} // namespace zlink::framework::e2e::yield_dispatch::client
