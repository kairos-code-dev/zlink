/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/framework/contracts/spots/spot.hpp>
#include <zlink/framework/contracts/timers/timer.hpp>
#include <zlink/Contracts/Eventing/timers.hpp>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace zlink::framework::detail
{

class timer_state_t
{
  public:
    std::string name;
    std::chrono::milliseconds period{0};
    timer_options_t options;
    std::type_index handler_type{typeid (void)};
    std::unique_ptr<zlink::timer_t> native_timer;
    std::uint64_t delivery_index = 0;
    std::uint64_t last_scheduled_index = 0;
    bool disposed = false;
    bool running = false;
    std::vector<timer_tick_t> delivered_ticks;
    std::vector<timer_failure_event_t> failure_events;
};

class timer_runtime_t
{
  public:
    static timer_runtime_t from (spot_context_t &context);

    result_t<timer_tick_t>
    dispatch_fire_count (timer_t &timer,
                         std::uint64_t fire_count,
                         std::function<void (const timer_tick_t &)> handler = {}) const;

    void cancel_all () const noexcept;
    std::vector<timer_failure_event_t> failure_events (const timer_t &timer) const;
    std::vector<timer_tick_t> delivered_ticks (const timer_t &timer) const;

  private:
    explicit timer_runtime_t (std::shared_ptr<spot_context_state_t> context);

    std::shared_ptr<spot_context_state_t> _context;
};

} // namespace zlink::framework::detail
