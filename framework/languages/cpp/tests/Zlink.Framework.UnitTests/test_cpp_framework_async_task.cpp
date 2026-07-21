/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#include <zlink/framework/contracts/channels/call.hpp>
#include <zlink/framework/contracts/dispatch/task.hpp>

#include <chrono>
#include <coroutine>
#include <deque>
#include <functional>
#include <thread>

namespace
{

zlink::framework::task_t<int> delayed_value (int value)
{
    struct delay_awaiter_t
    {
        int value;

        bool await_ready () const noexcept { return false; }
        void await_suspend (std::coroutine_handle<> continuation) const
        {
            std::thread ([continuation] {
                std::this_thread::sleep_for (std::chrono::milliseconds (5));
                continuation.resume ();
            }).detach ();
        }
        int await_resume () const noexcept { return value; }
    };

    co_return co_await delay_awaiter_t{value};
}

zlink::framework::task_t<int> await_shared (zlink::framework::task_t<int> &task, int offset)
{
    const auto value = co_await task;
    co_return value + offset;
}

zlink::framework::task_t<int> timeout_task ()
{
    co_return zlink::framework::detail::boundary_failure<int> (zlink::framework::detail::boundary_error_t::timed_out, "timeout preserved");
}

zlink::framework::task_t<int> await_timeout ()
{
    auto task = timeout_task ();
    co_return co_await task;
}

} // namespace

int main ()
{
    auto shared = delayed_value (40);
    auto first_waiter = await_shared (shared, 1);
    auto second_waiter = await_shared (shared, 2);
    if (first_waiter.result ().value () != 41 || second_waiter.result ().value () != 42) {
        return 1;
    }

    zlink::framework::detail::task_completion_source_t<int> completion;
    auto first_complete_wins = completion.task ();
    int callback_count = 0;
    int callback_value = 0;
    zlink::framework::detail::observe_task_completion (
      first_complete_wins,
      [&callback_count, &callback_value] (const zlink::framework::result_t<int> &result) {
          ++callback_count;
          callback_value = result.value ();
      });
    completion.complete (zlink::framework::result_t<int>::success (100));
    completion.complete (zlink::framework::result_t<int>::success (200));
    if (first_complete_wins.result ().value () != 100 || callback_count != 1
        || callback_value != 100) {
        return 2;
    }

    zlink::framework::request_call_t<int> call (zlink::framework::detail::boundary_failure<int> (zlink::framework::detail::boundary_error_t::timed_out, "timeout"));
    auto coroutine_result = call.async ().result ();
    if ((coroutine_result.error () != nullptr
         && zlink::framework::detail::boundary_state (*coroutine_result.error ()) != zlink::framework::detail::boundary_error_t::timed_out)) {
        return 3;
    }

    const auto preserved_failure = await_timeout ().result ();
    if (preserved_failure
        || (preserved_failure.error () != nullptr
         && zlink::framework::detail::boundary_state (*preserved_failure.error ()) != zlink::framework::detail::boundary_error_t::timed_out)) {
        return 4;
    }

    zlink::framework::request_call_t<int> shutdown_call (zlink::framework::detail::boundary_failure<int> (zlink::framework::detail::boundary_error_t::shutdown, "shutdown"));
    const auto shutdown_result = shutdown_call.async ().result ();
    if (shutdown_result || shutdown_result.error () == nullptr
        || zlink::framework::detail::boundary_state (*shutdown_result.error ())
             != zlink::framework::detail::boundary_error_t::shutdown) {
        return 5;
    }

    std::deque<std::function<void ()>> scheduled;
    zlink::framework::detail::task_completion_source_t<int> scheduled_completion (
      [&scheduled] (std::function<void ()> work) { scheduled.push_back (std::move (work)); });
    auto scheduled_task = scheduled_completion.task ();
    int scheduled_callback_count = 0;
    zlink::framework::detail::observe_task_completion (
      scheduled_task, [&scheduled_callback_count] (const zlink::framework::result_t<int> &result) {
          if (result.value () == 300) {
              ++scheduled_callback_count;
          }
      });
    scheduled_completion.complete (zlink::framework::result_t<int>::success (300));
    scheduled_completion.complete (zlink::framework::result_t<int>::success (400));
    if (scheduled_callback_count != 0 || scheduled.size () != 1) {
        return 6;
    }
    scheduled.front () ();
    scheduled.pop_front ();
    if (scheduled_callback_count != 1 || scheduled_task.result ().value () != 300) {
        return 7;
    }

    std::deque<std::function<void ()>> rescheduled_work;
    auto immediate_task = zlink::framework::task_t<int> (
      zlink::framework::result_t<int>::success (500));
    auto rescheduled_task = zlink::framework::detail::reschedule_task (
      std::move (immediate_task),
      [&rescheduled_work] (std::function<void ()> work) {
          rescheduled_work.push_back (std::move (work));
      });
    int rescheduled_callback_count = 0;
    zlink::framework::detail::observe_task_completion (
      rescheduled_task,
      [&rescheduled_callback_count] (const zlink::framework::result_t<int> &result) {
          if (result.value () == 500) {
              ++rescheduled_callback_count;
          }
      });
    if (rescheduled_callback_count != 0 || rescheduled_work.size () != 1) {
        return 8;
    }
    rescheduled_work.front () ();
    rescheduled_work.pop_front ();
    if (rescheduled_callback_count != 1 || rescheduled_task.result ().value () != 500) {
        return 9;
    }

    bool one_way_invoked = false;
    zlink::framework::send_call_t accepted (
      "test.command",
      [&one_way_invoked] (const std::string &,
                          const zlink::framework::send_call_t::metadata_map_t &) {
          one_way_invoked = true;
          return zlink::framework::result_t<void>::success ();
      });
    const auto accepted_result = accepted.submit ().result ();
    if (!accepted_result || !one_way_invoked
        || accepted_result.value ().status
             != zlink::framework::submit_status_t::submitted) {
        return 10;
    }

    zlink::framework::send_call_t timed_out (
      zlink::framework::detail::boundary_failure<void> (
        zlink::framework::detail::boundary_error_t::timed_out, "send timed out"));
    const auto timed_out_result = timed_out.submit ().result ();
    if (!timed_out_result
        || timed_out_result.value ().status
             != zlink::framework::submit_status_t::timed_out) {
        return 11;
    }

    zlink::framework::send_call_t disconnected (
      zlink::framework::detail::boundary_failure<void> (
        zlink::framework::detail::boundary_error_t::disconnected, "route unavailable"));
    const auto disconnected_result = disconnected.submit ().result ();
    if (!disconnected_result
        || disconnected_result.value ().status
             != zlink::framework::submit_status_t::route_not_connected) {
        return 12;
    }


    return 0;
}
