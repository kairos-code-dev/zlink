/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_TIMERS_HPP_INCLUDED
#define ZLINK_CPP_TIMERS_HPP_INCLUDED

#include "common.hpp"

namespace zlink
{

/**
 * @brief RAII wrapper for zlink timer wheel API.
 */
class timers_t
{
  public:
    /**
     * @brief Create a timer collection.
     */
    timers_t () : _timers (zlink_timers_new ()) {}
    /**
     * @brief Destroy the timer collection.
     */
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

    /**
     * @brief Add a periodic timer.
     * @param interval_ms_ Interval in milliseconds.
     * @param handler_ Timer callback.
     * @param arg_ User pointer for callback.
     * @return Timer id (>=0) on success, -1 on failure.
     */
    int add (size_t interval_ms_, zlink_timer_fn handler_, void *arg_)
    {
        return zlink_timers_add (_timers, interval_ms_, handler_, arg_);
    }

    /**
     * @brief Cancel a timer.
     * @param timer_id_ Timer id.
     * @return 0 on success, -1 on failure.
     */
    int cancel (int timer_id_)
    {
        return zlink_timers_cancel (_timers, timer_id_);
    }

    /**
     * @brief Update timer interval.
     * @param timer_id_ Timer id.
     * @param interval_ms_ New interval in milliseconds.
     * @return 0 on success, -1 on failure.
     */
    int set_interval (int timer_id_, size_t interval_ms_)
    {
        return zlink_timers_set_interval (_timers, timer_id_, interval_ms_);
    }

    /**
     * @brief Reset timer next-fire time.
     * @param timer_id_ Timer id.
     * @return 0 on success, -1 on failure.
     */
    int reset (int timer_id_)
    {
        return zlink_timers_reset (_timers, timer_id_);
    }

    /**
     * @brief Query milliseconds until next timer event.
     * @return Timeout in milliseconds, or -1.
     */
    long timeout () const
    {
        return _timers ? zlink_timers_timeout (_timers) : -1;
    }

    /**
     * @brief Execute due timers.
     * @return Number of executed callbacks, or -1 on failure.
     */
    int execute ()
    {
        return zlink_timers_execute (_timers);
    }

    /**
     * @brief Explicitly destroy timer collection.
     * @return 0 on success, -1 on failure.
     */
    int destroy ()
    {
        if (!_timers)
            return 0;

        void *tmp = _timers;
        const int rc = zlink_timers_destroy (&tmp);
        if (rc == 0)
            _timers = NULL;
        return rc;
    }

    /**
     * @brief Access raw native timer handle.
     * @return Native handle pointer.
     */
    void *handle () const { return _timers; }

  private:
    void *_timers;
};

} // namespace zlink

#endif
