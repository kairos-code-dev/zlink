/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#ifndef ZLINK_HAVE_WINDOWS
#include <unistd.h>
#endif

#include <vector>

#include "core/ctx.hpp"
#include "core/ctx_termination.hpp"
#include "sockets/common/socket_base.hpp"
#include "utils/err.hpp"

void zlink::ctx_termination_t::teardown_runtime (ctx_t &ctx_)
{
    ctx_._runtime_resources.teardown (ctx_, ctx_._socket_registry);
}

void zlink::ctx_termination_t::flush_pending_inproc_locked (ctx_t &ctx_)
{
    std::vector<std::string> pending_addresses;
    ctx_._inproc_registry.collect_pending_addresses (&pending_addresses);
    const bool saved_terminating = ctx_._terminating;
    ctx_._terminating = false;
    ctx_._slot_sync.unlock ();
    for (std::vector<std::string>::const_iterator it = pending_addresses.begin (),
                                                  end = pending_addresses.end ();
         it != end; ++it) {
        socket_base_t *socket = ctx_.create_socket (ZLINK_CORE_SOCKET_PAIR);
        zlink_assert (socket);
        socket->bind (it->c_str ());
        socket->close ();
    }
    ctx_._slot_sync.lock ();
    ctx_._terminating = saved_terminating;
}

bool zlink::ctx_termination_t::begin_shutdown_locked (ctx_t &ctx_, bool allow_fork_cleanup_)
{
    if (ctx_._starting) {
        ctx_._terminating = true;
        return false;
    }

#ifdef HAVE_FORK
    if (allow_fork_cleanup_ && ctx_._pid != getpid ()) {
        std::vector<socket_base_t *> sockets;
        ctx_._socket_registry.collect_sockets (&sockets);
        for (std::vector<socket_base_t *>::size_type i = 0, size = sockets.size (); i != size; ++i)
            sockets[i]->get_mailbox ()->forked ();
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
    std::vector<socket_base_t *> sockets;
    ctx_._socket_registry.collect_sockets (&sockets);
    for (std::vector<socket_base_t *>::size_type i = 0, size = sockets.size (); i != size; ++i)
        sockets[i]->stop ();
    if (sockets.empty ())
        ctx_._runtime_resources.stop_reaper ();

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
