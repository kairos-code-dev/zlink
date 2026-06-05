/* SPDX-License-Identifier: MPL-2.0 */

#include <zlink/framework/contracts/channels/call.hpp>
#include <zlink/framework/contracts/dispatch/task.hpp>

#include <chrono>
#include <coroutine>
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
    co_return zlink::framework::result_t<int>::failure (zlink::framework::framework_error_kind_t::timeout,
                                                        "timeout preserved");
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
      first_complete_wins, [&callback_count, &callback_value] (const zlink::framework::result_t<int> &result) {
          ++callback_count;
          callback_value = result.value ();
      });
    completion.complete (zlink::framework::result_t<int>::success (100));
    completion.complete (zlink::framework::result_t<int>::success (200));
    if (first_complete_wins.result ().value () != 100 || callback_count != 1 || callback_value != 100) {
        return 2;
    }

    auto callback_kind = zlink::framework::framework_error_kind_t::request_failed;
    zlink::framework::request_call_t<int> callback_call (
      zlink::framework::result_t<int>::failure (zlink::framework::framework_error_kind_t::timeout, "timeout"));
    callback_call.submit ([&] (zlink::framework::result_t<int> result) { callback_kind = result.error_kind (); });
    auto coroutine_result = callback_call.submit ().result ();
    if (callback_kind != coroutine_result.error_kind ()) {
        return 3;
    }

    const auto preserved_failure = await_timeout ().result ();
    if (preserved_failure || preserved_failure.error_kind () != zlink::framework::framework_error_kind_t::timeout) {
        return 4;
    }

    zlink::framework::request_call_t<int> shutdown_call (
      zlink::framework::result_t<int>::failure (zlink::framework::framework_error_kind_t::shutdown, "shutdown"));
    if (shutdown_call.submit ().result ().error_kind () != zlink::framework::framework_error_kind_t::shutdown) {
        return 5;
    }

    return 0;
}
