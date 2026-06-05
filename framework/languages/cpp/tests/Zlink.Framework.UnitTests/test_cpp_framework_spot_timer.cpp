/* SPDX-License-Identifier: MPL-2.0 */

#include <zlink/framework.hpp>

#include "runtime/timers/timer_runtime.hpp"

#include <chrono>
#include <stdexcept>

namespace
{

struct stage_spot_t
{
};

struct entry_spot_t
{
};

struct tick_handler_t
{
};

struct failing_tick_handler_t
{
};

} // namespace

int main ()
{
    using namespace std::chrono_literals;
    using zlink::framework::framework_error_kind_t;
    using zlink::framework::timer_overrun_policy_t;

    zlink::framework::spot_node_builder_t builder;
    zlink::framework::zlink_builder_t host;
    host.add_spot_node ("timer-node", [&builder] (zlink::framework::spot_node_builder_t &spot_node) {
        spot_node.add_entry_spot<entry_spot_t> ().add_spot<stage_spot_t> ("stage");
        builder = spot_node;
    });

    auto entry_context = builder.create_spot ("entry");
    auto context = builder.create_spot ("stage");
    auto timer = context.add_timer<tick_handler_t> ("stage-tick", 16ms,
                                                    {.overrun_policy = timer_overrun_policy_t::skip_late_ticks});
    if (timer.is_disposed ()) {
        return 1;
    }

    auto runtime = zlink::framework::detail::timer_runtime_t::from (context);
    int callback_count = 0;
    auto first = runtime.dispatch_fire_count (timer, 3, [&callback_count] (const zlink::framework::timer_tick_t &tick) {
        ++callback_count;
        if (tick.name != "stage-tick") {
            throw std::runtime_error ("unexpected timer name");
        }
    });
    if (!first || callback_count != 1 || first.value ().delivery_index != 1 || first.value ().scheduled_index != 3
        || first.value ().skipped_ticks != 2 || first.value ().period != 16ms
        || first.value ().scheduled_elapsed != 48ms || first.value ().started_elapsed != 48ms
        || first.value ().delay != 0ms) {
        return 2;
    }

    auto catch_up = context.add_timer<tick_handler_t> (
      "catch-up", 10ms, {.overrun_policy = timer_overrun_policy_t::catch_up_bounded, .max_catch_up_ticks = 2});
    auto catch_up_tick = runtime.dispatch_fire_count (catch_up, 5);
    if (!catch_up_tick || catch_up_tick.value ().scheduled_index != 4 || catch_up_tick.value ().skipped_ticks != 3) {
        return 3;
    }

    auto delay_next = context.add_timer<tick_handler_t> ("delay-next", 10ms,
                                                         {.overrun_policy = timer_overrun_policy_t::delay_next_tick});
    auto delay_tick = runtime.dispatch_fire_count (delay_next, 5);
    if (!delay_tick || delay_tick.value ().scheduled_index != 1 || delay_tick.value ().skipped_ticks != 0
        || delay_tick.value ().started_elapsed != 10ms) {
        return 4;
    }

    auto stop_timer =
      context.add_timer<failing_tick_handler_t> ("stop-on-error", 10ms, {.stop_on_unhandled_exception = true});
    auto failure = runtime.dispatch_fire_count (
      stop_timer, 1, [] (const zlink::framework::timer_tick_t &) { throw std::runtime_error ("tick failed"); });
    const auto failures = runtime.failure_events (stop_timer);
    if (failure || failure.error_kind () != framework_error_kind_t::request_failed || failures.size () != 1
        || !failures[0].stopped || failures[0].timer_name != "stop-on-error" || !stop_timer.is_disposed ()) {
        return 5;
    }

    auto continue_timer =
      context.add_timer<failing_tick_handler_t> ("continue-on-error", 10ms, {.stop_on_unhandled_exception = false});
    auto continue_failure = runtime.dispatch_fire_count (
      continue_timer, 1, [] (const zlink::framework::timer_tick_t &) { throw std::runtime_error ("tick failed"); });
    if (continue_failure || continue_failure.error_kind () != framework_error_kind_t::request_failed
        || runtime.failure_events (continue_timer).size () != 1 || continue_timer.is_disposed ()) {
        return 6;
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
      entry_timer, 1, [&runtime, &user_timer, &user_timer_ran_during_entry_tick] (const zlink::framework::timer_tick_t &) {
          const auto user_tick = runtime.dispatch_fire_count (user_timer, 1);
          user_timer_ran_during_entry_tick = user_tick && user_tick.value ().name == "user-tick";
      });
    if (!entry_result || !user_timer_ran_during_entry_tick) {
        return 12;
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

    bool invalid_catchup_failed = false;
    try {
        context.add_timer<tick_handler_t> (
          "bad-catchup", 1ms, {.overrun_policy = timer_overrun_policy_t::catch_up_bounded, .max_catch_up_ticks = 0});
    }
    catch (const zlink::framework::framework_exception_t &error) {
        invalid_catchup_failed = error.kind () == framework_error_kind_t::request_protocol_error;
    }
    if (!invalid_catchup_failed) {
        return 9;
    }

    runtime.cancel_all ();
    if (!timer.is_disposed () || !catch_up.is_disposed () || !delay_next.is_disposed ()
        || !continue_timer.is_disposed () || !running_timer.is_disposed () || !user_timer.is_disposed ()) {
        return 10;
    }
    entry_runtime.cancel_all ();
    if (!entry_timer.is_disposed ()) {
        return 13;
    }

    auto closed_result = runtime.dispatch_fire_count (timer, 1);
    if (closed_result || closed_result.error_kind () != framework_error_kind_t::closed) {
        return 11;
    }

    return 0;
}
