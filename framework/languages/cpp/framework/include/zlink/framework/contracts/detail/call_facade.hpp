/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/framework/contracts/dispatch/task.hpp>

#include <chrono>
#include <utility>

namespace zlink::framework::detail
{

template <typename T> class immediate_call_state_t
{
  public:
    explicit immediate_call_state_t (result_t<T> result) : _result (std::move (result)) {}

    void set_timeout (std::chrono::milliseconds timeout) { _timeout = timeout; }

    task_t<T> submit_async () { return task_t<T> (_result); }

  private:
    result_t<T> _result;
    std::chrono::milliseconds _timeout{0};
};

template <> class immediate_call_state_t<void>
{
  public:
    explicit immediate_call_state_t (result_t<void> result) : _result (std::move (result)) {}

    void set_timeout (std::chrono::milliseconds timeout) { _timeout = timeout; }

    task_t<void> submit_async () { return task_t<void> (_result); }

  private:
    result_t<void> _result;
    std::chrono::milliseconds _timeout{0};
};

template <typename TDerived, typename TResult> class call_facade_t
{
  public:
    TDerived &timeout (std::chrono::milliseconds timeout)
    {
        _state.set_timeout (timeout);
        return static_cast<TDerived &> (*this);
    }

    task_t<TResult> submit_async () { return _state.submit_async (); }

  protected:
    explicit call_facade_t (result_t<TResult> result) : _state (std::move (result)) {}

  private:
    immediate_call_state_t<TResult> _state;
};

} // namespace zlink::framework::detail
