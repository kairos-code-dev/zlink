/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_TIMERS_HPP_INCLUDED
#define ZLINK_CPP_TIMERS_HPP_INCLUDED

#include "error.hpp"

namespace zlink
{

class timer_t
{
  public:
    timer_t () : _timer (zlink_timer_new ()) {}

    explicit timer_t (void *timer_) : _timer (timer_) {}

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
        void *timer = zlink_spot_timer_new (spot_.handle ());
        if (!timer)
            throw config_error_t (config_result_t::invalid_handle, zlink_errno ());
        return timer_t (timer);
    }

    bool valid () const noexcept { return _timer != NULL; }
    void *handle () noexcept { return _timer; }
    const void *handle () const noexcept { return _timer; }

    void start (uint64_t interval_ns_, uint64_t repeat_count_)
    {
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (
            zlink_timer_start (_timer, interval_ns_, repeat_count_)));
    }

    void stop ()
    {
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (zlink_timer_stop (_timer)));
    }

    void recv (uint64_t *fire_count_out_, int flags = 0)
    {
        detail::throw_if_failed<recv_error_t> (
          static_cast<recv_result_t> (zlink_timer_recv (_timer, fire_count_out_)));
        if (flags != 0)
            (void) flags;
    }

    void set_handler (zlink_timer_handler_fn handler_, void *userdata_ = NULL)
    {
        detail::throw_if_failed<handler_error_t> (
          static_cast<handler_result_t> (
            zlink_timer_handler (_timer, handler_, userdata_)));
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
    void *_timer;
};

} // namespace zlink

#endif
