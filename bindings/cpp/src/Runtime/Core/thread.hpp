/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_THREAD_HPP_INCLUDED
#define ZLINK_CPP_THREAD_HPP_INCLUDED

#include <zlink/Contracts/Core/routing_id.hpp>

#include <functional>

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

    explicit thread_t (std::function<void()> fn_)
        : _thread (NULL)
    {
        std::function<void()> *state =
          new std::function<void()> (std::move (fn_));
        _thread = zlink_thread_start (&thread_t::run, state);
        if (!_thread)
            delete state;
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
    static void run (void *arg_)
    {
        std::function<void()> *fn =
          static_cast<std::function<void()> *> (arg_);
        if (!fn)
            return;
        try {
            (*fn) ();
        } catch (...) {
        }
        delete fn;
    }

    void *_thread;
};

} // namespace zlink

#endif
