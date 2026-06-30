/* SPDX-License-Identifier: MPL-2.0 */

#include "../Shared/yield_dispatch_contracts.hpp"
#include "Scenarios/yield_actor_scenario_context.hpp"
#include "Support/client_options.hpp"
#include "Support/evidence_wait.hpp"
#include "Support/scenario_assert.hpp"

#include <zlink/stream_connector.hpp>

#include <chrono>
#include <future>
#include <iostream>
#include <string>
#include <thread>

namespace yd = zlink::framework::e2e::yield_dispatch;
namespace yd_client = zlink::framework::e2e::yield_dispatch::client;

namespace
{

using yd_client::contains_in_order;
using yd_client::ensure;
using yd_client::ensure_contains_in_order;
using yd_client::ensure_result;
using yd_client::every_request_line_has;
using yd_client::unique_id;
using yd_client::wait_for_evidence_snapshot;

} // namespace

int main ()
{
    try {
        const auto client_options = yd_client::parse_client_options ();
        ensure (!client_options.session_a_stream_endpoint.empty (),
                "ZLINK_CPP_E2E_STREAM_ENDPOINT is required");

        auto options = yd_client::make_connector_options (client_options);
        auto client = zlink::stream_connector::connector_factory_t::create (options);
        client.codecs ().add_json ();
        auto connected = client.connect ();
        ensure (static_cast<bool> (connected), "YieldDispatch stream connect failed");
        auto observer = zlink::stream_connector::connector_factory_t::create (options);
        observer.codecs ().add_json ();
        auto observer_connected = observer.connect ();
        ensure (static_cast<bool> (observer_connected),
                "YieldDispatch observer stream connect failed");

        const auto spot_rid = unique_id ("yield-track-a");
        auto spot =
          client.request (yd::ensure_spot_req_t{.spot_rid = spot_rid})
            .packet_name (yd::ensure_spot_req_t::packet_name)
            .timeout (std::chrono::milliseconds (15000))
            .submit<yd::ensure_spot_reply_t> ();
        ensure (static_cast<bool> (spot), "YD-A ensure spot request failed");
        ensure (spot.value ().spot_rid == spot_rid, "YD-A ensure spot reply mismatch");
        const yd_client::yield_actor_scenario_context_t actors{
          .spot_rid = spot_rid, .actor_a = unique_id ("actor-a"), .actor_b = unique_id ("actor-b")};
        auto bound_actors =
          client.request (yd::bind_yield_actors_req_t{.spot_rid = actors.spot_rid,
                                                      .actor_ids = {actors.actor_a, actors.actor_b}})
            .packet_name (yd::bind_yield_actors_req_t::packet_name)
            .timeout (std::chrono::milliseconds (15000))
            .submit<yd::bind_yield_actors_reply_t> ();
        ensure (static_cast<bool> (bound_actors), "YD-B bind actors failed");
        ensure (bound_actors.value ().actors.size () == 2, "YD-B bind actor count mismatch");
        auto observer_bound_actors =
          observer.request (
                    yd::bind_yield_actors_req_t{.spot_rid = actors.spot_rid,
                                                .actor_ids = {actors.actor_a, actors.actor_b}})
            .packet_name (yd::bind_yield_actors_req_t::packet_name)
            .timeout (std::chrono::milliseconds (15000))
            .submit<yd::bind_yield_actors_reply_t> ();
        ensure (static_cast<bool> (observer_bound_actors),
                "YD-B observer bind actors failed");

        const auto a1 = unique_id ("YD-A1");
        auto hold = std::async (std::launch::async, [&] {
            return client.request (yd::hold_req_t{.request_id = a1, .delay_ms = 350})
              .packet_name (yd::hold_req_t::packet_name)
              .metadata (yd::spot_rid_metadata, spot_rid)
              .timeout (std::chrono::milliseconds (10000))
              .submit<yd::yield_dispatch_reply_t> ();
        });
        std::this_thread::sleep_for (std::chrono::milliseconds (75));
        auto hold_probe =
          client.request (yd::probe_req_t{.request_id = a1, .marker = "hold-probe"})
            .packet_name (yd::probe_req_t::packet_name)
            .metadata (yd::spot_rid_metadata, spot_rid)
            .timeout (std::chrono::milliseconds (10000))
            .submit<yd::yield_dispatch_reply_t> ();
        ensure (static_cast<bool> (hold_probe), "YD-A1 ProbeCommand send failed");
        auto hold_reply = hold.get ();
        ensure (static_cast<bool> (hold_reply), "YD-A1 HoldReq failed");
        auto a1_evidence =
          client.request (
                  yd::yield_evidence_wait_req_t{.request_id = a1,
                                                .marker = "probe-completed",
                                                .timeout_milliseconds = 20000})
            .packet_name (yd::yield_evidence_wait_req_t::packet_name)
            .metadata (yd::target_node_rid_metadata, "play-a")
            .timeout (std::chrono::milliseconds (30000))
            .submit<yd::yield_evidence_reply_t> ();
        ensure (static_cast<bool> (a1_evidence), "YD-A1 evidence wait failed");
        ensure (contains_in_order (a1_evidence.value ().evidence, a1,
                                   {"hold-started", "hold-resumed", "hold-completed",
                                    "probe-started"}),
                "YD-A1 marker order mismatch");
        std::cout << "scenario YD-A1 passed\n";

        const auto a2 = unique_id ("YD-A2");
        auto yielded = std::async (std::launch::async, [&] {
            return client
              .request (yd::yield_req_t{.request_id = a2,
                                        .delay_ms = 350,
                                        .correlation_id = "corr-a2"})
              .packet_name (yd::yield_req_t::packet_name)
              .metadata (yd::spot_rid_metadata, spot_rid)
              .timeout (std::chrono::milliseconds (10000))
              .submit<yd::yield_dispatch_reply_t> ();
        });
        std::this_thread::sleep_for (std::chrono::milliseconds (75));
        auto released =
          observer.request (
                  yd::yield_evidence_wait_req_t{.request_id = a2,
                                                .marker = "yield-released",
                                                .timeout_milliseconds = 20000})
            .packet_name (yd::yield_evidence_wait_req_t::packet_name)
            .metadata (yd::target_node_rid_metadata, "play-a")
            .timeout (std::chrono::milliseconds (30000))
            .submit<yd::yield_evidence_reply_t> ();
        ensure (static_cast<bool> (released), "YD-A2 yield-released wait failed");
        auto yield_probe =
          observer.request (yd::probe_req_t{.request_id = a2, .marker = "yield-probe"})
            .packet_name (yd::probe_req_t::packet_name)
            .metadata (yd::spot_rid_metadata, spot_rid)
            .timeout (std::chrono::milliseconds (10000))
            .submit<yd::yield_dispatch_reply_t> ();
        ensure (static_cast<bool> (yield_probe), "YD-A2 ProbeCommand send failed");
        auto yield_reply = yielded.get ();
        ensure (static_cast<bool> (yield_reply), "YD-A2 YieldReq failed");
        auto a2_evidence =
          observer.request (
                  yd::yield_evidence_wait_req_t{.request_id = a2,
                                                .marker = "yield-completed",
                                                .timeout_milliseconds = 20000})
            .packet_name (yd::yield_evidence_wait_req_t::packet_name)
            .metadata (yd::target_node_rid_metadata, "play-a")
            .timeout (std::chrono::milliseconds (30000))
            .submit<yd::yield_evidence_reply_t> ();
        ensure (static_cast<bool> (a2_evidence), "YD-A2 evidence wait failed");
        ensure (contains_in_order (a2_evidence.value ().evidence, a2,
                                   {"yield-started", "yield-released", "probe-started",
                                    "probe-completed", "yield-resumed", "yield-completed"}),
                "YD-A2 marker order mismatch");
        std::cout << "scenario YD-A2 passed\n";

        const auto a3 = unique_id ("YD-A3");
        auto context_send =
          client.send (yd::yield_command_t{.request_id = a3,
                                           .delay_ms = 50,
                                           .correlation_id = "corr-a3"})
            .packet_name (yd::yield_command_t::packet_name)
            .metadata (yd::spot_rid_metadata, spot_rid)
            .submit ();
        ensure (static_cast<bool> (context_send), "YD-A3 YieldCommand send failed");
        auto a3_evidence =
          observer.request (
                    yd::yield_evidence_wait_req_t{.request_id = a3,
                                                  .marker = "yield-completed",
                                                  .timeout_milliseconds = 20000})
            .packet_name (yd::yield_evidence_wait_req_t::packet_name)
            .metadata (yd::target_node_rid_metadata, "play-a")
            .timeout (std::chrono::milliseconds (30000))
            .submit<yd::yield_evidence_reply_t> ();
        ensure (static_cast<bool> (a3_evidence), "YD-A3 evidence wait failed");
        ensure (contains_in_order (a3_evidence.value ().evidence, a3,
                                   {"yield-started", "yield-released", "yield-resumed",
                                    "yield-completed"}),
                "YD-A3 marker order mismatch");
        ensure (every_request_line_has (a3_evidence.value ().evidence, a3,
                                        {"spot=" + spot_rid, "correlation=corr-a3"}),
                "YD-A3 context evidence mismatch");
        std::cout << "scenario YD-A3 passed\n";

        const auto a4 = unique_id ("YD-A4");
        auto worker = std::async (std::launch::async, [&] {
            return client
              .request (yd::worker_yield_req_t{.request_id = a4, .delay_ms = 350})
              .packet_name (yd::worker_yield_req_t::packet_name)
              .metadata (yd::spot_rid_metadata, spot_rid)
              .timeout (std::chrono::milliseconds (10000))
              .submit<yd::yield_dispatch_reply_t> ();
        });
        std::this_thread::sleep_for (std::chrono::milliseconds (75));
        auto worker_released =
          observer.request (
                    yd::yield_evidence_wait_req_t{.request_id = a4,
                                                  .marker = "worker-yield-released",
                                                  .timeout_milliseconds = 20000})
            .packet_name (yd::yield_evidence_wait_req_t::packet_name)
            .metadata (yd::target_node_rid_metadata, "play-a")
            .timeout (std::chrono::milliseconds (30000))
            .submit<yd::yield_evidence_reply_t> ();
        ensure (static_cast<bool> (worker_released), "YD-A4 worker-yield-released wait failed");
        auto worker_probe =
          observer.request (yd::probe_req_t{.request_id = a4, .marker = "worker-probe"})
            .packet_name (yd::probe_req_t::packet_name)
            .metadata (yd::spot_rid_metadata, spot_rid)
            .timeout (std::chrono::milliseconds (10000))
            .submit<yd::yield_dispatch_reply_t> ();
        ensure (static_cast<bool> (worker_probe), "YD-A4 ProbeReq failed");
        auto worker_reply = worker.get ();
        ensure (static_cast<bool> (worker_reply), "YD-A4 WorkerYieldReq failed");
        auto a4_evidence =
          observer.request (
                    yd::yield_evidence_wait_req_t{.request_id = a4,
                                                  .marker = "worker-yield-completed",
                                                  .timeout_milliseconds = 20000})
            .packet_name (yd::yield_evidence_wait_req_t::packet_name)
            .metadata (yd::target_node_rid_metadata, "play-a")
            .timeout (std::chrono::milliseconds (30000))
            .submit<yd::yield_evidence_reply_t> ();
        ensure (static_cast<bool> (a4_evidence), "YD-A4 evidence wait failed");
        ensure (contains_in_order (a4_evidence.value ().evidence, a4,
                                   {"worker-yield-started", "worker-yield-released",
                                    "probe-started", "probe-completed",
                                    "worker-yield-resumed", "worker-yield-completed"}),
                "YD-A4 marker order mismatch");
        std::cout << "scenario YD-A4 passed\n";

        const auto b1 = unique_id ("YD-B1");
        std::promise<zlink::stream_connector::result_t<yd::actor_yield_reply_t>>
          actor_yield_b1_promise;
        auto actor_yield_b1 = actor_yield_b1_promise.get_future ();
        client.request (yd::actor_yield_req_t{.request_id = b1, .delay_ms = 350})
          .packet_name (yd::actor_yield_req_t::packet_name)
          .metadata (yd::actor_id_metadata, actors.actor_a)
          .timeout (std::chrono::milliseconds (30000))
          .submit<yd::actor_yield_reply_t> (
            [&] (zlink::stream_connector::result_t<yd::actor_yield_reply_t> result) {
                actor_yield_b1_promise.set_value (std::move (result));
            });
        std::this_thread::sleep_for (std::chrono::milliseconds (75));
        auto actor_fast_b1 =
          observer.request (yd::actor_fast_req_t{.request_id = b1, .marker = "b1-fast"})
            .packet_name (yd::actor_fast_req_t::packet_name)
            .metadata (yd::actor_id_metadata, actors.actor_b)
            .timeout (std::chrono::milliseconds (30000))
            .submit<yd::actor_yield_reply_t> ();
        ensure (static_cast<bool> (actor_fast_b1), "YD-B1 ActorFastReq failed");
        auto actor_yield_b1_reply = actor_yield_b1.get ();
        ensure (static_cast<bool> (actor_yield_b1_reply), "YD-B1 ActorYieldReq failed");
        auto b1_evidence =
          client.request (yd::yield_evidence_req_t{.request_id = b1})
            .packet_name (yd::yield_evidence_req_t::packet_name)
            .metadata (yd::target_node_rid_metadata, "play-a")
            .timeout (std::chrono::milliseconds (30000))
            .submit<yd::yield_evidence_reply_t> ();
        ensure (static_cast<bool> (b1_evidence), "YD-B1 evidence request failed");
        ensure (contains_in_order (b1_evidence.value ().evidence, b1,
                                   {"actor-yield-started", "actor-yield-released",
                                    "actor-fast-started", "actor-fast-completed",
                                    "actor-yield-resumed", "actor-yield-completed"}),
                "YD-B1 marker order mismatch");
        std::cout << "scenario YD-B1 passed\n";

        const auto b2 = unique_id ("YD-B2");
        std::promise<zlink::stream_connector::result_t<yd::actor_yield_reply_t>>
          actor_yield_b2_promise;
        auto actor_yield_b2 = actor_yield_b2_promise.get_future ();
        client.request (yd::actor_yield_req_t{.request_id = b2, .delay_ms = 350})
          .packet_name (yd::actor_yield_req_t::packet_name)
          .metadata (yd::actor_id_metadata, actors.actor_a)
          .timeout (std::chrono::milliseconds (30000))
          .submit<yd::actor_yield_reply_t> (
            [&] (zlink::stream_connector::result_t<yd::actor_yield_reply_t> result) {
                actor_yield_b2_promise.set_value (std::move (result));
            });
        std::this_thread::sleep_for (std::chrono::milliseconds (75));
        auto actor_fast_b2 =
          observer.request (yd::actor_fast_req_t{.request_id = b2, .marker = "b2-fast"})
            .packet_name (yd::actor_fast_req_t::packet_name)
            .metadata (yd::actor_id_metadata, actors.actor_a)
            .timeout (std::chrono::milliseconds (30000))
            .submit<yd::actor_yield_reply_t> ();
        ensure (static_cast<bool> (actor_fast_b2), "YD-B2 ActorFastReq failed");
        auto actor_yield_b2_reply = actor_yield_b2.get ();
        ensure (static_cast<bool> (actor_yield_b2_reply), "YD-B2 ActorYieldReq failed");
        auto b2_evidence =
          client.request (yd::yield_evidence_req_t{.request_id = b2})
            .packet_name (yd::yield_evidence_req_t::packet_name)
            .metadata (yd::target_node_rid_metadata, "play-a")
            .timeout (std::chrono::milliseconds (30000))
            .submit<yd::yield_evidence_reply_t> ();
        ensure (static_cast<bool> (b2_evidence), "YD-B2 evidence request failed");
        ensure (contains_in_order (b2_evidence.value ().evidence, b2,
                                   {"actor-yield-started", "actor-yield-released",
                                    "actor-yield-resumed", "actor-yield-completed",
                                    "actor-fast-started", "actor-fast-completed"}),
                "YD-B2 marker order mismatch");
        std::cout << "scenario YD-B2 passed\n";

        const auto b3 = unique_id ("YD-B3");
        std::promise<zlink::stream_connector::result_t<yd::actor_yield_reply_t>>
          actor_join_b3_promise;
        auto actor_join_b3 = actor_join_b3_promise.get_future ();
        client.request (yd::actor_join_yield_req_t{.request_id = b3,
                                                   .target_node_rid = "play-a"})
          .packet_name (yd::actor_join_yield_req_t::packet_name)
          .metadata (yd::actor_id_metadata, actors.actor_a)
          .timeout (std::chrono::milliseconds (30000))
          .submit<yd::actor_yield_reply_t> (
            [&] (zlink::stream_connector::result_t<yd::actor_yield_reply_t> result) {
                actor_join_b3_promise.set_value (std::move (result));
            });
        std::this_thread::sleep_for (std::chrono::milliseconds (75));
        auto actor_fast_b3 =
          observer.request (yd::actor_fast_req_t{.request_id = b3, .marker = "b3-fast"})
            .packet_name (yd::actor_fast_req_t::packet_name)
            .metadata (yd::actor_id_metadata, actors.actor_b)
            .timeout (std::chrono::milliseconds (30000))
            .submit<yd::actor_yield_reply_t> ();
        ensure (static_cast<bool> (actor_fast_b3), "YD-B3 ActorFastReq failed");
        auto actor_join_b3_reply = actor_join_b3.get ();
        ensure (static_cast<bool> (actor_join_b3_reply), "YD-B3 ActorJoinYieldReq failed");
        auto b3_evidence =
          client.request (yd::yield_evidence_req_t{.request_id = b3})
            .packet_name (yd::yield_evidence_req_t::packet_name)
            .metadata (yd::target_node_rid_metadata, "play-a")
            .timeout (std::chrono::milliseconds (30000))
            .submit<yd::yield_evidence_reply_t> ();
        ensure (static_cast<bool> (b3_evidence), "YD-B3 evidence request failed");
        ensure (contains_in_order (b3_evidence.value ().evidence, b3,
                                   {"actor-join-yield-started",
                                    "actor-join-yield-released",
                                    "actor-fast-started",
                                    "actor-fast-completed",
                                    "actor-join-yield-resumed",
                                    "actor-join-yield-completed"}),
                "YD-B3 marker order mismatch");
        std::cout << "scenario YD-B3 passed\n";

        const auto timer_spot_rid = unique_id ("yield-timer");
        auto timer_spot =
          client.request (yd::ensure_spot_req_t{.spot_rid = timer_spot_rid})
            .packet_name (yd::ensure_spot_req_t::packet_name)
            .timeout (std::chrono::milliseconds (15000))
            .submit<yd::ensure_spot_reply_t> ();
        ensure (static_cast<bool> (timer_spot), "YD-C ensure timer spot request failed");
        ensure (timer_spot.value ().spot_rid == timer_spot_rid,
                "YD-C ensure timer spot reply mismatch");

        const auto c1 = unique_id ("YD-C1");
        auto timer_yield_start =
          client.send (yd::timer_start_command_t{.request_id = c1,
                                                 .timer_name = c1 + "-yield",
                                                 .mode = "yield-on-first",
                                                 .period_ms = 500,
                                                 .delay_ms = 350})
            .packet_name (yd::timer_start_command_t::packet_name)
            .metadata (yd::spot_rid_metadata, timer_spot_rid)
            .submit ();
        ensure (static_cast<bool> (timer_yield_start), "YD-C1 yield timer start failed");
        auto timer_yield_released =
          observer.request (
                    yd::yield_evidence_wait_req_t{.request_id = c1,
                                                  .marker = "timer-yield-released",
                                                  .timeout_milliseconds = 20000})
            .packet_name (yd::yield_evidence_wait_req_t::packet_name)
            .metadata (yd::target_node_rid_metadata, "play-a")
            .timeout (std::chrono::milliseconds (30000))
            .submit<yd::yield_evidence_reply_t> ();
        ensure (static_cast<bool> (timer_yield_released),
                "YD-C1 timer-yield-released wait failed");
        auto timer_fast_start =
          client.send (yd::timer_start_command_t{.request_id = c1,
                                                 .timer_name = c1 + "-fast",
                                                 .mode = "fast",
                                                 .period_ms = 50,
                                                 .delay_ms = 0})
            .packet_name (yd::timer_start_command_t::packet_name)
            .metadata (yd::spot_rid_metadata, timer_spot_rid)
            .submit ();
        ensure (static_cast<bool> (timer_fast_start), "YD-C1 fast timer start failed");
        auto timer_fast_completed =
          observer.request (
                    yd::yield_evidence_wait_req_t{.request_id = c1,
                                                  .marker = "timer-fast-completed",
                                                  .timeout_milliseconds = 20000})
            .packet_name (yd::yield_evidence_wait_req_t::packet_name)
            .metadata (yd::target_node_rid_metadata, "play-a")
            .timeout (std::chrono::milliseconds (30000))
            .submit<yd::yield_evidence_reply_t> ();
        ensure (static_cast<bool> (timer_fast_completed),
                "YD-C1 timer-fast-completed wait failed");
        auto c1_evidence =
          observer.request (
                    yd::yield_evidence_wait_req_t{.request_id = c1,
                                                  .marker = "timer-yield-completed",
                                                  .timeout_milliseconds = 20000})
            .packet_name (yd::yield_evidence_wait_req_t::packet_name)
            .metadata (yd::target_node_rid_metadata, "play-a")
            .timeout (std::chrono::milliseconds (30000))
            .submit<yd::yield_evidence_reply_t> ();
        ensure_result (c1_evidence, "YD-C1 evidence wait failed");
        ensure_contains_in_order (c1_evidence.value ().evidence, c1,
                                  {"timer-yield-started",
                                   "timer-yield-released",
                                   "timer-fast-started",
                                   "timer-fast-completed",
                                   "timer-yield-resumed",
                                   "timer-yield-completed"},
                                  "YD-C1 marker order mismatch");
        auto timer_stop =
          client.send (yd::timer_stop_command_t{.request_id = c1})
            .packet_name (yd::timer_stop_command_t::packet_name)
            .metadata (yd::spot_rid_metadata, timer_spot_rid)
            .submit ();
        ensure (static_cast<bool> (timer_stop), "YD-C1 timer stop failed");
        std::cout << "scenario YD-C1 passed\n";

        const auto c2 = unique_id ("YD-C2");
        auto timer_reentry_start =
          client.send (yd::timer_start_command_t{.request_id = c2,
                                                 .timer_name = c2 + "-same",
                                                 .mode = "yield-then-next",
                                                 .period_ms = 340,
                                                 .delay_ms = 350})
            .packet_name (yd::timer_start_command_t::packet_name)
            .metadata (yd::spot_rid_metadata, timer_spot_rid)
            .submit ();
        ensure (static_cast<bool> (timer_reentry_start), "YD-C2 timer start failed");
        auto c2_evidence =
          observer.request (
                    yd::yield_evidence_wait_req_t{.request_id = c2,
                                                  .marker = "timer-next-completed",
                                                  .timeout_milliseconds = 20000})
            .packet_name (yd::yield_evidence_wait_req_t::packet_name)
            .metadata (yd::target_node_rid_metadata, "play-a")
            .timeout (std::chrono::milliseconds (30000))
            .submit<yd::yield_evidence_reply_t> ();
        ensure_result (c2_evidence, "YD-C2 evidence wait failed");
        ensure_contains_in_order (c2_evidence.value ().evidence, c2,
                                  {"timer-yield-started",
                                   "timer-yield-released",
                                   "timer-yield-resumed",
                                   "timer-yield-completed",
                                   "timer-next-started",
                                   "timer-next-completed"},
                                  "YD-C2 marker order mismatch");
        auto c2_timer_stop =
          client.send (yd::timer_stop_command_t{.request_id = c2})
            .packet_name (yd::timer_stop_command_t::packet_name)
            .metadata (yd::spot_rid_metadata, timer_spot_rid)
            .submit ();
        ensure (static_cast<bool> (c2_timer_stop), "YD-C2 timer stop failed");
        std::cout << "scenario YD-C2 passed\n";

        const auto c3_actor_then_timer = unique_id ("YD-C3A");
        std::promise<zlink::stream_connector::result_t<yd::actor_yield_reply_t>>
          actor_yield_c3_promise;
        auto actor_yield_c3 = actor_yield_c3_promise.get_future ();
        client.request (
                yd::actor_yield_req_t{.request_id = c3_actor_then_timer, .delay_ms = 350})
          .packet_name (yd::actor_yield_req_t::packet_name)
          .metadata (yd::actor_id_metadata, actors.actor_a)
          .timeout (std::chrono::milliseconds (30000))
          .submit<yd::actor_yield_reply_t> (
            [&] (zlink::stream_connector::result_t<yd::actor_yield_reply_t> result) {
                actor_yield_c3_promise.set_value (std::move (result));
            });
        std::this_thread::sleep_for (std::chrono::milliseconds (75));
        auto c3_fast_timer_start =
          observer.send (yd::timer_start_command_t{.request_id = c3_actor_then_timer,
                                                   .timer_name = c3_actor_then_timer + "-fast",
                                                   .mode = "fast",
                                                   .period_ms = 50,
                                                   .delay_ms = 0})
            .packet_name (yd::timer_start_command_t::packet_name)
            .metadata (yd::spot_rid_metadata, spot_rid)
            .submit ();
        ensure (static_cast<bool> (c3_fast_timer_start), "YD-C3A fast timer start failed");
        auto c3_fast_timer_completed =
          observer.request (
                    yd::yield_evidence_wait_req_t{.request_id = c3_actor_then_timer,
                                                  .marker = "timer-fast-completed",
                                                  .timeout_milliseconds = 20000})
            .packet_name (yd::yield_evidence_wait_req_t::packet_name)
            .metadata (yd::target_node_rid_metadata, "play-a")
            .timeout (std::chrono::milliseconds (30000))
            .submit<yd::yield_evidence_reply_t> ();
        ensure (static_cast<bool> (c3_fast_timer_completed),
                "YD-C3A timer-fast-completed wait failed");
        auto c3_fast_timer_stop =
          observer.send (yd::timer_stop_command_t{.request_id = c3_actor_then_timer})
            .packet_name (yd::timer_stop_command_t::packet_name)
            .metadata (yd::spot_rid_metadata, spot_rid)
            .submit ();
        ensure (static_cast<bool> (c3_fast_timer_stop), "YD-C3A timer stop failed");
        auto actor_yield_c3_reply = actor_yield_c3.get ();
        ensure (static_cast<bool> (actor_yield_c3_reply), "YD-C3A ActorYieldReq failed");
        auto c3_actor_timer_evidence =
          client.request (yd::yield_evidence_req_t{.request_id = c3_actor_then_timer})
            .packet_name (yd::yield_evidence_req_t::packet_name)
            .metadata (yd::target_node_rid_metadata, "play-a")
            .timeout (std::chrono::milliseconds (30000))
            .submit<yd::yield_evidence_reply_t> ();
        ensure (static_cast<bool> (c3_actor_timer_evidence),
                "YD-C3A evidence request failed");
        ensure_contains_in_order (c3_actor_timer_evidence.value ().evidence,
                                  c3_actor_then_timer,
                                  {"actor-yield-started",
                                   "actor-yield-released",
                                   "timer-fast-started",
                                   "timer-fast-completed",
                                   "actor-yield-resumed",
                                   "actor-yield-completed"},
                                  "YD-C3A marker order mismatch");

        const auto c3_timer_then_actor = unique_id ("YD-C3B");
        auto c3_yield_timer_start =
          client.send (yd::timer_start_command_t{.request_id = c3_timer_then_actor,
                                                 .timer_name = c3_timer_then_actor + "-yield",
                                                 .mode = "yield-on-first",
                                                 .period_ms = 500,
                                                 .delay_ms = 350})
            .packet_name (yd::timer_start_command_t::packet_name)
            .metadata (yd::spot_rid_metadata, spot_rid)
            .submit ();
        ensure (static_cast<bool> (c3_yield_timer_start), "YD-C3B yield timer start failed");
        auto c3_yield_timer_released =
          observer.request (
                    yd::yield_evidence_wait_req_t{.request_id = c3_timer_then_actor,
                                                  .marker = "timer-yield-released",
                                                  .timeout_milliseconds = 20000})
            .packet_name (yd::yield_evidence_wait_req_t::packet_name)
            .metadata (yd::target_node_rid_metadata, "play-a")
            .timeout (std::chrono::milliseconds (30000))
            .submit<yd::yield_evidence_reply_t> ();
        ensure (static_cast<bool> (c3_yield_timer_released),
                "YD-C3B timer-yield-released wait failed");
        auto c3_actor_fast =
          client.request (yd::actor_fast_req_t{.request_id = c3_timer_then_actor,
                                               .marker = "c3-actor-fast"})
            .packet_name (yd::actor_fast_req_t::packet_name)
            .metadata (yd::actor_id_metadata, actors.actor_b)
            .timeout (std::chrono::milliseconds (30000))
            .submit<yd::actor_yield_reply_t> ();
        ensure (static_cast<bool> (c3_actor_fast), "YD-C3B ActorFastReq failed");
        auto c3_timer_actor_evidence =
          observer.request (
                    yd::yield_evidence_wait_req_t{.request_id = c3_timer_then_actor,
                                                  .marker = "timer-yield-completed",
                                                  .timeout_milliseconds = 20000})
            .packet_name (yd::yield_evidence_wait_req_t::packet_name)
            .metadata (yd::target_node_rid_metadata, "play-a")
            .timeout (std::chrono::milliseconds (30000))
            .submit<yd::yield_evidence_reply_t> ();
        ensure (static_cast<bool> (c3_timer_actor_evidence),
                "YD-C3B evidence wait failed");
        auto c3_yield_timer_stop =
          client.send (yd::timer_stop_command_t{.request_id = c3_timer_then_actor})
            .packet_name (yd::timer_stop_command_t::packet_name)
            .metadata (yd::spot_rid_metadata, spot_rid)
            .submit ();
        ensure (static_cast<bool> (c3_yield_timer_stop), "YD-C3B timer stop failed");
        ensure_contains_in_order (c3_timer_actor_evidence.value ().evidence,
                                  c3_timer_then_actor,
                                  {"timer-yield-started",
                                   "timer-yield-released",
                                   "actor-fast-started",
                                   "actor-fast-completed",
                                   "timer-yield-resumed",
                                   "timer-yield-completed"},
                                  "YD-C3B marker order mismatch");
        std::cout << "scenario YD-C3 passed\n";

        const auto remote_owner_spot = unique_id ("yield-remote-owner");
        const auto remote_target_spot = unique_id ("yield-remote-target");
        auto remote_owner =
          client.request (yd::ensure_spot_req_t{.spot_rid = remote_owner_spot})
            .packet_name (yd::ensure_spot_req_t::packet_name)
            .timeout (std::chrono::milliseconds (15000))
            .submit<yd::ensure_spot_reply_t> ();
        ensure (static_cast<bool> (remote_owner), "YD-D2 owner spot ensure failed");
        auto remote_target =
          client.request (yd::ensure_spot_req_t{.spot_rid = remote_target_spot})
            .packet_name (yd::ensure_spot_req_t::packet_name)
            .metadata (yd::target_node_rid_metadata, "play-b")
            .timeout (std::chrono::milliseconds (15000))
            .submit<yd::ensure_spot_reply_t> ();
        ensure (static_cast<bool> (remote_target), "YD-D2 target spot ensure failed");
        const auto d2 = unique_id ("YD-D2");
        auto remote_reply =
          client.request (yd::remote_spot_yield_req_t{.request_id = d2,
                                                      .target_spot_rid = remote_target_spot,
                                                      .delay_ms = 350})
            .packet_name (yd::remote_spot_yield_req_t::packet_name)
            .metadata (yd::spot_rid_metadata, remote_owner_spot)
            .timeout (std::chrono::milliseconds (30000))
            .submit<yd::yield_dispatch_reply_t> ();
        ensure (static_cast<bool> (remote_reply), "YD-D2 RemoteSpotYieldReq failed");
        ensure (remote_reply.value ().scenario_id == "YD-D2",
                "YD-D2 reply scenario mismatch");
        ensure (remote_reply.value ().node_rid == "play-a",
                "YD-D2 caller continuation node mismatch");
        auto d2_owner_evidence =
          wait_for_evidence_snapshot (client, d2, "remote-yield-completed", "play-a",
                                      std::chrono::milliseconds (20000));
        ensure (static_cast<bool> (d2_owner_evidence), "YD-D2 owner evidence wait failed");
        ensure_contains_in_order (d2_owner_evidence.value ().evidence, d2,
                                  {"remote-yield-started",
                                   "remote-yield-released",
                                   "remote-yield-resumed",
                                   "remote-yield-completed"},
                                  "YD-D2 owner marker order mismatch");
        ensure (every_request_line_has (d2_owner_evidence.value ().evidence, d2,
                                        {"rid=play-a"}),
                "YD-D2 owner evidence node mismatch");
        bool saw_target_node = false;
        for (const auto &line : d2_owner_evidence.value ().evidence) {
            if (line.find ("remote-yield-resumed") != std::string::npos
                && line.find ("targetNode=play-b") != std::string::npos) {
                saw_target_node = true;
            }
        }
        ensure (saw_target_node, "YD-D2 target node marker missing");
        auto d2_target_evidence =
          client.request (yd::yield_evidence_req_t{.request_id = d2})
            .packet_name (yd::yield_evidence_req_t::packet_name)
            .metadata (yd::target_node_rid_metadata, "play-b")
            .timeout (std::chrono::milliseconds (30000))
            .submit<yd::yield_evidence_reply_t> ();
        ensure (static_cast<bool> (d2_target_evidence), "YD-D2 target evidence request failed");
        bool saw_target_yield = false;
        for (const auto &line : d2_target_evidence.value ().evidence) {
            if (line.find ("yield-started|rid=play-b") != std::string::npos
                && line.find ("spot=" + remote_target_spot) != std::string::npos) {
                saw_target_yield = true;
            }
            ensure (line.find ("remote-yield-resumed|rid=play-b") == std::string::npos,
                    "YD-D2 target node owned caller continuation");
        }
        ensure (saw_target_yield, "YD-D2 target play-b marker missing");
        std::cout << "scenario YD-D2 passed\n";

        (void) observer.close ();
        (void) client.close ();
        std::cout << "yield-dispatch track-a-d result=passed\n";
        return 0;
    }
    catch (const std::exception &error) {
        std::cerr << "yield-dispatch client failed: " << error.what () << "\n";
        return 1;
    }
}
