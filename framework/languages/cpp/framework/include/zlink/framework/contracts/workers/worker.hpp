/* SPDX-License-Identifier: MPL-2.0 */
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

class worker_scheduler_t
{
  public:
    virtual ~worker_scheduler_t () = default;

    virtual bool try_schedule (std::function<void ()> work) = 0;
    virtual void post_owner (std::function<void ()> work) = 0;
};

enum class worker_completion_mode_t
{
    owner_queue,
    current_turn
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
        return result_t<TResult>::failure (error.kind (), error.what (), error.is_retriable ());
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
    using executor_t = std::function<task_t<TResult> (
      std::optional<std::chrono::milliseconds>, detail::worker_completion_mode_t)>;
    using completion_callback_t = std::function<task_t<void> (result_t<TResult>)>;

    worker_call_t () = default;
    explicit worker_call_t (executor_t executor) :
        _executor (std::move (executor)),
        _yield_turn (detail::capture_current_serial_yield_turn ())
    {
    }

    worker_call_t &timeout (std::chrono::milliseconds value)
    {
        _timeout = value;
        return *this;
    }

    task_t<TResult> async ()
    {
        if (!try_start ()) {
            return task_t<TResult> (
              result_t<TResult>::failure (framework_error_kind_t::request_protocol_error,
                                          "worker call already has a terminator"));
        }
        if (!_executor) {
            return task_t<TResult> (result_t<TResult>::failure (
              framework_error_kind_t::request_failed, "worker runtime is not configured"));
        }
        return _executor (_timeout, detail::worker_completion_mode_t::owner_queue);
    }

    task_t<TResult> yield ()
    {
        if (!try_start ()) {
            return task_t<TResult> (
              result_t<TResult>::failure (framework_error_kind_t::request_protocol_error,
                                          "worker call already has a terminator"));
        }
        if (!_executor) {
            return task_t<TResult> (result_t<TResult>::failure (
              framework_error_kind_t::request_failed, "worker runtime is not configured"));
        }
        if (!_yield_turn) {
            return task_t<TResult> (result_t<TResult>::failure (
              framework_error_kind_t::request_protocol_error,
              "yield requires a framework Spot handler turn captured when the call object was created"));
        }
        if (!_yield_turn->release ()) {
            return task_t<TResult> (result_t<TResult>::failure (
              framework_error_kind_t::request_protocol_error,
              "yield could not release the current Spot handler turn"));
        }
        return detail::reschedule_task (
          _executor (_timeout, detail::worker_completion_mode_t::owner_queue),
          _yield_turn->resume_scheduler ());
    }

    void submit (completion_callback_t callback)
    {
        if (!try_start ()) {
            throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                         "worker call already has a terminator");
        }
        if (!callback) {
            throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                         "worker completion callback is required");
        }
        if (!_executor) {
            (void) callback (result_t<TResult>::failure (framework_error_kind_t::request_failed,
                                                         "worker runtime is not configured"));
            return;
        }

        auto task = _executor (_timeout, detail::worker_completion_mode_t::owner_queue);
        detail::observe_task_completion (
          task, [callback = std::move (callback)] (const result_t<TResult> &result) mutable {
              (void) callback (result);
          });
    }

  private:
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
    std::shared_ptr<detail::serial_yield_turn_t> _yield_turn;
    bool _started = false;
};

} // namespace zlink::framework
