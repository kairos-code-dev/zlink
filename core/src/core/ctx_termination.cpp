/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#ifndef ZLINK_HAVE_WINDOWS
#include <unistd.h>
#endif

#include "core/ctx.hpp"
#include "core/ctx_termination.hpp"
#include "core/io_thread.hpp"
#include "core/reaper.hpp"
#include "sockets/socket_base.hpp"
#include "services/control/service_control_runtime.hpp"
#include "utils/err.hpp"

void zlink::ctx_termination_t::teardown_runtime (ctx_t &ctx_)
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
    }
    for (ctx_t::io_threads_t::size_type i = 0; i != io_threads_size; ++i) {
        LIBZLINK_DELETE (ctx_._io_threads[i]);
    }
    ctx_._io_threads.clear ();

    LIBZLINK_DELETE (ctx_._reaper);
    ctx_._reaper = NULL;
}

void zlink::ctx_termination_t::flush_pending_inproc_locked (ctx_t &ctx_)
{
    const ctx_t::pending_connections_t pending = ctx_._pending_connections;
    const bool saved_terminating = ctx_._terminating;
    ctx_._terminating = false;
    ctx_._slot_sync.unlock ();
    for (ctx_t::pending_connections_t::const_iterator it = pending.begin (),
                                                       end = pending.end ();
         it != end; ++it) {
        socket_base_t *socket = ctx_.create_socket (ZLINK_CORE_SOCKET_PAIR);
        zlink_assert (socket);
        socket->bind (it->first.c_str ());
        socket->close ();
    }
    ctx_._slot_sync.lock ();
    ctx_._terminating = saved_terminating;
}

bool zlink::ctx_termination_t::begin_shutdown_locked (
  ctx_t &ctx_,
  bool allow_fork_cleanup_)
{
    if (ctx_._starting) {
        ctx_._terminating = true;
        return false;
    }

#ifdef HAVE_FORK
    if (allow_fork_cleanup_ && ctx_._pid != getpid ()) {
        for (ctx_t::sockets_t::size_type i = 0, size = ctx_._sockets.size ();
             i != size; ++i) {
            ctx_._sockets[i]->get_mailbox ()->forked ();
        }
        ctx_._term_mailbox.forked ();
    }
#else
    LIBZLINK_UNUSED (allow_fork_cleanup_);
#endif

    const bool restarted = ctx_._terminating;
    ctx_._terminating = true;
    if (restarted)
        return true;

    ctx_.debug_dump_sockets_locked ("terminate-before-stop");
    for (ctx_t::sockets_t::size_type i = 0, size = ctx_._sockets.size ();
         i != size; ++i) {
        ctx_._sockets[i]->stop ();
    }
    if (ctx_._sockets.empty ())
        ctx_._reaper->stop ();

    return true;
}

int zlink::ctx_termination_t::wait_for_reaper_done (ctx_t &ctx_)
{
    command_t cmd;
    const int rc = ctx_._term_mailbox.recv (&cmd, -1);
    if (rc == -1 && errno == EINTR)
        return -1;
    errno_assert (rc == 0);
    zlink_assert (cmd.type == command_t::done);
    return 0;
}
