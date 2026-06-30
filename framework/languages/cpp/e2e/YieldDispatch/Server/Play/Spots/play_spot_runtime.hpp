/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "play_spot_types.hpp"
#include "../Handlers/play_basic_spot_handlers.hpp"
#include "../Handlers/play_failure_spot_handlers.hpp"
#include "../Handlers/play_remote_spot_handlers.hpp"
#include "../Handlers/play_timer_spot_handlers.hpp"
#include "../Support/play_support.hpp"

#include <zlink/framework.hpp>

#include <chrono>
#include <map>
#include <mutex>
#include <string>

namespace zlink::framework::e2e::yield_dispatch::server::play
{

namespace yd = zlink::framework::e2e::yield_dispatch;

class yield_probe_spot_t : public zlink::framework::spot_t
{
  public:
    explicit yield_probe_spot_t (evidence_store_t &evidence) : _evidence (evidence) {}

    void configure (zlink::framework::spot_context_t &context)
    {
        _context = context;
        context.handlers ()
          .add_handler<&yield_probe_spot_t::hold_req> (yd::hold_req_t::packet_name)
          .add_handler<&yield_probe_spot_t::hold_command> (yd::hold_command_t::packet_name)
          .add_handler<&yield_probe_spot_t::yield_req> (yd::yield_req_t::packet_name)
          .add_handler<&yield_probe_spot_t::yield_command> (yd::yield_command_t::packet_name)
          .add_handler<&yield_probe_spot_t::worker_yield_req> (
            yd::worker_yield_req_t::packet_name)
          .add_handler<&yield_probe_spot_t::worker_yield_command> (
            yd::worker_yield_command_t::packet_name)
          .add_handler<&yield_probe_spot_t::yield_timeout_req> (
            yd::yield_timeout_req_t::packet_name)
          .add_handler<&yield_probe_spot_t::yield_timeout_command> (
            yd::yield_timeout_command_t::packet_name)
          .add_handler<&yield_probe_spot_t::remote_spot_yield_req> (
            yd::remote_spot_yield_req_t::packet_name)
          .add_handler<&yield_probe_spot_t::timer_start_command> (
            yd::timer_start_command_t::packet_name)
          .add_handler<&yield_probe_spot_t::timer_stop_command> (
            yd::timer_stop_command_t::packet_name)
          .add_handler<&yield_probe_spot_t::probe_req> (yd::probe_req_t::packet_name)
          .add_handler<&yield_probe_spot_t::probe_command> (yd::probe_command_t::packet_name);
    }

    zlink::framework::task_t<yd::yield_dispatch_reply_t> hold_req (const yd::hold_req_t &request)
    {
        co_await handle_basic_hold (_context, _evidence, request.request_id, request.delay_ms);
        co_return basic_spot_reply (_context, _evidence, "YD-A1", request.request_id,
                                    "hold-completed");
    }

    zlink::framework::task_t<void> hold_command (const yd::hold_command_t &request)
    {
        co_await handle_basic_hold (_context, _evidence, request.request_id, request.delay_ms);
    }

    zlink::framework::task_t<yd::yield_dispatch_reply_t> yield_req (const yd::yield_req_t &request)
    {
        co_await handle_basic_yield (_context, _evidence, request.request_id, request.delay_ms,
                                     request.correlation_id);
        co_return basic_spot_reply (_context, _evidence, "YD-A2", request.request_id,
                                    "yield-completed");
    }

    zlink::framework::task_t<void> yield_command (const yd::yield_command_t &request)
    {
        co_await handle_basic_yield (_context, _evidence, request.request_id, request.delay_ms,
                                     request.correlation_id);
    }

    zlink::framework::task_t<yd::yield_dispatch_reply_t>
    worker_yield_req (const yd::worker_yield_req_t &request)
    {
        co_await handle_basic_worker_yield (_context, _evidence, request.request_id,
                                            request.delay_ms);
        co_return basic_spot_reply (_context, _evidence, "YD-A4", request.request_id,
                                    "worker-yield-completed");
    }

    zlink::framework::task_t<void> worker_yield_command (
      const yd::worker_yield_command_t &request)
    {
        co_await handle_basic_worker_yield (_context, _evidence, request.request_id,
                                            request.delay_ms);
    }

    zlink::framework::task_t<yd::yield_timeout_reply_t>
    yield_timeout_req (const yd::yield_timeout_req_t &request)
    {
        co_return co_await handle_yield_timeout (_context, _evidence, request);
    }

    zlink::framework::task_t<void> yield_timeout_command (
      const yd::yield_timeout_command_t &request)
    {
        co_await handle_yield_timeout_command (_context, _evidence, request);
    }

    zlink::framework::task_t<yd::yield_dispatch_reply_t>
    remote_spot_yield_req (const yd::remote_spot_yield_req_t &request)
    {
        co_return co_await handle_remote_spot_yield (_context, _evidence, request);
    }

    void timer_start_command (const yd::timer_start_command_t &request)
    {
        handle_timer_start_command (_context, _evidence, _timers, _timer_mutex, request);
    }

    void timer_stop_command (const yd::timer_stop_command_t &request)
    {
        handle_timer_stop_command (_timers, _timer_mutex, request);
    }

    yd::yield_dispatch_reply_t probe_req (const yd::probe_req_t &request)
    {
        handle_basic_probe (_context, _evidence, request.request_id, request.marker);
        return basic_spot_reply (_context, _evidence, "YD-PROBE", request.request_id,
                                 request.marker);
    }

    void probe_command (const yd::probe_command_t &request)
    {
        handle_basic_probe (_context, _evidence, request.request_id, request.marker);
    }

    zlink::framework::task_t<void> handle_timer_tick (
      const zlink::framework::timer_tick_t &tick)
    {
        co_await play::handle_timer_tick (_context, _evidence, _timers, _timer_mutex, tick);
        co_return;
    }

  private:
    evidence_store_t &_evidence;
    zlink::framework::spot_context_t _context;
    std::mutex _timer_mutex;
    std::map<std::string, yield_timer_state_t> _timers;
};

inline zlink::framework::task_t<void>
yield_timer_handler_t::handle (yield_probe_spot_t &spot,
                               const zlink::framework::timer_tick_t &tick) const
{
    co_await spot.handle_timer_tick (tick);
}

} // namespace zlink::framework::e2e::yield_dispatch::server::play
