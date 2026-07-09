/* SPDX-License-Identifier: MPL-2.0 */

#pragma once

#include "play_basic_spot_handlers.hpp"
#include "../Support/play_support.hpp"
#include "../../../Shared/yield_dispatch_contracts.hpp"

#include <zlink/framework.hpp>

#include <chrono>
#include <string>

namespace zlink::framework::e2e::yield_dispatch::server::play {

namespace yd = zlink::framework::e2e::yield_dispatch;

inline zlink::framework::task_t<yd::yield_dispatch_res_t>
handle_remote_spot_yield (zlink::framework::spot_context_t &context,
                          evidence_store_t &evidence,
                          const yd::remote_spot_yield_req_t &request)
{
    const auto spot_rid = std::string (context.spot_rid ().value ());
    evidence.add ("remote-yield-started|rid=" + evidence.node_rid + "|spot="
                  + spot_rid + "|request=" + request.request_id + "|target="
                  + request.target_spot_rid + "|handler=spot");
    auto call =
      context
        .request_to<yd::yield_dispatch_res_t> (
          zlink::framework::spot_ref_t{
            .mesh_name = yd::spot_channel,
            .node_rid = zlink::routing_id_t::from ("play-b"),
            .spot_rid = zlink::routing_id_t::from (request.target_spot_rid)},
          yd::yield_req_t{.request_id = request.request_id,
                          .delay_ms = request.delay_ms,
                          .correlation_id = "remote-spot"})
        .packet_name (yd::yield_req_t::packet_name)
        .timeout (std::chrono::milliseconds (5000));
    evidence.add ("remote-yield-released|rid=" + evidence.node_rid + "|spot="
                  + spot_rid + "|request=" + request.request_id + "|target="
                  + request.target_spot_rid + "|handler=spot");
    auto target_reply = co_await call.yield ();
    evidence.add ("remote-yield-resumed|rid=" + evidence.node_rid + "|spot="
                  + spot_rid + "|request=" + request.request_id + "|target="
                  + request.target_spot_rid + "|targetNode=" + target_reply.node_rid
                  + "|handler=spot");
    evidence.add ("remote-yield-completed|rid=" + evidence.node_rid + "|spot="
                  + spot_rid + "|request=" + request.request_id + "|target="
                  + request.target_spot_rid + "|targetNode=" + target_reply.node_rid
                  + "|handler=spot");
    co_return basic_spot_reply (context, evidence, "YD-D2", request.request_id,
                                "remote-yield-completed");
}

} // namespace zlink::framework::e2e::yield_dispatch::server::play
