/* SPDX-License-Identifier: MPL-2.0 */

#include "timer_runtime.hpp"

#include "runtime/spots/spot_runtime.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace zlink::framework
{

timer_t::timer_t () : _state (std::make_shared<detail::timer_state_t> ()) {}

timer_t::timer_t (std::shared_ptr<detail::timer_state_t> state)
  : _state (std::move (state))
{
}

timer_t::~timer_t () = default;
timer_t::timer_t (timer_t &&) noexcept = default;
timer_t &timer_t::operator= (timer_t &&) noexcept = default;

bool
timer_t::is_disposed () const noexcept
{
  return !_state || _state->disposed;
}

void
timer_t::cancel () noexcept
{
  if (_state) {
    if (_state->native_timer && _state->native_timer->valid ()) {
      try {
        _state->native_timer->stop ();
      } catch (...) {
      }
    }
    _state->disposed = true;
  }
}

timer_t
spot_context_t::add_timer_erased (std::string name,
                                  std::chrono::milliseconds period,
                                  timer_options_t options,
                                  std::type_index handler_type)
{
  if (name.empty ()) {
    throw framework_exception_t (
      framework_error_kind_t::request_protocol_error,
      "SPOT timer name must not be empty");
  }
  if (period <= std::chrono::milliseconds::zero ()) {
    throw framework_exception_t (
      framework_error_kind_t::request_protocol_error,
      "SPOT timer period must be greater than zero");
  }
  if (options.overrun_policy == timer_overrun_policy_t::catch_up_bounded &&
      options.max_catch_up_ticks == 0) {
    throw framework_exception_t (
      framework_error_kind_t::request_protocol_error,
      "SPOT timer max catch-up ticks must be greater than zero");
  }

  const auto duplicate = std::any_of (
    _state->timers.begin (),
    _state->timers.end (),
    [&](const std::shared_ptr<detail::timer_state_t> &timer) {
      return timer->name == name && !timer->disposed;
    });
  if (duplicate) {
    throw framework_exception_t (
      framework_error_kind_t::request_protocol_error,
      "duplicate SPOT timer registration");
  }

  auto state = std::make_shared<detail::timer_state_t> ();
  state->name = std::move (name);
  state->period = period;
  state->options = options;
  state->handler_type = handler_type;
  state->native_timer = std::make_unique<zlink::timer_t> ();
  state->native_timer->start (period);
  _state->timers.push_back (state);
  return timer_t (state);
}

} // namespace zlink::framework

namespace zlink::framework::detail
{

timer_runtime_t::timer_runtime_t (std::shared_ptr<spot_context_state_t> context)
  : _context (std::move (context))
{
}

timer_runtime_t
timer_runtime_t::from (spot_context_t &context)
{
  return timer_runtime_t (context._state);
}

namespace
{

std::uint64_t
select_scheduled_index (const timer_state_t &state, std::uint64_t fire_count)
{
  if (state.options.overrun_policy == timer_overrun_policy_t::delay_next_tick) {
    return state.last_scheduled_index + 1;
  }

  const auto due = state.last_scheduled_index + std::max<std::uint64_t> (
                                                1, fire_count);
  if (state.options.overrun_policy == timer_overrun_policy_t::skip_late_ticks) {
    return due;
  }

  const auto available = due - state.last_scheduled_index;
  if (available > state.options.max_catch_up_ticks) {
    return due - state.options.max_catch_up_ticks + 1;
  }
  return state.last_scheduled_index + 1;
}

timer_tick_t
make_tick (timer_state_t &state, std::uint64_t fire_count)
{
  const auto scheduled_index = select_scheduled_index (state, fire_count);
  const auto skipped_ticks = scheduled_index - state.last_scheduled_index - 1;
  state.delivery_index++;

  const auto scheduled_elapsed = state.period * scheduled_index;
  const auto started_elapsed =
    state.options.overrun_policy == timer_overrun_policy_t::delay_next_tick
      ? state.period * state.delivery_index
      : state.period * (state.last_scheduled_index +
                        std::max<std::uint64_t> (1, fire_count));

  timer_tick_t tick {
    state.name,
    state.delivery_index,
    scheduled_index,
    state.period,
    scheduled_elapsed,
    started_elapsed,
    started_elapsed - scheduled_elapsed,
    skipped_ticks };
  state.last_scheduled_index = scheduled_index;
  state.delivered_ticks.push_back (tick);
  return tick;
}

} // namespace

result_t<timer_tick_t>
timer_runtime_t::dispatch_fire_count (
  timer_t &timer,
  std::uint64_t fire_count,
  std::function<void (const timer_tick_t &)> handler) const
{
  if (timer.is_disposed ()) {
    return result_t<timer_tick_t>::failure (
      framework_error_kind_t::closed, "SPOT timer is disposed");
  }
  if (timer._state->running) {
    return result_t<timer_tick_t>::failure (
      framework_error_kind_t::request_rejected,
      "SPOT timer callback is already running");
  }

  timer._state->running = true;
  auto reset_running = [&timer] { timer._state->running = false; };

  try {
    auto tick = make_tick (*timer._state, fire_count);
    if (handler) {
      handler (tick);
    }
    reset_running ();
    return result_t<timer_tick_t>::success (std::move (tick));
  } catch (const std::exception &error) {
    const auto stopped = timer._state->options.stop_on_unhandled_exception;
    timer._state->failure_events.push_back (timer_failure_event_t {
      timer._state->name,
      timer._state->handler_type,
      timer._state->delivery_index,
      stopped,
      error.what () });
    if (stopped) {
      timer._state->disposed = true;
    }
    reset_running ();
    return result_t<timer_tick_t>::failure (
      framework_error_kind_t::request_failed, error.what ());
  } catch (...) {
    const auto stopped = timer._state->options.stop_on_unhandled_exception;
    timer._state->failure_events.push_back (timer_failure_event_t {
      timer._state->name,
      timer._state->handler_type,
      timer._state->delivery_index,
      stopped,
      "unknown timer handler failure" });
    if (stopped) {
      timer._state->disposed = true;
    }
    reset_running ();
    return result_t<timer_tick_t>::failure (
      framework_error_kind_t::request_failed,
      "unknown timer handler failure");
  }
}

void
timer_runtime_t::cancel_all () const noexcept
{
  for (const auto &timer : _context->timers) {
    if (timer->native_timer && timer->native_timer->valid ()) {
      try {
        timer->native_timer->stop ();
      } catch (...) {
      }
    }
    timer->disposed = true;
    timer->running = false;
  }
}

std::vector<timer_failure_event_t>
timer_runtime_t::failure_events (const timer_t &timer) const
{
  if (!timer._state) {
    return {};
  }
  return timer._state->failure_events;
}

std::vector<timer_tick_t>
timer_runtime_t::delivered_ticks (const timer_t &timer) const
{
  if (!timer._state) {
    return {};
  }
  return timer._state->delivered_ticks;
}

} // namespace zlink::framework::detail
