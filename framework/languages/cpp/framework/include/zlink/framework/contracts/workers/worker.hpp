/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include <zlink/framework/contracts/dispatch/task.hpp>
#include <zlink/framework/contracts/errors/result.hpp>

#include <chrono>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>

namespace zlink::framework
{

namespace detail
{

template <typename TTask> struct task_result;

template <typename TResult> struct task_result<task_t<TResult>>
{
    using type = TResult;
};

template <typename TTask>
using task_result_t = typename task_result<std::remove_cvref_t<TTask>>::type;

class worker_scheduler_t
{
  public:
    virtual ~worker_scheduler_t () = default;

    virtual bool try_schedule (std::function<void ()> work) = 0;
    virtual void post_owner (std::function<void ()> work) = 0;
};

template <typename TResult, typename TWork> result_t<TResult> run_worker_body (TWork &work)
{
    try {
        if constexpr (std::is_void_v<TResult>) {
            work ();
            return result_t<void>::success ();
        } else {
            return result_t<TResult>::success (work ());
        }
    }
    catch (const framework_exception_t &error) {
        return detail::result_access_t::failure<TResult> (error);
    }
    catch (const std::exception &error) {
        return result_t<TResult>::failure (framework_error_kind_t::worker_failed, error.what ());
    }
    catch (...) {
        return result_t<TResult>::failure (framework_error_kind_t::worker_failed,
                                           "worker task threw an exception");
    }
}

} // namespace detail

template <typename TResult> class worker_call_t
{
  public:
    using executor_t =
      std::function<task_t<TResult> (std::optional<std::chrono::milliseconds>)>;

    worker_call_t () = default;
    explicit worker_call_t (executor_t executor) : _executor (std::move (executor)) {}

    worker_call_t &timeout (std::chrono::milliseconds value)
    {
        _timeout = value;
        return *this;
    }

    task_t<TResult> submit () { return start (false); }

    task_t<TResult> yield () { return start (true); }

  private:
    task_t<TResult> start (bool release_turn)
    {
        if (release_turn && !detail::current_serial_turn_allows_yield ()) {
            return detail::unsupported_yield_task<TResult> ();
        }
        if (!try_start ()) {
            return task_t<TResult> (
              result_t<TResult>::failure (framework_error_kind_t::request_protocol_error,
                                          "worker call already has a terminator"));
        }
        if (!_executor) {
            return task_t<TResult> (result_t<TResult>::failure (
              framework_error_kind_t::request_failed, "worker runtime is not configured"));
        }
        auto turn_plan = detail::prepare_serial_turn_await (release_turn);
        if (!turn_plan) {
            return _executor (_timeout);
        }
        return detail::reschedule_task (
          _executor (_timeout),
          std::move (turn_plan->scheduler));
    }
    bool try_start ()
    {
        if (_started) {
            return false;
        }
        _started = true;
        return true;
    }

    executor_t _executor;
    std::optional<std::chrono::milliseconds> _timeout;
    bool _started = false;
};

} // namespace zlink::framework
