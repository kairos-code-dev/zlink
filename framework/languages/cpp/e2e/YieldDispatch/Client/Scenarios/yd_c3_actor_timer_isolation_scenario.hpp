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

namespace zlink::framework::e2e::yield_dispatch::client
{

template <typename TConnector>
void run_yd_c3_actor_timer_isolation_scenario (
  TConnector &connector,
  const yield_actor_scenario_context_t &actors)
{
    const auto actor_then_timer = unique_id ("YD-C3A");
    std::promise<zlink::stream_connector::result_t<actor_yield_res_t>>
      actor_yield_promise;
    auto actor_yield = actor_yield_promise.get_future ();
    connector.request (actor_yield_req_t{.request_id = actor_then_timer, .delay_ms = 1500})
      .packet_name (actor_yield_req_t::packet_name)
      .metadata (actor_id_metadata, actors.actor_a)
      .timeout (std::chrono::milliseconds (30000))
      .template submit<actor_yield_res_t> (
        [&] (zlink::stream_connector::result_t<actor_yield_res_t> result) {
            actor_yield_promise.set_value (std::move (result));
        });
    auto actor_released =
      connector.request (
                yield_evidence_wait_req_t{.request_id = actor_then_timer,
                                          .marker = "actor-yield-released",
                                          .timeout_milliseconds = 3000})
        .packet_name (yield_evidence_wait_req_t::packet_name)
        .metadata (target_node_rid_metadata, "play-a")
        .timeout (std::chrono::milliseconds (30000))
        .template submit<yield_evidence_res_t> ();
    ensure (static_cast<bool> (actor_released), "YD-C3A actor-yield-released wait failed");
    connector.send (timer_start_msg_t{.request_id = actor_then_timer,
                                      .timer_name = actor_then_timer + "-fast",
                                      .mode = "fast",
                                      .period_ms = 50,
                                      .delay_ms = 0})
        .packet_name (timer_start_msg_t::packet_name)
        .metadata (spot_rid_metadata, actors.spot_rid)
        .submit ();
    auto fast_timer_completed =
      connector.request (
                yield_evidence_wait_req_t{.request_id = actor_then_timer,
                                          .marker = "timer-fast-completed",
                                          .timeout_milliseconds = 3000})
        .packet_name (yield_evidence_wait_req_t::packet_name)
        .metadata (target_node_rid_metadata, "play-a")
        .timeout (std::chrono::milliseconds (30000))
        .template submit<yield_evidence_res_t> ();
    ensure (static_cast<bool> (fast_timer_completed),
            "YD-C3A timer-fast-completed wait failed");
    connector.send (timer_stop_msg_t{.request_id = actor_then_timer})
        .packet_name (timer_stop_msg_t::packet_name)
        .metadata (spot_rid_metadata, actors.spot_rid)
        .submit ();
    auto actor_yield_reply = actor_yield.get ();
    ensure (static_cast<bool> (actor_yield_reply), "YD-C3A ActorYieldReq failed");
    auto actor_timer_evidence =
      connector.request (yield_evidence_req_t{.request_id = actor_then_timer})
        .packet_name (yield_evidence_req_t::packet_name)
        .metadata (target_node_rid_metadata, "play-a")
        .timeout (std::chrono::milliseconds (30000))
        .template submit<yield_evidence_res_t> ();
    ensure (static_cast<bool> (actor_timer_evidence),
            "YD-C3A evidence request failed");
    ensure_contains_in_order (actor_timer_evidence.value ().evidence,
                              actor_then_timer,
                              {"actor-yield-started",
                               "actor-yield-released",
                               "timer-fast-started",
                               "timer-fast-completed",
                               "actor-yield-resumed",
                               "actor-yield-completed"},
                              "YD-C3A marker order mismatch");

    const auto timer_then_actor = unique_id ("YD-C3B");
    connector.send (timer_start_msg_t{.request_id = timer_then_actor,
                                      .timer_name = timer_then_actor + "-yield",
                                      .mode = "yield-on-first",
                                      .period_ms = 500,
                                      .delay_ms = 350})
        .packet_name (timer_start_msg_t::packet_name)
        .metadata (spot_rid_metadata, actors.spot_rid)
        .submit ();
    auto yield_timer_released =
      connector.request (
                yield_evidence_wait_req_t{.request_id = timer_then_actor,
                                          .marker = "timer-yield-released",
                                          .timeout_milliseconds = 3000})
        .packet_name (yield_evidence_wait_req_t::packet_name)
        .metadata (target_node_rid_metadata, "play-a")
        .timeout (std::chrono::milliseconds (30000))
        .template submit<yield_evidence_res_t> ();
    ensure (static_cast<bool> (yield_timer_released),
            "YD-C3B timer-yield-released wait failed");
    auto actor_fast =
      connector.request (actor_fast_req_t{.request_id = timer_then_actor,
                                          .marker = "c3-actor-fast"})
        .packet_name (actor_fast_req_t::packet_name)
        .metadata (actor_id_metadata, actors.actor_b)
        .timeout (std::chrono::milliseconds (30000))
        .template submit<actor_yield_res_t> ();
    ensure (static_cast<bool> (actor_fast), "YD-C3B ActorFastReq failed");
    auto timer_actor_evidence =
      connector.request (
                yield_evidence_wait_req_t{.request_id = timer_then_actor,
                                          .marker = "timer-yield-completed",
                                          .timeout_milliseconds = 3000})
        .packet_name (yield_evidence_wait_req_t::packet_name)
        .metadata (target_node_rid_metadata, "play-a")
        .timeout (std::chrono::milliseconds (30000))
        .template submit<yield_evidence_res_t> ();
    ensure (static_cast<bool> (timer_actor_evidence),
            "YD-C3B evidence wait failed");
    connector.send (timer_stop_msg_t{.request_id = timer_then_actor})
        .packet_name (timer_stop_msg_t::packet_name)
        .metadata (spot_rid_metadata, actors.spot_rid)
        .submit ();
    ensure_contains_in_order (timer_actor_evidence.value ().evidence,
                              timer_then_actor,
                              {"timer-yield-started",
                               "timer-yield-released",
                               "actor-fast-started",
                               "actor-fast-completed",
                               "timer-yield-resumed",
                               "timer-yield-completed"},
                              "YD-C3B marker order mismatch");
    std::cout << "scenario YD-C3 passed\n";
}

} // namespace zlink::framework::e2e::yield_dispatch::client
