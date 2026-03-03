/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_THREAD_HPP_INCLUDED
#define ZLINK_CPP_THREAD_HPP_INCLUDED

#include "common.hpp"

namespace zlink
{

class thread_t
{
  public:
    thread_t () : _thread (NULL) {}

    explicit thread_t (zlink_thread_fn *fn_, void *arg_)
        : _thread (zlink_threadstart (fn_, arg_))
    {
    }

    ~thread_t () { close (); }

    thread_t (thread_t &&other) noexcept : _thread (other._thread)
    {
        other._thread = NULL;
    }

    thread_t &operator= (thread_t &&other) noexcept
    {
        if (this == &other)
            return *this;

        close ();
        _thread = other._thread;
        other._thread = NULL;
        return *this;
    }

    thread_t (const thread_t &) = delete;
    thread_t &operator= (const thread_t &) = delete;

    void close ()
    {
        if (_thread) {
            zlink_threadclose (_thread);
            _thread = NULL;
        }
    }

  private:
    void *_thread;
};

} // namespace zlink

#endif
