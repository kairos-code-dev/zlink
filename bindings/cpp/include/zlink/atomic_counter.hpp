/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_ATOMIC_COUNTER_HPP_INCLUDED
#define ZLINK_CPP_ATOMIC_COUNTER_HPP_INCLUDED

#include "common.hpp"

namespace zlink
{

class atomic_counter_t
{
  public:
    atomic_counter_t () : _counter (zlink_atomic_counter_new ()) {}
    ~atomic_counter_t () { destroy (); }

    atomic_counter_t (atomic_counter_t &&other) noexcept : _counter (other._counter)
    {
        other._counter = NULL;
    }

    atomic_counter_t &operator= (atomic_counter_t &&other) noexcept
    {
        if (this == &other)
            return *this;
        destroy ();
        _counter = other._counter;
        other._counter = NULL;
        return *this;
    }

    atomic_counter_t (const atomic_counter_t &) = delete;
    atomic_counter_t &operator= (const atomic_counter_t &) = delete;

    void set (int value_) { zlink_atomic_counter_set (_counter, value_); }
    int inc () { return zlink_atomic_counter_inc (_counter); }
    int dec () { return zlink_atomic_counter_dec (_counter); }
    int value () const { return zlink_atomic_counter_value (_counter); }

    void destroy ()
    {
        if (!_counter)
            return;

        void *tmp = _counter;
        _counter = NULL;
        zlink_atomic_counter_destroy (&tmp);
    }

  private:
    void *_counter;
};

} // namespace zlink

#endif
