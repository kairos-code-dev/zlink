/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#ifndef ZLINK_HAVE_WINDOWS
#include <unistd.h>
#endif

#include <new>

#include "core/ctx.hpp"
#include "core/ctx_bootstrap.hpp"
#include "core/io_thread.hpp"
#include "core/reaper.hpp"
#include "services/control/service_control_runtime.hpp"

namespace
{
const int ctx_bootstrap_retry_count = 50;
const int term_and_reaper_threads_count = 2;
}

void zlink::ctx_bootstrap_t::cleanup_failed_start_locked (ctx_t &ctx_)
{
    if (ctx_._service_control_runtime) {
        ctx_._service_control_runtime->stop ();
        delete ctx_._service_control_runtime;
        ctx_._service_control_runtime = NULL;
    }

    const ctx_t::io_threads_t::size_type io_threads_size =
      ctx_._io_threads.size ();
    for (ctx_t::io_threads_t::size_type i = 0; i != io_threads_size; ++i) {
        ctx_._io_threads[i]->stop ();
        LIBZLINK_DELETE (ctx_._io_threads[i]);
    }
    ctx_._io_threads.clear ();

    if (ctx_._reaper) {
        ctx_._reaper->stop ();
        LIBZLINK_DELETE (ctx_._reaper);
        ctx_._reaper = NULL;
    }

    ctx_._slots.clear ();
    ctx_._empty_slots.clear ();
}

bool zlink::ctx_bootstrap_t::start_runtime_locked (ctx_t &ctx_)
{
    ctx_._opt_sync.lock ();
    const int mazlink = ctx_._max_sockets;
    const int ios = ctx_._io_thread_count;
    ctx_._opt_sync.unlock ();

    const int slot_count =
      mazlink + ios + term_and_reaper_threads_count;

    try {
        ctx_._slots.reserve (slot_count);
        ctx_._empty_slots.reserve (slot_count - term_and_reaper_threads_count);
    }
    catch (const std::bad_alloc &) {
        errno = ENOMEM;
        return false;
    }

    ctx_._slots.resize (term_and_reaper_threads_count);
    ctx_._slots[ctx_t::term_tid] = &ctx_._term_mailbox;

    ctx_._reaper = new (std::nothrow) reaper_t (&ctx_, ctx_t::reaper_tid);
    if (!ctx_._reaper) {
        errno = ENOMEM;
        cleanup_failed_start_locked (ctx_);
        return false;
    }
    if (!ctx_._reaper->get_mailbox ()->valid ()) {
        cleanup_failed_start_locked (ctx_);
        return false;
    }
    ctx_._slots[ctx_t::reaper_tid] = ctx_._reaper->get_mailbox ();
    ctx_._reaper->start ();

    ctx_._service_control_runtime =
      new (std::nothrow) service_control_runtime_t (&ctx_);
    if (!ctx_._service_control_runtime) {
        errno = ENOMEM;
        cleanup_failed_start_locked (ctx_);
        return false;
    }
    if (!ctx_._service_control_runtime->start ()) {
        cleanup_failed_start_locked (ctx_);
        return false;
    }

    ctx_._slots.resize (slot_count, NULL);
    for (int i = term_and_reaper_threads_count;
         i != ios + term_and_reaper_threads_count; ++i) {
        io_thread_t *io_thread = new (std::nothrow) io_thread_t (&ctx_, i);
        if (!io_thread) {
            errno = ENOMEM;
            cleanup_failed_start_locked (ctx_);
            return false;
        }
        if (!io_thread->get_mailbox ()->valid ()) {
            LIBZLINK_DELETE (io_thread);
            cleanup_failed_start_locked (ctx_);
            return false;
        }
        ctx_._io_threads.push_back (io_thread);
        ctx_._slots[i] = io_thread->get_mailbox ();
        io_thread->start ();
    }

    for (int32_t i = static_cast<int32_t> (ctx_._slots.size ()) - 1;
         i >= static_cast<int32_t> (ios) + term_and_reaper_threads_count;
         --i) {
        ctx_._empty_slots.push_back (i);
    }

    ctx_._starting = false;
    return true;
}

zlink::service_control_runtime_t *
zlink::ctx_bootstrap_t::ensure_service_runtime (ctx_t &ctx_)
{
    int last_errno = ENOTSUP;
    for (int attempt = 0; attempt < ctx_bootstrap_retry_count; ++attempt) {
        ctx_._slot_sync.lock ();
        if (ctx_._terminating) {
            ctx_._slot_sync.unlock ();
            errno = ETERM;
            return NULL;
        }
        if (ctx_._service_control_runtime) {
            service_control_runtime_t *runtime = ctx_._service_control_runtime;
            ctx_._slot_sync.unlock ();
            return runtime;
        }
        if (!ctx_._starting) {
            ctx_._slot_sync.unlock ();
            errno = ENOTSUP;
            return NULL;
        }

        const bool started = start_runtime_locked (ctx_);
        service_control_runtime_t *runtime = ctx_._service_control_runtime;
        last_errno = errno;
        ctx_._slot_sync.unlock ();
        if (started && runtime)
            return runtime;
#ifndef ZLINK_HAVE_WINDOWS
        usleep (1000);
#endif
    }

    errno = last_errno;
    return NULL;
}
