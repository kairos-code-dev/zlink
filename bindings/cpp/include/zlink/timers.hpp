/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_TIMERS_HPP_INCLUDED
#define ZLINK_CPP_TIMERS_HPP_INCLUDED

#include "error.hpp"

#include <chrono>

namespace zlink
{

class timer_t;
namespace service
{
class spot_t;
} // namespace service

namespace detail
{
inline void *native_handle (timer_t &timer_) noexcept;
inline const void *native_handle (const timer_t &timer_) noexcept;
inline void *native_handle (service::spot_t &spot_) noexcept;
inline const void *native_handle (const service::spot_t &spot_) noexcept;
} // namespace detail

class timer_t
{
  public:
    timer_t () : _timer (zlink_timer_new ()) {}

    ~timer_t ()
    {
        try {
            destroy ();
        } catch (...) {
        }
    }

    timer_t (timer_t &&other) noexcept : _timer (other._timer)
    {
        other._timer = NULL;
    }

    timer_t &operator= (timer_t &&other) noexcept
    {
        if (this == &other)
            return *this;
        try {
            destroy ();
        } catch (...) {
        }
        _timer = other._timer;
        other._timer = NULL;
        return *this;
    }

    timer_t (const timer_t &) = delete;
    timer_t &operator= (const timer_t &) = delete;

    template<typename SpotLike>
    static timer_t from_spot (SpotLike &spot_)
    {
        void *timer = zlink_spot_timer_new (zlink::detail::native_handle (spot_));
        if (!timer)
            throw config_error_t (config_result_t::invalid_handle, zlink_errno ());
        return timer_t (timer);
    }

    bool valid () const noexcept { return _timer != NULL; }

    template<class Rep, class Period>
    void start (std::chrono::duration<Rep, Period> interval_,
                uint64_t repeat_count_ = 0)
    {
        const uint64_t interval_ns = static_cast<uint64_t> (
          std::chrono::duration_cast<std::chrono::nanoseconds> (interval_)
            .count ());
        start_ns (interval_ns, repeat_count_);
    }

  private:
    void start_ns (uint64_t interval_ns_, uint64_t repeat_count_)
    {
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (
            zlink_timer_start (_timer, interval_ns_, repeat_count_)));
    }

  public:
    void stop ()
    {
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (zlink_timer_stop (_timer)));
    }

    std::optional<uint64_t> recv ()
    {
        uint64_t fire_count = 0;
        const recv_result_t result =
          static_cast<recv_result_t> (zlink_timer_recv (_timer, &fire_count));
        if (result == recv_result_t::no_data)
            return std::nullopt;
        if (result != recv_result_t::ok)
            throw recv_error_t (result, zlink_errno ());
        return std::optional<uint64_t> (fire_count);
    }

    void set_handler (std::function<void(uint64_t)> handler_)
    {
        _handler = std::move (handler_);
        detail::throw_if_failed<handler_error_t> (
          static_cast<handler_result_t> (
            zlink_timer_handler (_timer, &timer_t::handler_trampoline, this)));
    }

    void destroy ()
    {
        if (!_timer)
            return;

        void *timer = _timer;
        _timer = NULL;
        detail::throw_if_failed<close_error_t> (
          static_cast<close_result_t> (zlink_timer_destroy (&timer)));
    }

  private:
    explicit timer_t (void *timer_) : _timer (timer_) {}

    friend void *detail::native_handle (timer_t &timer_) noexcept;
    friend const void *
    detail::native_handle (const timer_t &timer_) noexcept;

    static void handler_trampoline (void *, uint64_t fire_count_, void *userdata_)
    {
        timer_t *self = static_cast<timer_t *> (userdata_);
        if (!self || !self->_handler)
            return;
        self->_handler (fire_count_);
    }

    void *_timer;
    std::function<void(uint64_t)> _handler;
};

namespace detail
{
inline void *native_handle (timer_t &timer_) noexcept
{
    return timer_._timer;
}

inline const void *native_handle (const timer_t &timer_) noexcept
{
    return timer_._timer;
}
} // namespace detail

} // namespace zlink

#endif
