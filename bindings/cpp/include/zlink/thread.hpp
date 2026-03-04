/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_THREAD_HPP_INCLUDED
#define ZLINK_CPP_THREAD_HPP_INCLUDED

#include "common.hpp"

namespace zlink
{

/**
 * @brief RAII wrapper for a background zlink thread.
 */
class thread_t
{
  public:
    /**
     * @brief Construct an empty thread wrapper.
     */
    thread_t () : _thread (NULL) {}

    /**
     * @brief Start a new background thread.
     * @param fn_ Thread function.
     * @param arg_ User argument passed to `fn_`.
     */
    explicit thread_t (zlink_thread_fn *fn_, void *arg_)
        : _thread (zlink_thread_start (fn_, arg_))
    {
    }

    /**
     * @brief Join the thread if running.
     */
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

    /**
     * @brief Join the thread and clear handle.
     */
    void close ()
    {
        if (_thread) {
            zlink_thread_join (_thread);
            _thread = NULL;
        }
    }

  private:
    void *_thread;
};

} // namespace zlink

#endif
