/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace zlink::framework
{

namespace detail
{
class cancellation_state_t
{
  public:
    bool cancelled () const noexcept
    {
        std::lock_guard lock (_mutex);
        return _cancelled;
    }

    bool cancel ()
    {
        std::vector<std::function<void ()>> callbacks;
        {
            std::lock_guard lock (_mutex);
            if (_cancelled) {
                return false;
            }
            _cancelled = true;
            callbacks = std::move (_callbacks);
        }
        for (auto &callback : callbacks) {
            if (callback) {
                callback ();
            }
        }
        return true;
    }

    void register_callback (std::function<void ()> callback)
    {
        bool run_now = false;
        {
            std::lock_guard lock (_mutex);
            if (_cancelled) {
                run_now = true;
            } else {
                _callbacks.push_back (std::move (callback));
            }
        }
        if (run_now && callback) {
            callback ();
        }
    }

  private:
    mutable std::mutex _mutex;
    bool _cancelled = false;
    std::vector<std::function<void ()>> _callbacks;
};
} // namespace detail

class cancellation_token_t
{
  public:
    cancellation_token_t () noexcept = default;

    bool can_be_cancelled () const noexcept { return static_cast<bool> (_state); }

    bool is_cancellation_requested () const noexcept
    {
        return _state != nullptr && _state->cancelled ();
    }

    void register_callback (std::function<void ()> callback) const
    {
        if (_state) {
            _state->register_callback (std::move (callback));
        }
    }

  private:
    explicit cancellation_token_t (std::shared_ptr<detail::cancellation_state_t> state) noexcept :
        _state (std::move (state))
    {
    }

    std::shared_ptr<detail::cancellation_state_t> _state;

    friend class cancellation_token_source_t;
};

class cancellation_token_source_t
{
  public:
    cancellation_token_source_t () :
        _state (std::make_shared<detail::cancellation_state_t> ())
    {
    }

    cancellation_token_t token () const noexcept { return cancellation_token_t (_state); }

    bool cancel () const { return _state != nullptr && _state->cancel (); }

  private:
    std::shared_ptr<detail::cancellation_state_t> _state;
};

} // namespace zlink::framework
