/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_TIMERS_HPP_INCLUDED
#define ZLINK_CPP_TIMERS_HPP_INCLUDED

#include "common.hpp"

namespace zlink
{

class timers_t
{
  public:
    timers_t () : _timers (zlink_timers_new ()) {}
    ~timers_t () { destroy (); }

    timers_t (timers_t &&other) noexcept : _timers (other._timers)
    {
        other._timers = NULL;
    }

    timers_t &operator= (timers_t &&other) noexcept
    {
        if (this == &other)
            return *this;
        destroy ();
        _timers = other._timers;
        other._timers = NULL;
        return *this;
    }

    timers_t (const timers_t &) = delete;
    timers_t &operator= (const timers_t &) = delete;

    int add (size_t interval_ms_, zlink_timer_fn handler_, void *arg_)
    {
        return zlink_timers_add (_timers, interval_ms_, handler_, arg_);
    }

    int cancel (int timer_id_)
    {
        return zlink_timers_cancel (_timers, timer_id_);
    }

    int set_interval (int timer_id_, size_t interval_ms_)
    {
        return zlink_timers_set_interval (_timers, timer_id_, interval_ms_);
    }

    int reset (int timer_id_)
    {
        return zlink_timers_reset (_timers, timer_id_);
    }

    long timeout () const
    {
        return _timers ? zlink_timers_timeout (_timers) : -1;
    }

    int execute ()
    {
        return zlink_timers_execute (_timers);
    }

    int destroy ()
    {
        if (!_timers)
            return 0;

        void *tmp = _timers;
        _timers = NULL;
        return zlink_timers_destroy (&tmp);
    }

    void *handle () const { return _timers; }

  private:
    void *_timers;
};

} // namespace zlink

#endif
