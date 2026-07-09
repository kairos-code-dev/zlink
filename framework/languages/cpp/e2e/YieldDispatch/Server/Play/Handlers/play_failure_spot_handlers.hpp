/* SPDX-License-Identifier: MPL-2.0 */

#pragma once

#include "../Support/play_support.hpp"
#include "../../../Shared/yield_dispatch_contracts.hpp"

#include <zlink/framework.hpp>

#include <chrono>
#include <string>
#include <thread>

namespace zlink::framework::e2e::yield_dispatch::server::play
{

namespace yd = zlink::framework::e2e::yield_dispatch;

inline std::string timeout_error_name (const zlink::framework::framework_exception_t &error)
{
    if (error.kind () == zlink::framework::framework_error_kind_t::timeout) {
        return "Timeout";
    }
    return error.what ();
}

inline std::string cancellation_error_name (const zlink::framework::framework_exception_t &error)
{
    if (error.kind () == zlink::framework::framework_error_kind_t::cancelled) {
        return "Cancelled";
    }
    return error.what ();
}

inline yd::yield_timeout_res_t
timeout_reply (const zlink::framework::spot_context_t &context,
               const evidence_store_t &evidence,
               const yd::yield_timeout_req_t &request,
               bool timed_out,
               std::string error)
{
    return {.scenario_id = "YD-E1",
            .request_id = request.request_id,
            .spot_rid = std::string (context.spot_rid ().value ()),
            .node_rid = evidence.node_rid,
            .timed_out = timed_out,
            .error = std::move (error)};
}

inline zlink::framework::task_t<yd::yield_timeout_res_t>
handle_yield_timeout (zlink::framework::spot_context_t &context,
                      evidence_store_t &evidence,
                      const yd::yield_timeout_req_t &request)
{
    const auto spot_rid = std::string (context.spot_rid ().value ());
    evidence.add ("timeout-yield-started|rid=" + evidence.node_rid + "|spot=" + spot_rid
                  + "|request=" + request.request_id + "|handler=spot");
    try {
        auto call =
          context.outbound ()
            .request (yd::delay_channel,
                      yd::delay_req_t{.request_id = request.request_id,
                                      .delay_ms = request.delay_ms,
                                      .marker = "timeout"})
            .packet_name (yd::delay_req_t::packet_name)
            .timeout (std::chrono::milliseconds (request.timeout_ms));
        evidence.add ("timeout-yield-released|rid=" + evidence.node_rid + "|spot=" + spot_rid
                      + "|request=" + request.request_id + "|handler=spot");
        co_await call.yield<yd::delay_res_t> ();
        evidence.add ("timeout-yield-unexpected-resumed|rid=" + evidence.node_rid
                      + "|spot=" + spot_rid + "|request=" + request.request_id
                      + "|handler=spot");
        co_return timeout_reply (context, evidence, request, false, "");
    }
    catch (const zlink::framework::framework_exception_t &error) {
        evidence.add ("timeout-yield-completed|rid=" + evidence.node_rid + "|spot=" + spot_rid
                      + "|request=" + request.request_id + "|error="
                      + timeout_error_name (error) + "|handler=spot");
        co_return timeout_reply (context, evidence, request, true,
                                 timeout_error_name (error));
    }
}

inline zlink::framework::task_t<void>
handle_yield_timeout_command (zlink::framework::spot_context_t &context,
                              evidence_store_t &evidence,
                              const yd::yield_timeout_msg_t &request)
{
    const yd::yield_timeout_req_t as_request{.request_id = request.request_id,
                                             .delay_ms = request.delay_ms,
                                             .timeout_ms = request.timeout_ms};
    (void) co_await handle_yield_timeout (context, evidence, as_request);
}

inline yd::yield_cancel_res_t
cancel_reply (const zlink::framework::spot_context_t &context,
              const evidence_store_t &evidence,
              const yd::yield_cancel_req_t &request,
              bool cancelled,
              std::string error)
{
    return {.scenario_id = "YD-E2",
            .request_id = request.request_id,
            .spot_rid = std::string (context.spot_rid ().value ()),
            .node_rid = evidence.node_rid,
            .canceled = cancelled,
            .error = std::move (error)};
}

inline zlink::framework::task_t<yd::yield_cancel_res_t>
handle_yield_cancel (zlink::framework::spot_context_t &context,
                     evidence_store_t &evidence,
                     const yd::yield_cancel_req_t &request)
{
    const auto spot_rid = std::string (context.spot_rid ().value ());
    evidence.add ("cancel-yield-started|rid=" + evidence.node_rid + "|spot=" + spot_rid
                  + "|request=" + request.request_id + "|handler=spot");
    zlink::framework::cancellation_token_source_t cancellation;
    std::thread ([cancellation, delay = request.cancel_after_ms] () mutable {
        std::this_thread::sleep_for (std::chrono::milliseconds (delay));
        (void) cancellation.cancel ();
    }).detach ();
    try {
        auto call =
          context.outbound ()
            .request (yd::delay_channel,
                      yd::delay_req_t{.request_id = request.request_id,
                                      .delay_ms = request.delay_ms,
                                      .marker = "cancel"})
            .packet_name (yd::delay_req_t::packet_name)
            .timeout (std::chrono::milliseconds (5000));
        evidence.add ("cancel-yield-released|rid=" + evidence.node_rid + "|spot=" + spot_rid
                      + "|request=" + request.request_id + "|handler=spot");
        co_await call.yield<yd::delay_res_t> (cancellation.token ());
        evidence.add ("cancel-yield-unexpected-resumed|rid=" + evidence.node_rid
                      + "|spot=" + spot_rid + "|request=" + request.request_id
                      + "|handler=spot");
        co_return cancel_reply (context, evidence, request, false, "");
    }
    catch (const zlink::framework::framework_exception_t &error) {
        evidence.add ("cancel-yield-completed|rid=" + evidence.node_rid + "|spot=" + spot_rid
                      + "|request=" + request.request_id + "|error="
                      + cancellation_error_name (error) + "|handler=spot");
        co_return cancel_reply (context, evidence, request, true,
                                cancellation_error_name (error));
    }
}

inline zlink::framework::task_t<void>
handle_yield_cancel_command (zlink::framework::spot_context_t &context,
                             evidence_store_t &evidence,
                             const yd::yield_cancel_msg_t &request)
{
    const yd::yield_cancel_req_t as_request{.request_id = request.request_id,
                                            .delay_ms = request.delay_ms,
                                            .cancel_after_ms = request.cancel_after_ms};
    (void) co_await handle_yield_cancel (context, evidence, as_request);
}

} // namespace zlink::framework::e2e::yield_dispatch::server::play
