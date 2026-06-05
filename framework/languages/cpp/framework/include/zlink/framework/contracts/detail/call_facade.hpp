/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/framework/contracts/channels/pending_operation.hpp>
#include <zlink/framework/contracts/dispatch/task.hpp>

#include <chrono>
#include <functional>
#include <utility>

namespace zlink::framework::detail
{

template <typename T> class immediate_call_state_t
{
  public:
    explicit immediate_call_state_t (result_t<T> result) : _result (std::move (result)) {}

    void set_timeout (std::chrono::milliseconds timeout) { _timeout = timeout; }

    task_t<T> submit () { return task_t<T> (_result); }

    pending_operation_t submit (std::function<void (result_t<T>)> callback)
    {
        callback (_result);
        return pending_operation_t::make_completed ();
    }

  private:
    result_t<T> _result;
    std::chrono::milliseconds _timeout{0};
};

template <> class immediate_call_state_t<void>
{
  public:
    explicit immediate_call_state_t (result_t<void> result) : _result (std::move (result)) {}

    void set_timeout (std::chrono::milliseconds timeout) { _timeout = timeout; }

    task_t<void> submit () { return task_t<void> (_result); }

    pending_operation_t submit (std::function<void (result_t<void>)> callback)
    {
        callback (_result);
        return pending_operation_t::make_completed ();
    }

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

    task_t<TResult> submit () { return _state.submit (); }

    pending_operation_t submit (std::function<void (result_t<TResult>)> callback)
    {
        return _state.submit (std::move (callback));
    }

  protected:
    explicit call_facade_t (result_t<TResult> result) : _state (std::move (result)) {}

  private:
    immediate_call_state_t<TResult> _state;
};

} // namespace zlink::framework::detail
