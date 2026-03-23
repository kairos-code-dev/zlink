/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_SERVICES_COMMON_SERVICE_SOCKET_REGISTRY_HPP_INCLUDED__
#define __ZLINK_SERVICES_COMMON_SERVICE_SOCKET_REGISTRY_HPP_INCLUDED__

#include "core/ctx.hpp"
#include "sockets/socket_base.hpp"
#include "sockets/socket_close_ops.hpp"
#include "utils/clock.hpp"
#include "utils/mutex.hpp"

#include <map>
#include <stdio.h>
#include <stdlib.h>

namespace zlink
{
class service_socket_registry_t
{
  public:
    service_socket_registry_t ()
    {
    }

    void register_socket (socket_base_t *socket_, bool accepting_new_)
    {
        if (!socket_)
            return;
        scoped_lock_t lock (_sync);
        if (!accepting_new_)
            return;
        _owned_sockets[socket_->socket_id ()] = socket_;
    }

    void unregister_socket (const socket_base_t *socket_)
    {
        if (!socket_)
            return;
        scoped_lock_t lock (_sync);
        _owned_sockets.erase (socket_->socket_id ());
        _closing_sockets.erase (socket_->socket_id ());
    }

    int close_socket (socket_base_t *&socket_)
    {
        if (!socket_)
            return 0;

        socket_base_t *socket = socket_;
        const int socket_id = socket->socket_id ();
        {
            scoped_lock_t lock (_sync);
            _owned_sockets.erase (socket_id);
            _closing_sockets[socket_id] = socket;
        }
        return socket_close_ops_t::request_close (socket_);
    }

    int close_socket_and_wait (ctx_t *ctx_,
                               socket_base_t *&socket_,
                               int timeout_ms_)
    {
        if (!socket_)
            return 0;

        socket_base_t *socket = socket_;
        const int socket_id = socket->socket_id ();
        {
            scoped_lock_t lock (_sync);
            _owned_sockets.erase (socket_id);
            _closing_sockets[socket_id] = socket;
        }
        const int rc =
          socket_close_ops_t::request_close_and_wait (ctx_, socket_, timeout_ms_);
        if (rc == 0)
            erase_closing_socket (socket_id);
        return rc;
    }

    int wait_drained (ctx_t *ctx_, int timeout_ms_)
    {
        if (!ctx_)
            return 0;

        const uint64_t deadline_ms =
          timeout_ms_ >= 0 ? zlink::clock_t ().now_ms () + timeout_ms_ : 0;

        while (true) {
            std::map<int, const socket_base_t *> sockets;
            size_t owned_count = 0;
            {
                scoped_lock_t lock (_sync);
                owned_count = _owned_sockets.size ();
                sockets = _closing_sockets;
            }

            if (owned_count == 0 && sockets.empty ())
                return 0;
            if (timeout_ms_ >= 0) {
                const uint64_t now_ms = zlink::clock_t ().now_ms ();
                if (now_ms >= deadline_ms) {
                    debug_dump ("[service-drain] timeout");
                    errno = ETIMEDOUT;
                    return -1;
                }
            }

            for (std::map<int, const socket_base_t *>::const_iterator it =
                   sockets.begin ();
                 it != sockets.end (); ++it) {
                const uint64_t now_ms = zlink::clock_t ().now_ms ();
                const int remaining_ms =
                  timeout_ms_ < 0 ? -1
                                  : static_cast<int> (deadline_ms - now_ms);
                if (socket_close_ops_t::wait_until_closed (
                      ctx_, it->second, remaining_ms)
                    != 0)
                    return -1;
                erase_closing_socket (it->first);
            }
        }
    }

    int force_wait_remaining (ctx_t *ctx_, int timeout_ms_)
    {
        if (!ctx_)
            return 0;

        const uint64_t deadline_ms =
          timeout_ms_ >= 0 ? zlink::clock_t ().now_ms () + timeout_ms_ : 0;

        while (true) {
            std::map<int, const socket_base_t *> owned;
            std::map<int, const socket_base_t *> closing;
            {
                scoped_lock_t lock (_sync);
                owned = _owned_sockets;
                for (std::map<int, const socket_base_t *>::const_iterator it =
                       owned.begin ();
                     it != owned.end (); ++it) {
                    _closing_sockets[it->first] = it->second;
                }
                _owned_sockets.clear ();
                closing = _closing_sockets;
            }

            if (owned.empty () && closing.empty ())
                return 0;
            if (timeout_ms_ >= 0) {
                const uint64_t now_ms = zlink::clock_t ().now_ms ();
                if (now_ms >= deadline_ms) {
                    debug_dump ("[service-force-drain] timeout");
                    errno = ETIMEDOUT;
                    return -1;
                }
            }

            for (std::map<int, const socket_base_t *>::const_iterator it =
                   owned.begin ();
                 it != owned.end (); ++it) {
                socket_base_t *socket =
                  const_cast<socket_base_t *> (it->second);
                if (!socket)
                    continue;
                socket_close_ops_t::request_close (socket);
            }

            for (std::map<int, const socket_base_t *>::const_iterator it =
                   closing.begin ();
                 it != closing.end (); ++it) {
                const uint64_t now_ms = zlink::clock_t ().now_ms ();
                const int remaining_ms =
                  timeout_ms_ < 0 ? -1
                                  : static_cast<int> (deadline_ms - now_ms);
                if (socket_close_ops_t::wait_until_closed (
                      ctx_, it->second, remaining_ms)
                    != 0)
                    return -1;
                erase_closing_socket (it->first);
            }
        }
    }

    size_t socket_count () const
    {
        scoped_lock_t lock (const_cast<mutex_t &> (_sync));
        return _owned_sockets.size () + _closing_sockets.size ();
    }

    void clear ()
    {
        scoped_lock_t lock (_sync);
        _owned_sockets.clear ();
        _closing_sockets.clear ();
    }

  private:
    void erase_closing_socket (int socket_id_)
    {
        scoped_lock_t lock (_sync);
        _closing_sockets.erase (socket_id_);
    }

    void debug_dump (const char *prefix_) const
    {
        if (!getenv ("ZLINK_DEBUG_SERVICE_RUNTIME_DRAIN"))
            return;

        scoped_lock_t lock (const_cast<mutex_t &> (_sync));
        std::fprintf (stderr, "%s owned=", prefix_);
        for (std::map<int, const socket_base_t *>::const_iterator it =
               _owned_sockets.begin ();
             it != _owned_sockets.end (); ++it)
            std::fprintf (stderr, "%d,", it->first);
        std::fprintf (stderr, " closing=");
        for (std::map<int, const socket_base_t *>::const_iterator it =
               _closing_sockets.begin ();
             it != _closing_sockets.end (); ++it)
            std::fprintf (stderr, "%d,", it->first);
        std::fprintf (stderr, "\n");
        std::fflush (stderr);
    }

    mutable mutex_t _sync;
    std::map<int, const socket_base_t *> _owned_sockets;
    std::map<int, const socket_base_t *> _closing_sockets;

    ZLINK_NON_COPYABLE_NOR_MOVABLE (service_socket_registry_t)
};
}

#endif
