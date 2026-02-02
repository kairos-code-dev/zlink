/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"
#include "core/platform_thread.hpp"
#include "utils/err.hpp"
#include "utils/macros.hpp"

#if defined ZLINK_HAVE_WINDOWS
#include "utils/windows.hpp"
#include <process.h>
#if defined __MINGW32__
#include "pthread.h"
#endif
#elif defined ZLINK_HAVE_VXWORKS
#include <vxWorks.h>
#include <taskLib.h>
#include <limits.h>
#else
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/resource.h>
#endif

namespace
{
struct thread_start_t
{
    zlink::thread_entry_t entry;
    void *arg;
};
}

#if defined ZLINK_HAVE_WINDOWS
namespace
{
#if defined _WIN32_WCE
static DWORD thread_routine (LPVOID arg_)
#else
static unsigned int __stdcall thread_routine (void *arg_)
#endif
{
    thread_start_t *start = static_cast<thread_start_t *> (arg_);
    zlink::thread_entry_t entry = start->entry;
    void *arg = start->arg;
    delete start;
    entry (arg);
    return 0;
}
}

struct zlink::platform_thread_t
{
    HANDLE handle;
    unsigned int thread_id;
};

zlink::platform_thread_t *zlink::platform_thread_start (thread_entry_t entry_,
                                                    void *arg_)
{
    platform_thread_t *thread = new (std::nothrow) platform_thread_t ();
    alloc_assert (thread);

    thread_start_t *start = new (std::nothrow) thread_start_t ();
    alloc_assert (start);
    start->entry = entry_;
    start->arg = arg_;

    unsigned int stack = 0;
#if defined _WIN64
    stack = 0x400000;
#endif

#if defined _WIN32_WCE
    thread->handle = (HANDLE) CreateThread (NULL, stack, &thread_routine,
                                            start, 0, &thread->thread_id);
#else
    thread->handle = (HANDLE) _beginthreadex (NULL, stack, &thread_routine,
                                              start, 0, &thread->thread_id);
#endif
    win_assert (thread->handle != NULL);
    return thread;
}

void zlink::platform_thread_stop (platform_thread_t *thread_)
{
    if (!thread_)
        return;
    const DWORD rc = WaitForSingleObject (thread_->handle, INFINITE);
    win_assert (rc != WAIT_FAILED);
    const BOOL rc2 = CloseHandle (thread_->handle);
    win_assert (rc2 != 0);
    thread_->handle = NULL;
}

bool zlink::platform_thread_is_current (const platform_thread_t *thread_)
{
    if (!thread_)
        return false;
    return GetCurrentThreadId () == thread_->thread_id;
}

void zlink::platform_thread_destroy (platform_thread_t *thread_)
{
    if (!thread_)
        return;
    if (thread_->handle)
        CloseHandle (thread_->handle);
    delete thread_;
}

void zlink::platform_thread_apply_scheduling (
  int priority_, int scheduling_policy_, const std::set<int> &affinity_cpus_)
{
    LIBZLINK_UNUSED (priority_);
    LIBZLINK_UNUSED (scheduling_policy_);
    LIBZLINK_UNUSED (affinity_cpus_);
}

void zlink::platform_thread_apply_name (const char *name_)
{
    if (!name_ || !name_[0] || !IsDebuggerPresent ())
        return;

#ifdef _MSC_VER
    struct thread_info_t
    {
        DWORD _type;
        LPCSTR _name;
        DWORD _thread_id;
        DWORD _flags;
    };

    thread_info_t thread_info;
    thread_info._type = 0x1000;
    thread_info._name = name_;
    thread_info._thread_id = -1;
    thread_info._flags = 0;

    __try {
        const DWORD MS_VC_EXCEPTION = 0x406D1388;
        RaiseException (MS_VC_EXCEPTION, 0,
                        sizeof (thread_info) / sizeof (ULONG_PTR),
                        (ULONG_PTR *) &thread_info);
    }
    __except (EXCEPTION_CONTINUE_EXECUTION) {
    }
#elif defined(__MINGW32__)
    int rc = pthread_setname_np (pthread_self (), name_);
    if (rc)
        return;
#else
    // not implemented
#endif
}

#elif defined ZLINK_HAVE_VXWORKS

namespace
{
static void *thread_routine (void *arg_)
{
    thread_start_t *start = static_cast<thread_start_t *> (arg_);
    zlink::thread_entry_t entry = start->entry;
    void *arg = start->arg;
    delete start;
    entry (arg);
    return NULL;
}
}

struct zlink::platform_thread_t
{
    int descriptor;
};

zlink::platform_thread_t *zlink::platform_thread_start (thread_entry_t entry_,
                                                    void *arg_)
{
    platform_thread_t *thread = new (std::nothrow) platform_thread_t ();
    alloc_assert (thread);

    thread_start_t *start = new (std::nothrow) thread_start_t ();
    alloc_assert (start);
    start->entry = entry_;
    start->arg = arg_;

    thread->descriptor = taskSpawn (
      NULL, 100, 0, 4000, (FUNCPTR) thread_routine, (int) start, 0, 0, 0, 0, 0,
      0, 0, 0, 0);
    if (thread->descriptor == NULL || thread->descriptor <= 0) {
        delete start;
        delete thread;
        return NULL;
    }
    return thread;
}

void zlink::platform_thread_stop (platform_thread_t *thread_)
{
    if (!thread_)
        return;
    while ((thread_->descriptor != NULL || thread_->descriptor > 0)
           && taskIdVerify (thread_->descriptor) == 0) {
    }
}

bool zlink::platform_thread_is_current (const platform_thread_t *thread_)
{
    if (!thread_)
        return false;
    return taskIdSelf () == thread_->descriptor;
}

void zlink::platform_thread_destroy (platform_thread_t *thread_)
{
    if (!thread_)
        return;
    if (thread_->descriptor != NULL || thread_->descriptor > 0)
        taskDelete (thread_->descriptor);
    delete thread_;
}

void zlink::platform_thread_apply_scheduling (
  int priority_, int scheduling_policy_, const std::set<int> &affinity_cpus_)
{
    LIBZLINK_UNUSED (scheduling_policy_);
    LIBZLINK_UNUSED (affinity_cpus_);
    int priority = (priority_ >= 0 ? priority_ : 100);
    priority = (priority < UCHAR_MAX ? priority : 100);
    taskPrioritySet (taskIdSelf (), priority);
}

void zlink::platform_thread_apply_name (const char *name_)
{
    LIBZLINK_UNUSED (name_);
}

#else

namespace
{
static void *thread_routine (void *arg_)
{
#if !defined ZLINK_HAVE_OPENVMS && !defined ZLINK_HAVE_ANDROID
    sigset_t signal_set;
    int rc = sigfillset (&signal_set);
    errno_assert (rc == 0);
    rc = pthread_sigmask (SIG_BLOCK, &signal_set, NULL);
    posix_assert (rc);
#endif
    thread_start_t *start = static_cast<thread_start_t *> (arg_);
    zlink::thread_entry_t entry = start->entry;
    void *arg = start->arg;
    delete start;
    return entry (arg);
}
}

struct zlink::platform_thread_t
{
    pthread_t handle;
};

zlink::platform_thread_t *zlink::platform_thread_start (thread_entry_t entry_,
                                                    void *arg_)
{
    platform_thread_t *thread = new (std::nothrow) platform_thread_t ();
    alloc_assert (thread);

    thread_start_t *start = new (std::nothrow) thread_start_t ();
    alloc_assert (start);
    start->entry = entry_;
    start->arg = arg_;

    int rc = pthread_create (&thread->handle, NULL, thread_routine, start);
    posix_assert (rc);
    return thread;
}

void zlink::platform_thread_stop (platform_thread_t *thread_)
{
    if (!thread_)
        return;
    int rc = pthread_join (thread_->handle, NULL);
    posix_assert (rc);
}

bool zlink::platform_thread_is_current (const platform_thread_t *thread_)
{
    if (!thread_)
        return false;
    return bool (pthread_equal (pthread_self (), thread_->handle));
}

void zlink::platform_thread_destroy (platform_thread_t *thread_)
{
    if (!thread_)
        return;
    delete thread_;
}

void zlink::platform_thread_apply_scheduling (
  int priority_, int scheduling_policy_, const std::set<int> &affinity_cpus_)
{
#if defined _POSIX_THREAD_PRIORITY_SCHEDULING                                  \
  && _POSIX_THREAD_PRIORITY_SCHEDULING >= 0
    int policy = 0;
    struct sched_param param;

#if _POSIX_THREAD_PRIORITY_SCHEDULING == 0                                     \
  && defined _SC_THREAD_PRIORITY_SCHEDULING
    if (sysconf (_SC_THREAD_PRIORITY_SCHEDULING) < 0) {
        return;
    }
#endif
    int rc = pthread_getschedparam (pthread_self (), &policy, &param);
    posix_assert (rc);

    if (scheduling_policy_ != ZLINK_THREAD_SCHED_POLICY_DFLT) {
        policy = scheduling_policy_;
    }

    bool use_nice_instead_priority =
      (policy != SCHED_FIFO) && (policy != SCHED_RR);

    if (use_nice_instead_priority)
        param.sched_priority = 0;
    else if (priority_ != ZLINK_THREAD_PRIORITY_DFLT)
        param.sched_priority = priority_;

#ifdef __NetBSD__
    if (policy == SCHED_OTHER)
        param.sched_priority = -1;
#endif

    rc = pthread_setschedparam (pthread_self (), policy, &param);

#if defined(__FreeBSD_kernel__) || defined(__FreeBSD__)
    if (rc == ENOSYS)
        return;
#endif

    posix_assert (rc);

#if !defined ZLINK_HAVE_VXWORKS
    if (use_nice_instead_priority && priority_ != ZLINK_THREAD_PRIORITY_DFLT
        && priority_ > 0) {
        rc = nice (-20);
        errno_assert (rc != -1);
    }
#endif

#ifdef ZLINK_HAVE_PTHREAD_SET_AFFINITY
    if (!affinity_cpus_.empty ()) {
        cpu_set_t cpuset;
        CPU_ZERO (&cpuset);
        for (std::set<int>::const_iterator it = affinity_cpus_.begin (),
                                           end = affinity_cpus_.end ();
             it != end; it++) {
            CPU_SET ((int) (*it), &cpuset);
        }
        rc = pthread_setaffinity_np (pthread_self (), sizeof (cpu_set_t),
                                     &cpuset);
        posix_assert (rc);
    }
#endif
#else
    LIBZLINK_UNUSED (priority_);
    LIBZLINK_UNUSED (scheduling_policy_);
    LIBZLINK_UNUSED (affinity_cpus_);
#endif
}

void zlink::platform_thread_apply_name (const char *name_)
{
    if (!name_ || !name_[0])
        return;

#if defined(ZLINK_HAVE_ANDROID)
    return;
#endif

#if defined(ZLINK_HAVE_PTHREAD_SETNAME_1)
    int rc = pthread_setname_np (name_);
    if (rc)
        return;
#elif defined(ZLINK_HAVE_PTHREAD_SETNAME_2)
    int rc = pthread_setname_np (pthread_self (), name_);
    if (rc)
        return;
#elif defined(ZLINK_HAVE_PTHREAD_SETNAME_3)
    int rc = pthread_setname_np (pthread_self (), name_, NULL);
    if (rc)
        return;
#elif defined(ZLINK_HAVE_PTHREAD_SET_NAME)
    pthread_set_name_np (pthread_self (), name_);
#endif
}

#endif
