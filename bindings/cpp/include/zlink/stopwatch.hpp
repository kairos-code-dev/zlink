/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_STOPWATCH_HPP_INCLUDED
#define ZLINK_CPP_STOPWATCH_HPP_INCLUDED

#include "common.hpp"

namespace zlink
{

class stopwatch_t
{
  public:
    stopwatch_t () : _watch (zlink_stopwatch_start ()) {}
    ~stopwatch_t () { _watch = NULL; }

    stopwatch_t (stopwatch_t &&other) noexcept : _watch (other._watch)
    {
        other._watch = NULL;
    }

    stopwatch_t &operator= (stopwatch_t &&other) noexcept
    {
        if (this == &other)
            return *this;
        _watch = other._watch;
        other._watch = NULL;
        return *this;
    }

    stopwatch_t (const stopwatch_t &) = delete;
    stopwatch_t &operator= (const stopwatch_t &) = delete;

    unsigned long intermediate ()
    {
        return zlink_stopwatch_intermediate (_watch);
    }

    unsigned long stop ()
    {
        return zlink_stopwatch_stop (_watch);
    }

  private:
    void *_watch;
};

} // namespace zlink

#endif
