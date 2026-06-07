/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/stream_connector/contracts/result.hpp>

#include <coroutine>
#include <functional>
#include <optional>
#include <type_traits>
#include <utility>

namespace zlink::stream_connector
{

template <typename T> class task_t
{
  public:
    struct promise_type
    {
        std::optional<result_t<T>> result;

        task_t get_return_object ()
        {
            return task_t (std::coroutine_handle<promise_type>::from_promise (*this));
        }
        std::suspend_never initial_suspend () noexcept { return {}; }
        std::suspend_always final_suspend () noexcept { return {}; }
        void unhandled_exception ()
        {
            result = result_t<T>::failure (error_code_t::user_callback_failed,
                                           "unhandled connector coroutine exception");
        }
        void return_value (result_t<T> value) { result = std::move (value); }
        template <typename U>
        requires (!std::is_same_v<std::remove_cvref_t<U>, result_t<T>>) void return_value (
          U &&value)
        {
            result = result_t<T>::success (T (std::forward<U> (value)));
        }
    };

    explicit task_t (result_t<T> result) : _result (std::move (result)) {}
    task_t (task_t &&other) noexcept : _handle (other._handle), _result (std::move (other._result))
    {
        other._handle = {};
    }
    task_t &operator= (task_t &&other) noexcept
    {
        if (this != &other) {
            if (_handle) {
                _handle.destroy ();
            }
            _handle = other._handle;
            _result = std::move (other._result);
            other._handle = {};
        }
        return *this;
    }
    task_t (const task_t &) = delete;
    task_t &operator= (const task_t &) = delete;
    ~task_t ()
    {
        if (_handle) {
            _handle.destroy ();
        }
    }

    bool await_ready () const noexcept { return true; }
    void await_suspend (std::coroutine_handle<>) const noexcept {}
    T await_resume () { return result ().value (); }

    const result_t<T> &result () const
    {
        if (_handle) {
            return *_handle.promise ().result;
        }
        return *_result;
    }

    result_t<T> consume_result ()
    {
        if (_handle) {
            return std::move (*_handle.promise ().result);
        }
        return std::move (*_result);
    }

    void on_completed (std::function<void (result_t<T>)> callback)
    {
        if (callback) {
            callback (consume_result ());
        }
    }

  private:
    explicit task_t (std::coroutine_handle<promise_type> handle) : _handle (handle) {}

    std::coroutine_handle<promise_type> _handle;
    std::optional<result_t<T>> _result;
};

template <> class task_t<void>
{
  public:
    struct promise_type
    {
        result_t<void> result = result_t<void>::success ();

        task_t get_return_object ()
        {
            return task_t (std::coroutine_handle<promise_type>::from_promise (*this));
        }
        std::suspend_never initial_suspend () noexcept { return {}; }
        std::suspend_always final_suspend () noexcept { return {}; }
        void unhandled_exception ()
        {
            result = result_t<void>::failure (error_code_t::user_callback_failed,
                                              "unhandled connector coroutine exception");
        }
        void return_void () noexcept {}
    };

    explicit task_t (result_t<void> result) : _result (std::move (result)) {}
    task_t (task_t &&other) noexcept : _handle (other._handle), _result (std::move (other._result))
    {
        other._handle = {};
    }
    task_t &operator= (task_t &&other) noexcept
    {
        if (this != &other) {
            if (_handle) {
                _handle.destroy ();
            }
            _handle = other._handle;
            _result = std::move (other._result);
            other._handle = {};
        }
        return *this;
    }
    task_t (const task_t &) = delete;
    task_t &operator= (const task_t &) = delete;
    ~task_t ()
    {
        if (_handle) {
            _handle.destroy ();
        }
    }

    bool await_ready () const noexcept { return true; }
    void await_suspend (std::coroutine_handle<>) const noexcept {}
    void await_resume () { result ().operator bool (); }

    const result_t<void> &result () const
    {
        if (_handle) {
            return _handle.promise ().result;
        }
        return *_result;
    }

    result_t<void> consume_result ()
    {
        if (_handle) {
            return std::move (_handle.promise ().result);
        }
        return std::move (*_result);
    }

    void on_completed (std::function<void (result_t<void>)> callback)
    {
        if (callback) {
            callback (consume_result ());
        }
    }

  private:
    explicit task_t (std::coroutine_handle<promise_type> handle) : _handle (handle) {}

    std::coroutine_handle<promise_type> _handle;
    std::optional<result_t<void>> _result;
};

} // namespace zlink::stream_connector
