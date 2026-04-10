/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_TIMERS_HPP_INCLUDED
#define ZLINK_CPP_TIMERS_HPP_INCLUDED

#include "common.hpp"

namespace zlink
{

class timer_t
{
  public:
    timer_t () : _timer (zlink_timer_new ()) {}

    explicit timer_t (void *timer_) : _timer (timer_) {}

    ~timer_t () { destroy (); }

    timer_t (timer_t &&other) noexcept : _timer (other._timer)
    {
        other._timer = NULL;
    }

    timer_t &operator= (timer_t &&other) noexcept
    {
        if (this == &other)
            return *this;
        destroy ();
        _timer = other._timer;
        other._timer = NULL;
        return *this;
    }

    timer_t (const timer_t &) = delete;
    timer_t &operator= (const timer_t &) = delete;

    template<typename SpotLike>
    static timer_t from_spot (SpotLike &spot_)
    {
        return timer_t (zlink_spot_timer_new (spot_.handle ()));
    }

    bool valid () const noexcept { return _timer != NULL; }

    void *handle () noexcept { return _timer; }
    const void *handle () const noexcept { return _timer; }

    int start (uint64_t interval_ns_, uint64_t repeat_count_)
    {
        return zlink_timer_start (_timer, interval_ns_, repeat_count_);
    }

    int stop () { return zlink_timer_stop (_timer); }

    int recv (uint64_t *fire_count_out_, int flags_ = 0)
    {
        return zlink_timer_recv (_timer, fire_count_out_, flags_);
    }

    int set_handler (zlink_timer_handler_fn handler_, void *userdata_ = NULL)
    {
        return zlink_timer_handler (_timer, handler_, userdata_);
    }

    int destroy ()
    {
        if (!_timer)
            return 0;

        void *timer = _timer;
        const int rc = zlink_timer_destroy (&timer);
        if (rc == 0)
            _timer = NULL;
        return rc;
    }

  private:
    void *_timer;
};

} // namespace zlink

#endif
