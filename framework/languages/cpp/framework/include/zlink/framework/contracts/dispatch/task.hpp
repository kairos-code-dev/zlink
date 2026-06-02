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

template<typename T>
class task_t;

namespace detail
{

template<typename T>
class task_shared_state_t
{
public:
  void complete (result_t<T> result)
  {
    std::vector<std::coroutine_handle<>> continuations;
    std::vector<std::function<void (const result_t<T> &)>> callbacks;
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
      callback (*_result);
    }
    for (auto continuation : continuations) {
      continuation.resume ();
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
      continuation.resume ();
    }
  }

  const result_t<T> &result ()
  {
    std::unique_lock lock (_mutex);
    _ready.wait (lock, [&] {
      return _result.has_value ();
    });
    return *_result;
  }

  void on_completed (std::function<void (const result_t<T> &)> callback)
  {
    const result_t<T> *completed = nullptr;
    {
      std::lock_guard lock (_mutex);
      if (_result) {
        completed = &*_result;
      } else {
        _callbacks.push_back (std::move (callback));
        return;
      }
    }
    callback (*completed);
  }

private:
  mutable std::mutex _mutex;
  std::condition_variable _ready;
  std::optional<result_t<T>> _result;
  std::vector<std::coroutine_handle<>> _continuations;
  std::vector<std::function<void (const result_t<T> &)>> _callbacks;
};

template<typename T>
class task_completion_source_t
{
public:
  task_completion_source_t ()
    : _state (std::make_shared<task_shared_state_t<T>> ())
  {
  }

  task_t<T> task ();

  void complete (result_t<T> result) { _state->complete (std::move (result)); }

private:
  std::shared_ptr<task_shared_state_t<T>> _state;
};

template<typename T, typename TCallback>
void observe_task_completion (
  task_t<T> &task,
  TCallback &&callback);

} // namespace detail

template<typename T>
class task_t
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
      } catch (const framework_exception_t &error) {
        completion.complete (result_t<T>::failure (
          error.kind (),
          error.what (),
          error.is_retriable ()));
      } catch (...) {
        completion.complete (result_t<T>::failure (
          framework_error_kind_t::request_failed,
          "unhandled coroutine exception"));
      }
    }

    void return_value (result_t<T> result)
    {
      completion.complete (std::move (result));
    }

    template<typename U>
      requires (!std::is_same_v<std::remove_cvref_t<U>, result_t<T>>)
    void return_value (U &&value)
    {
      completion.complete (
        result_t<T>::success (T (std::forward<U> (value))));
    }
  };

  explicit task_t (result_t<T> result)
    : _state (std::make_shared<detail::task_shared_state_t<T>> ())
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
  explicit task_t (std::shared_ptr<detail::task_shared_state_t<T>> state)
    : _state (std::move (state))
  {
  }

  std::shared_ptr<detail::task_shared_state_t<T>> _state;

  friend class detail::task_completion_source_t<T>;
  template<typename TObserved, typename TCallback>
  friend void detail::observe_task_completion (
    task_t<TObserved> &,
    TCallback &&);
};

template<>
class task_t<void>
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
      } catch (const framework_exception_t &error) {
        completion.complete (result_t<void>::failure (
          error.kind (),
          error.what (),
          error.is_retriable ()));
      } catch (...) {
        completion.complete (result_t<void>::failure (
          framework_error_kind_t::request_failed,
          "unhandled coroutine exception"));
      }
    }
    void return_void () noexcept
    {
      completion.complete (result_t<void>::success ());
    }
  };

  explicit task_t (result_t<void> result)
    : _state (std::make_shared<detail::task_shared_state_t<void>> ())
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
  explicit task_t (std::shared_ptr<detail::task_shared_state_t<void>> state)
    : _state (std::move (state))
  {
  }

  std::shared_ptr<detail::task_shared_state_t<void>> _state;

  friend class detail::task_completion_source_t<void>;
  template<typename TObserved, typename TCallback>
  friend void detail::observe_task_completion (
    task_t<TObserved> &,
    TCallback &&);
};

namespace detail
{

template<typename T>
task_t<T>
task_completion_source_t<T>::task ()
{
  return task_t<T> (_state);
}

template<typename T, typename TCallback>
void
observe_task_completion (
  task_t<T> &task,
  TCallback &&callback)
{
  task._state->on_completed (
    std::function<void (const result_t<T> &)> (
      std::forward<TCallback> (callback)));
}

} // namespace detail

} // namespace zlink::framework
