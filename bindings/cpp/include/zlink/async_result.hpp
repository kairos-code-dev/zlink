/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_ASYNC_RESULT_HPP_INCLUDED
#define ZLINK_CPP_ASYNC_RESULT_HPP_INCLUDED

#include "common.hpp"

#include <atomic>
#include <chrono>
#if defined(__cpp_impl_coroutine) || defined(__cpp_coroutines)
#include <coroutine>
#define ZLINK_CPP_HAS_COROUTINE_SUPPORT 1
#endif
#include <functional>
#include <future>
#include <memory>
#include <thread>

namespace zlink
{

template<typename T> class async_result_t
{
  public:
    explicit async_result_t (std::future<T> future_)
        : async_result_t (std::move (future_), std::function<void()> ())
    {
    }

    async_result_t (std::future<T> future_, std::function<void()> progress_)
        : _state (new shared_state_t (std::move (future_)))
    {
        _state->progress = std::move (progress_);
    }

    async_result_t (async_result_t &&) noexcept = default;
    async_result_t &operator= (async_result_t &&) noexcept = default;

    async_result_t (const async_result_t &) = delete;
    async_result_t &operator= (const async_result_t &) = delete;

    ZLINK_CPP_NODISCARD bool valid () const
    {
        return _state && _state->future.valid ();
    }

    void wait () const
    {
        if (!_state->progress) {
            _state->future.wait ();
            return;
        }

        while (_state->future.wait_for (std::chrono::milliseconds (0))
               != std::future_status::ready) {
            pump_progress_once ();
            (void) _state->future.wait_for (progress_slice ());
        }
    }

    template<typename Rep, typename Period>
    ZLINK_CPP_NODISCARD std::future_status
    wait_for (const std::chrono::duration<Rep, Period> &timeout_) const
    {
        if (!_state->progress || timeout_ <= timeout_.zero ())
            return _state->future.wait_for (timeout_);

        const std::chrono::steady_clock::time_point deadline =
          std::chrono::steady_clock::now ()
          + std::chrono::duration_cast<std::chrono::steady_clock::duration> (
            timeout_);

        while (true) {
            if (_state->future.wait_for (std::chrono::milliseconds (0))
                == std::future_status::ready)
                return std::future_status::ready;

            pump_progress_once ();

            const std::chrono::steady_clock::time_point now =
              std::chrono::steady_clock::now ();
            if (now >= deadline)
                return std::future_status::timeout;

            const std::chrono::steady_clock::duration remaining =
              deadline - now;
            const std::chrono::steady_clock::duration slice =
              remaining < progress_slice () ? remaining : progress_slice ();
            if (_state->future.wait_for (slice) == std::future_status::ready)
                return std::future_status::ready;
        }
    }

    template<typename Clock, typename Duration>
    ZLINK_CPP_NODISCARD std::future_status
    wait_until (const std::chrono::time_point<Clock, Duration> &deadline_) const
    {
        if (!_state->progress)
            return _state->future.wait_until (deadline_);

        const std::chrono::time_point<Clock, Duration> now = Clock::now ();
        if (deadline_ <= now)
            return _state->future.wait_for (std::chrono::milliseconds (0));

        return wait_for (deadline_ - now);
    }

    ZLINK_CPP_NODISCARD T get ()
    {
        wait ();
        return _state->future.get ();
    }

#if defined(ZLINK_CPP_HAS_COROUTINE_SUPPORT)
    ZLINK_CPP_NODISCARD bool await_ready () const
    {
        return wait_for (std::chrono::milliseconds (0))
               == std::future_status::ready;
    }

    void await_suspend (std::coroutine_handle<> continuation_)
    {
        std::shared_ptr<shared_state_t> state = _state;
        state->waiter_started.store (true);
        std::thread ([state, continuation_]() mutable {
            try {
                state->value.reset (new T (state->future.get ()));
            } catch (...) {
                state->error = std::current_exception ();
            }
            continuation_.resume ();
        }).detach ();
    }

    ZLINK_CPP_NODISCARD T await_resume ()
    {
        if (!_state->waiter_started.load ())
            return _state->future.get ();
        if (_state->error)
            std::rethrow_exception (_state->error);
        return std::move (*_state->value);
    }
#endif

  private:
    struct shared_state_t
    {
        explicit shared_state_t (std::future<T> future_)
            : future (std::move (future_)), waiter_started (false)
        {
        }

        std::future<T> future;
        std::function<void()> progress;
        std::atomic<bool> waiter_started;
#if defined(ZLINK_CPP_HAS_COROUTINE_SUPPORT)
        std::unique_ptr<T> value;
        std::exception_ptr error;
#endif
    };

    static std::chrono::milliseconds progress_slice ()
    {
        return std::chrono::milliseconds (1);
    }

    void pump_progress_once () const
    {
        if (_state->progress)
            _state->progress ();
    }

    std::shared_ptr<shared_state_t> _state;
};

} // namespace zlink

#endif
