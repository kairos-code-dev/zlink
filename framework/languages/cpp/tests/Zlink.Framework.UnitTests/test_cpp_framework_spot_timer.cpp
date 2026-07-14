/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#include <zlink/framework.hpp>

#include "runtime/timers/timer_runtime.hpp"

#include <chrono>
#include <future>
#include <stdexcept>
#include <thread>

namespace
{

struct stage_spot_t : public zlink::framework::spot_t
{
};

struct entry_spot_t : public zlink::framework::entry_spot_t
{
};

struct tick_handler_t
{
    void handle (zlink::framework::spot_t &, const zlink::framework::timer_tick_t &) const {}
};

struct failing_tick_handler_t
{
    void handle (zlink::framework::spot_t &, const zlink::framework::timer_tick_t &) const {}
};

} // namespace

int main ()
{
    using namespace std::chrono_literals;
    using zlink::framework::framework_error_kind_t;
    using zlink::framework::timer_overrun_policy_t;

    zlink::framework::spot_node_builder_t builder;
    zlink::framework::zlink_builder_t host;
    builder = host.add_spot_node ("timer-node");
    builder.add_entry_spot<entry_spot_t> ().add_spot<stage_spot_t> ("stage");

    auto entry_context = builder.create_spot ("entry").context;
    auto context = builder.create_spot ("stage").context;

    zlink::framework::timer_t detached_timer;
    if (detached_timer.is_disposed ()) {
        return 16;
    }
    detached_timer.cancel ();
    if (!detached_timer.is_disposed ()) {
        return 17;
    }

    auto timer = context.add_timer<tick_handler_t> (
      "stage-tick", 16ms, {.overrun_policy = timer_overrun_policy_t::skip_late_ticks});
    if (timer.is_disposed ()) {
        return 1;
    }

    auto runtime = zlink::framework::detail::timer_runtime_t::from (context);
    int callback_count = 0;
    auto first = runtime.dispatch_fire_count (
      timer, 3, [&callback_count] (const zlink::framework::timer_tick_t &tick) {
          ++callback_count;
          if (tick.name != "stage-tick") {
              throw std::runtime_error ("unexpected timer name");
          }
      });
    if (!first || callback_count != 1 || first.value ().delivery_index != 1
        || first.value ().scheduled_index != 3 || first.value ().skipped_ticks != 2
        || first.value ().period != 16ms || first.value ().scheduled_elapsed != 48ms
        || first.value ().started_elapsed != 48ms || first.value ().delay != 0ms) {
        return 2;
    }

    auto catch_up = context.add_timer<tick_handler_t> (
      "catch-up", 10ms,
      {.overrun_policy = timer_overrun_policy_t::catch_up_bounded, .max_catch_up_ticks = 2});
    auto catch_up_tick = runtime.dispatch_fire_count (catch_up, 5);
    if (!catch_up_tick || catch_up_tick.value ().scheduled_index != 4
        || catch_up_tick.value ().skipped_ticks != 3) {
        return 3;
    }

    auto delay_next = context.add_timer<tick_handler_t> (
      "delay-next", 10ms, {.overrun_policy = timer_overrun_policy_t::delay_next_tick});
    auto delay_tick = runtime.dispatch_fire_count (delay_next, 5);
    if (!delay_tick || delay_tick.value ().scheduled_index != 1
        || delay_tick.value ().skipped_ticks != 0 || delay_tick.value ().started_elapsed != 10ms) {
        return 4;
    }

    auto stop_timer = context.add_timer<failing_tick_handler_t> (
      "stop-on-error", 10ms, {.stop_on_unhandled_exception = true});
    auto failure =
      runtime.dispatch_fire_count (stop_timer, 1, [] (const zlink::framework::timer_tick_t &) {
          throw std::runtime_error ("tick failed");
      });
    const auto failures = runtime.failure_events (stop_timer);
    if (failure || failure.error_kind () != framework_error_kind_t::request_failed
        || failures.size () != 1 || !failures[0].stopped
        || failures[0].timer_name != "stop-on-error" || !stop_timer.is_disposed ()) {
        return 5;
    }

    auto continue_timer = context.add_timer<failing_tick_handler_t> (
      "continue-on-error", 10ms, {.stop_on_unhandled_exception = false});
    auto continue_failure =
      runtime.dispatch_fire_count (continue_timer, 1, [] (const zlink::framework::timer_tick_t &) {
          throw std::runtime_error ("tick failed");
      });
    if (continue_failure || continue_failure.error_kind () != framework_error_kind_t::request_failed
        || runtime.failure_events (continue_timer).size () != 1 || continue_timer.is_disposed ()) {
        return 6;
    }

    auto unknown_failure_timer = context.add_timer<failing_tick_handler_t> (
      "unknown-failure", 10ms, {.stop_on_unhandled_exception = false});
    auto unknown_failure = runtime.dispatch_fire_count (
      unknown_failure_timer, 1, [] (const zlink::framework::timer_tick_t &) { throw 42; });
    if (unknown_failure
        || unknown_failure.error_kind () != framework_error_kind_t::request_failed
        || runtime.failure_events (unknown_failure_timer).size () != 1
        || runtime.failure_events (unknown_failure_timer)[0].message
             != "unknown timer handler failure"
        || unknown_failure_timer.is_disposed ()) {
        return 18;
    }

    auto running_timer = context.add_timer<tick_handler_t> ("running", 10ms);
    auto running_result = runtime.dispatch_fire_count (
      running_timer, 1, [&runtime, &running_timer] (const zlink::framework::timer_tick_t &) {
          const auto nested = runtime.dispatch_fire_count (running_timer, 1);
          if (nested || nested.error_kind () != framework_error_kind_t::request_rejected) {
              throw std::runtime_error ("timer reentry was not rejected");
          }
      });
    if (!running_result) {
        return 7;
    }

    auto entry_timer = entry_context.add_timer<tick_handler_t> ("entry-tick", 10ms);
    auto user_timer = context.add_timer<tick_handler_t> ("user-tick", 10ms);
    auto entry_runtime = zlink::framework::detail::timer_runtime_t::from (entry_context);
    bool user_timer_ran_during_entry_tick = false;
    auto entry_result = entry_runtime.dispatch_fire_count (
      entry_timer, 1,
      [&runtime, &user_timer,
       &user_timer_ran_during_entry_tick] (const zlink::framework::timer_tick_t &) {
          const auto user_tick = runtime.dispatch_fire_count (user_timer, 1);
          user_timer_ran_during_entry_tick = user_tick && user_tick.value ().name == "user-tick";
      });
    if (!entry_result || !user_timer_ran_during_entry_tick) {
        return 12;
    }

    auto close_created = builder.create_spot ("stage");
    auto close_context = close_created.context;
    auto close_timer = close_context.add_timer<tick_handler_t> ("close-after-callback", 10ms);
    auto close_runtime = zlink::framework::detail::timer_runtime_t::from (close_context);
    bool close_requested_inside_callback = false;
    bool spot_visible_during_callback = false;
    auto close_tick = close_runtime.dispatch_fire_count (
      close_timer, 1, [&] (const zlink::framework::timer_tick_t &) {
          const auto close_result = close_context.close ().result ();
          close_requested_inside_callback = close_result && close_result.value ();
          spot_visible_during_callback = builder.find_spot (close_created.spot_rid).result ().value ().has_value ();
      });
    if (!close_tick || !close_requested_inside_callback || !spot_visible_during_callback) {
        return 14;
    }
    if (builder.find_spot (close_created.spot_rid).result ().value ()) {
        return 15;
    }
    if (!close_timer.is_disposed ()) {
        return 22;
    }

    auto concurrent_close_created = builder.create_spot ("stage");
    auto concurrent_close_context = concurrent_close_created.context;
    auto concurrent_close_timer =
      concurrent_close_context.add_timer<tick_handler_t> ("concurrent-close", 10ms);
    auto concurrent_close_runtime =
      zlink::framework::detail::timer_runtime_t::from (concurrent_close_context);
    std::promise<void> callback_entered;
    std::promise<void> release_callback;
    auto release_signal = release_callback.get_future ().share ();
    bool callback_completed = false;
    std::thread callback_thread ([&] {
        callback_completed = static_cast<bool> (concurrent_close_runtime.dispatch_fire_count (
          concurrent_close_timer, 1, [&] (const zlink::framework::timer_tick_t &) {
              callback_entered.set_value ();
              release_signal.wait ();
          }));
    });
    callback_entered.get_future ().wait ();
    const auto concurrent_close_result = concurrent_close_context.close ().result ();
    const auto remained_visible =
      builder.find_spot (concurrent_close_created.spot_rid).result ().value ().has_value ();
    release_callback.set_value ();
    callback_thread.join ();
    if (!concurrent_close_result || !concurrent_close_result.value () || !remained_visible
        || !callback_completed
        || builder.find_spot (concurrent_close_created.spot_rid).result ().value ()
        || !concurrent_close_timer.is_disposed ()) {
        return 23;
    }

    bool invalid_period_failed = false;
    try {
        context.add_timer<tick_handler_t> ("bad-period", 0ms);
    }
    catch (const zlink::framework::framework_exception_t &error) {
        invalid_period_failed = error.kind () == framework_error_kind_t::request_protocol_error;
    }
    if (!invalid_period_failed) {
        return 8;
    }

    bool empty_name_failed = false;
    try {
        context.add_timer<tick_handler_t> ("", 1ms);
    }
    catch (const zlink::framework::framework_exception_t &error) {
        empty_name_failed = error.kind () == framework_error_kind_t::request_protocol_error;
    }
    if (!empty_name_failed) {
        return 19;
    }

    bool invalid_catchup_failed = false;
    try {
        context.add_timer<tick_handler_t> (
          "bad-catchup", 1ms,
          {.overrun_policy = timer_overrun_policy_t::catch_up_bounded, .max_catch_up_ticks = 0});
    }
    catch (const zlink::framework::framework_exception_t &error) {
        invalid_catchup_failed = error.kind () == framework_error_kind_t::request_protocol_error;
    }
    if (!invalid_catchup_failed) {
        return 9;
    }

    runtime.cancel_all ();
    if (!timer.is_disposed () || !catch_up.is_disposed () || !delay_next.is_disposed ()
        || !continue_timer.is_disposed () || !running_timer.is_disposed ()
        || !user_timer.is_disposed ()) {
        return 10;
    }
    entry_runtime.cancel_all ();
    if (!entry_timer.is_disposed ()) {
        return 13;
    }

    auto closed_result = runtime.dispatch_fire_count (timer, 1);
    if (closed_result || (closed_result.error () != nullptr
         && zlink::framework::detail::boundary_state (*closed_result.error ()) != zlink::framework::detail::boundary_error_t::closed)) {
        return 11;
    }

    auto move_source = context.add_timer<tick_handler_t> ("move-source", 10ms);
    zlink::framework::timer_t move_target (std::move (move_source));
    if (runtime.delivered_ticks (move_source).size () != 0
        || runtime.failure_events (move_source).size () != 0) {
        return 20;
    }
    move_target.cancel ();
    zlink::framework::timer_t assigned_target;
    assigned_target = std::move (move_target);
    if (!assigned_target.is_disposed ()) {
        return 21;
    }

    return 0;
}
