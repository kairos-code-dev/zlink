/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_FAST_MUTEX_HPP_INCLUDED__
#define __ZLINK_FAST_MUTEX_HPP_INCLUDED__

#include "utils/err.hpp"
#include "utils/macros.hpp"

#if defined(ZLINK_HAVE_WINDOWS) && !defined(ZLINK_USE_CV_IMPL_PTHREADS)

#include "utils/mutex.hpp"

namespace zlink
{
typedef mutex_t fast_mutex_t;
}

#elif defined ZLINK_HAVE_VXWORKS

#include "utils/mutex.hpp"

namespace zlink
{
typedef mutex_t fast_mutex_t;
}

#else

#include <pthread.h>
#include <stdint.h>
#include <atomic>

#if defined(ZLINK_HAVE_LINUX)
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace zlink
{
class fast_mutex_t
{
  public:
    inline fast_mutex_t () : _owner (0), _depth (0)
    {
        int rc = pthread_mutex_init (&_mutex, NULL);
        posix_assert (rc);
    }

    inline ~fast_mutex_t ()
    {
        int rc = pthread_mutex_destroy (&_mutex);
        posix_assert (rc);
    }

    inline void lock ()
    {
        const uint64_t tid = current_thread_id ();
        if (_owner.load (std::memory_order_relaxed) == tid) {
            ++_depth;
            return;
        }

        int rc = pthread_mutex_lock (&_mutex);
        posix_assert (rc);
        _owner.store (tid, std::memory_order_relaxed);
        _depth = 1;
    }

    inline bool try_lock ()
    {
        const uint64_t tid = current_thread_id ();
        if (_owner.load (std::memory_order_relaxed) == tid) {
            ++_depth;
            return true;
        }

        int rc = pthread_mutex_trylock (&_mutex);
        if (rc == EBUSY)
            return false;
        posix_assert (rc);
        _owner.store (tid, std::memory_order_relaxed);
        _depth = 1;
        return true;
    }

    inline void unlock ()
    {
        const uint64_t tid = current_thread_id ();
        zlink_assert (_owner.load (std::memory_order_relaxed) == tid);
        if (_depth > 1) {
            --_depth;
            return;
        }

        _depth = 0;
        _owner.store (0, std::memory_order_relaxed);
        int rc = pthread_mutex_unlock (&_mutex);
        posix_assert (rc);
    }

  private:
    static inline uint64_t current_thread_id ()
    {
#if defined(ZLINK_HAVE_LINUX)
        static thread_local uint64_t tid = 0;
        if (tid == 0)
            tid = static_cast<uint64_t> (syscall (SYS_gettid));
        return tid;
#else
        return static_cast<uint64_t> (
          reinterpret_cast<uintptr_t> (pthread_self ()));
#endif
    }

    pthread_mutex_t _mutex;
    std::atomic<uint64_t> _owner;
    unsigned int _depth;

    ZLINK_NON_COPYABLE_NOR_MOVABLE (fast_mutex_t)
};
}

#endif

namespace zlink
{
struct scoped_fast_lock_t
{
    scoped_fast_lock_t (fast_mutex_t &mutex_) : _mutex (mutex_)
    {
        _mutex.lock ();
    }

    ~scoped_fast_lock_t () { _mutex.unlock (); }

  private:
    fast_mutex_t &_mutex;

    ZLINK_NON_COPYABLE_NOR_MOVABLE (scoped_fast_lock_t)
};

struct scoped_optional_fast_lock_t
{
    scoped_optional_fast_lock_t (fast_mutex_t *mutex_) : _mutex (mutex_)
    {
        if (_mutex != NULL)
            _mutex->lock ();
    }

    ~scoped_optional_fast_lock_t ()
    {
        if (_mutex != NULL)
            _mutex->unlock ();
    }

  private:
    fast_mutex_t *_mutex;

    ZLINK_NON_COPYABLE_NOR_MOVABLE (scoped_optional_fast_lock_t)
};
}

#endif
