/* SPDX-License-Identifier: MPL-2.0 */

#pragma once

#include "../Support/play_support.hpp"
#include "../../../Shared/yield_dispatch_contracts.hpp"

#include <zlink/framework.hpp>

#include <chrono>
#include <string>
#include <thread>
#include <utility>

namespace zlink::framework::e2e::yield_dispatch::server::play {

namespace yd = zlink::framework::e2e::yield_dispatch;

inline yd::yield_dispatch_reply_t
basic_spot_reply (const zlink::framework::spot_context_t &context,
                  const evidence_store_t &evidence,
                  std::string scenario_id,
                  std::string request_id,
                  std::string marker)
{
    return {.scenario_id = std::move (scenario_id),
            .request_id = std::move (request_id),
            .spot_rid = std::string (context.spot_rid ().value ()),
            .node_rid = evidence.node_rid,
            .marker = std::move (marker)};
}

inline zlink::framework::task_t<void>
handle_basic_hold (zlink::framework::spot_context_t &context,
                   evidence_store_t &evidence,
                   const std::string &request_id,
                   int delay_ms)
{
    const auto spot_rid = std::string (context.spot_rid ().value ());
    evidence.add ("hold-started|rid=" + evidence.node_rid + "|spot=" + spot_rid
                  + "|request=" + request_id + "|handler=spot");
    co_await context.outbound ()
      .request (yd::delay_channel,
                yd::delay_req_t{.request_id = request_id,
                                .delay_ms = delay_ms,
                                .marker = "hold"})
      .packet_name (yd::delay_req_t::packet_name)
      .timeout (std::chrono::milliseconds (5000))
      .async<yd::delay_reply_t> ();
    evidence.add ("hold-resumed|rid=" + evidence.node_rid + "|spot=" + spot_rid
                  + "|request=" + request_id + "|handler=spot");
    evidence.add ("hold-completed|rid=" + evidence.node_rid + "|spot=" + spot_rid
                  + "|request=" + request_id + "|handler=spot");
    co_return;
}

inline zlink::framework::task_t<void>
handle_basic_yield (zlink::framework::spot_context_t &context,
                    evidence_store_t &evidence,
                    const std::string &request_id,
                    int delay_ms,
                    const std::string &correlation_id)
{
    const auto spot_rid = std::string (context.spot_rid ().value ());
    evidence.add ("yield-started|rid=" + evidence.node_rid + "|spot=" + spot_rid
                  + "|request=" + request_id + "|correlation="
                  + correlation_id + "|handler=spot");
    auto call =
      context.outbound ()
        .request (yd::delay_channel,
                  yd::delay_req_t{.request_id = request_id,
                                  .delay_ms = delay_ms,
                                  .marker = "yield"})
        .packet_name (yd::delay_req_t::packet_name)
        .timeout (std::chrono::milliseconds (5000));
    evidence.add ("yield-released|rid=" + evidence.node_rid + "|spot=" + spot_rid
                  + "|request=" + request_id + "|correlation="
                  + correlation_id + "|handler=spot");
    co_await call.yield<yd::delay_reply_t> ();
    evidence.add ("yield-resumed|rid=" + evidence.node_rid + "|spot=" + spot_rid
                  + "|request=" + request_id + "|correlation="
                  + correlation_id + "|handler=spot");
    evidence.add ("yield-completed|rid=" + evidence.node_rid + "|spot=" + spot_rid
                  + "|request=" + request_id + "|correlation="
                  + correlation_id + "|handler=spot");
    co_return;
}

inline zlink::framework::task_t<void>
handle_basic_worker_yield (zlink::framework::spot_context_t &context,
                           evidence_store_t &evidence,
                           const std::string &request_id,
                           int delay_ms)
{
    const auto spot_rid = std::string (context.spot_rid ().value ());
    evidence.add ("worker-yield-started|rid=" + evidence.node_rid + "|spot=" + spot_rid
                  + "|request=" + request_id + "|handler=spot");
    auto call = context.run_worker ([request_id, delay_ms] {
        std::this_thread::sleep_for (std::chrono::milliseconds (delay_ms));
        return request_id;
    });
    evidence.add ("worker-yield-released|rid=" + evidence.node_rid + "|spot=" + spot_rid
                  + "|request=" + request_id + "|handler=spot");
    co_await call.yield ();
    evidence.add ("worker-yield-resumed|rid=" + evidence.node_rid + "|spot=" + spot_rid
                  + "|request=" + request_id + "|handler=spot");
    evidence.add ("worker-yield-completed|rid=" + evidence.node_rid + "|spot=" + spot_rid
                  + "|request=" + request_id + "|handler=spot");
    co_return;
}

inline void
handle_basic_probe (zlink::framework::spot_context_t &context,
                    evidence_store_t &evidence,
                    const std::string &request_id,
                    const std::string &marker)
{
    const auto spot_rid = std::string (context.spot_rid ().value ());
    evidence.add ("probe-started|rid=" + evidence.node_rid + "|spot=" + spot_rid
                  + "|request=" + request_id + "|marker=" + marker
                  + "|handler=spot");
    evidence.add ("probe-completed|rid=" + evidence.node_rid + "|spot=" + spot_rid
                  + "|request=" + request_id + "|marker=" + marker
                  + "|handler=spot");
}

} // namespace zlink::framework::e2e::yield_dispatch::server::play
