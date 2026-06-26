/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/framework/contracts/errors/result.hpp>

#include <condition_variable>
#include <coroutine>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

namespace zlink::framework
{

template <typename T> class task_t;

namespace detail
{

using task_scheduler_t = std::function<void (std::function<void ()>)>;

class serial_yield_turn_t
{
  public:
    virtual ~serial_yield_turn_t () = default;
    virtual bool release () = 0;
    virtual bool released () const = 0;
    virtual task_scheduler_t resume_scheduler () = 0;
};

inline thread_local std::shared_ptr<serial_yield_turn_t> current_serial_yield_turn;

inline std::shared_ptr<serial_yield_turn_t> capture_current_serial_yield_turn ()
{
    return current_serial_yield_turn;
}

class serial_yield_turn_scope_t
{
  public:
    explicit serial_yield_turn_scope_t (std::shared_ptr<serial_yield_turn_t> turn) :
        _previous (std::move (current_serial_yield_turn))
    {
        current_serial_yield_turn = std::move (turn);
    }

    ~serial_yield_turn_scope_t () { current_serial_yield_turn = std::move (_previous); }

    serial_yield_turn_scope_t (const serial_yield_turn_scope_t &) = delete;
    serial_yield_turn_scope_t &operator= (const serial_yield_turn_scope_t &) = delete;

  private:
    std::shared_ptr<serial_yield_turn_t> _previous;
};

template <typename T>
class task_shared_state_t : public std::enable_shared_from_this<task_shared_state_t<T>>
{
  public:
    explicit task_shared_state_t (task_scheduler_t scheduler = {}) :
        _scheduler (std::move (scheduler))
    {
    }

    void complete (result_t<T> result)
    {
        std::vector<std::coroutine_handle<>> continuations;
        std::vector<std::function<void (const result_t<T> &)>> callbacks;
        auto self = this->shared_from_this ();
        {
            std::lock_guard lock (_mutex);
            if (_result) {
                return;
            }
            _result = std::move (result);
            continuations = std::move (_continuations);
            callbacks = std::move (_callbacks);
        }
        _ready.notify_all ();
        for (auto &callback : callbacks) {
            schedule ([self, callback = std::move (callback)] { callback (*self->_result); });
        }
        for (auto continuation : continuations) {
            schedule ([continuation] { continuation.resume (); });
        }
    }

    bool is_ready () const
    {
        std::lock_guard lock (_mutex);
        return _result.has_value ();
    }

    void set_continuation (std::coroutine_handle<> continuation)
    {
        bool resume_now = false;
        {
            std::lock_guard lock (_mutex);
            if (_result) {
                resume_now = true;
            } else {
                _continuations.push_back (continuation);
            }
        }
        if (resume_now) {
            schedule ([continuation] { continuation.resume (); });
        }
    }

    const result_t<T> &result ()
    {
        std::unique_lock lock (_mutex);
        _ready.wait (lock, [&] { return _result.has_value (); });
        return *_result;
    }

    void on_completed (std::function<void (const result_t<T> &)> callback)
    {
        auto self = this->shared_from_this ();
        bool completed = false;
        {
            std::lock_guard lock (_mutex);
            if (_result) {
                completed = true;
            } else {
                _callbacks.push_back (std::move (callback));
                return;
            }
        }
        if (completed) {
            schedule ([self, callback = std::move (callback)] { callback (*self->_result); });
        }
    }

  private:
    void schedule (std::function<void ()> work) const
    {
        if (!_scheduler) {
            work ();
            return;
        }
        try {
            _scheduler (std::move (work));
        }
        catch (...) {
        }
    }

    mutable std::mutex _mutex;
    std::condition_variable _ready;
    task_scheduler_t _scheduler;
    std::optional<result_t<T>> _result;
    std::vector<std::coroutine_handle<>> _continuations;
    std::vector<std::function<void (const result_t<T> &)>> _callbacks;
};

template <typename T> class task_completion_source_t
{
  public:
    explicit task_completion_source_t (task_scheduler_t scheduler = {}) :
        _state (std::make_shared<task_shared_state_t<T>> (std::move (scheduler)))
    {
    }

    task_t<T> task ();

    void complete (result_t<T> result) { _state->complete (std::move (result)); }

  private:
    std::shared_ptr<task_shared_state_t<T>> _state;
};

template <typename T, typename TCallback>
void observe_task_completion (task_t<T> &task, TCallback &&callback);

template <typename T> task_t<T> reschedule_task (task_t<T> task, task_scheduler_t scheduler);

} // namespace detail

template <typename T> class task_t
{
  public:
    struct promise_type
    {
        detail::task_completion_source_t<T> completion;

        task_t get_return_object () { return completion.task (); }
        std::suspend_never initial_suspend () noexcept { return {}; }
        std::suspend_never final_suspend () noexcept { return {}; }
        void unhandled_exception ()
        {
            try {
                throw;
            }
            catch (const framework_exception_t &error) {
                completion.complete (
                  result_t<T>::failure (error.kind (), error.what (), error.is_retriable ()));
            }
            catch (...) {
                completion.complete (result_t<T>::failure (framework_error_kind_t::request_failed,
                                                           "unhandled coroutine exception"));
            }
        }

        void return_value (result_t<T> result) { completion.complete (std::move (result)); }

        template <typename U>
        requires (!std::is_same_v<std::remove_cvref_t<U>, result_t<T>>) void return_value (
          U &&value)
        {
            completion.complete (result_t<T>::success (T (std::forward<U> (value))));
        }
    };

    explicit task_t (result_t<T> result) :
        _state (std::make_shared<detail::task_shared_state_t<T>> ())
    {
        _state->complete (std::move (result));
    }

    task_t (task_t &&) noexcept = default;
    task_t &operator= (task_t &&) noexcept = default;
    task_t (const task_t &) = delete;
    task_t &operator= (const task_t &) = delete;
    ~task_t () = default;

    bool await_ready () const noexcept { return _state->is_ready (); }
    void await_suspend (std::coroutine_handle<> continuation)
    {
        _state->set_continuation (continuation);
    }
    T await_resume () { return result ().value (); }

    const result_t<T> &result () const { return _state->result (); }

  private:
    explicit task_t (std::shared_ptr<detail::task_shared_state_t<T>> state) :
        _state (std::move (state))
    {
    }

    std::shared_ptr<detail::task_shared_state_t<T>> _state;

    friend class detail::task_completion_source_t<T>;
    template <typename TObserved, typename TCallback>
    friend void detail::observe_task_completion (task_t<TObserved> &, TCallback &&);
};

template <> class task_t<void>
{
  public:
    struct promise_type
    {
        detail::task_completion_source_t<void> completion;

        task_t get_return_object () { return completion.task (); }
        std::suspend_never initial_suspend () noexcept { return {}; }
        std::suspend_never final_suspend () noexcept { return {}; }
        void unhandled_exception ()
        {
            try {
                throw;
            }
            catch (const framework_exception_t &error) {
                completion.complete (
                  result_t<void>::failure (error.kind (), error.what (), error.is_retriable ()));
            }
            catch (...) {
                completion.complete (result_t<void>::failure (
                  framework_error_kind_t::request_failed, "unhandled coroutine exception"));
            }
        }
        void return_void () noexcept { completion.complete (result_t<void>::success ()); }
    };

    explicit task_t (result_t<void> result) :
        _state (std::make_shared<detail::task_shared_state_t<void>> ())
    {
        _state->complete (std::move (result));
    }

    task_t (task_t &&) noexcept = default;
    task_t &operator= (task_t &&) noexcept = default;
    task_t (const task_t &) = delete;
    task_t &operator= (const task_t &) = delete;
    ~task_t () = default;

    bool await_ready () const noexcept { return _state->is_ready (); }
    void await_suspend (std::coroutine_handle<> continuation)
    {
        _state->set_continuation (continuation);
    }
    void await_resume () { result ().value (); }

    const result_t<void> &result () const { return _state->result (); }

  private:
    explicit task_t (std::shared_ptr<detail::task_shared_state_t<void>> state) :
        _state (std::move (state))
    {
    }

    std::shared_ptr<detail::task_shared_state_t<void>> _state;

    friend class detail::task_completion_source_t<void>;
    template <typename TObserved, typename TCallback>
    friend void detail::observe_task_completion (task_t<TObserved> &, TCallback &&);
};

namespace detail
{

template <typename T> task_t<T> task_completion_source_t<T>::task ()
{
    return task_t<T> (_state);
}

template <typename T, typename TCallback>
void observe_task_completion (task_t<T> &task, TCallback &&callback)
{
    task._state->on_completed (
      std::function<void (const result_t<T> &)> (std::forward<TCallback> (callback)));
}

template <typename T> task_t<T> reschedule_task (task_t<T> task, task_scheduler_t scheduler)
{
    auto source = std::make_shared<task_completion_source_t<T>> (std::move (scheduler));
    auto output = source->task ();
    auto observed = std::make_shared<task_t<T>> (std::move (task));
    detail::observe_task_completion (
      *observed, [source, observed] (const result_t<T> &result) mutable {
          source->complete (result);
      });
    return output;
}

} // namespace detail

template <typename T, typename TCallback>
void observe_task_completion (task_t<T> &task, TCallback &&callback)
{
    detail::observe_task_completion (task, std::forward<TCallback> (callback));
}

} // namespace zlink::framework
