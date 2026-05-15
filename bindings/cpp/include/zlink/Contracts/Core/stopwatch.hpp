/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_STOPWATCH_HPP_INCLUDED
#define ZLINK_CPP_STOPWATCH_HPP_INCLUDED

#include "common.hpp"

namespace zlink
{

/**
 * @brief Simple elapsed-time stopwatch wrapper.
 */
class stopwatch_t
{
  public:
    /**
     * @brief Start a new stopwatch.
     */
    stopwatch_t () : _watch (zlink_stopwatch_start ()) {}
    /**
     * @brief Destroy wrapper handle.
     */
    ~stopwatch_t () { close (); }

    stopwatch_t (stopwatch_t &&other) noexcept : _watch (other._watch)
    {
        other._watch = NULL;
    }

    stopwatch_t &operator= (stopwatch_t &&other) noexcept
    {
        if (this == &other)
            return *this;
        close ();
        _watch = other._watch;
        other._watch = NULL;
        return *this;
    }

    stopwatch_t (const stopwatch_t &) = delete;
    stopwatch_t &operator= (const stopwatch_t &) = delete;

    /**
     * @brief Read elapsed microseconds without stopping.
     * @return Elapsed microseconds.
     */
    unsigned long intermediate ()
    {
        if (!_watch)
            return 0;
        return zlink_stopwatch_intermediate (_watch);
    }

    /**
     * @brief Stop the stopwatch and return elapsed microseconds.
     * @return Elapsed microseconds.
     */
    unsigned long stop ()
    {
        if (!_watch)
            return 0;
        const unsigned long elapsed = zlink_stopwatch_stop (_watch);
        _watch = NULL;
        return elapsed;
    }

  private:
    void close () noexcept
    {
        if (_watch) {
            (void) zlink_stopwatch_stop (_watch);
            _watch = NULL;
        }
    }

    void *_watch;
};

} // namespace zlink

#endif
