/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_SERVICES_COMMON_SERVICE_SOCKET_REGISTRY_HPP_INCLUDED__
#define __ZLINK_SERVICES_COMMON_SERVICE_SOCKET_REGISTRY_HPP_INCLUDED__

#include "core/ctx.hpp"
#include "sockets/common/socket_base.hpp"
#include "sockets/common/socket_close_ops.hpp"
#include "utils/debug_log.hpp"
#include "utils/mutex.hpp"

#include <map>
#include <stdio.h>
#include <stdlib.h>

namespace zlink
{
class service_socket_registry_t
{
  public:
    typedef std::map<int, socket_base_t *> socket_map_t;

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

    int close_socket (socket_base_t *&socket_,
                      const socket_base_t **closed_socket_ = NULL)
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
        if (closed_socket_)
            *closed_socket_ = socket;
        return socket_close_ops_t::request_close (socket_);
    }

    void snapshot_drain_state (size_t *owned_count_, socket_map_t *closing_) const
    {
        scoped_lock_t lock (_sync);
        if (owned_count_)
            *owned_count_ = _owned_sockets.size ();
        if (closing_)
            *closing_ = _closing_sockets;
    }

    void handoff_owned_to_closing (socket_map_t *owned_, socket_map_t *closing_)
    {
        scoped_lock_t lock (_sync);
        if (owned_)
            *owned_ = _owned_sockets;
        for (socket_map_t::const_iterator it = _owned_sockets.begin ();
             it != _owned_sockets.end (); ++it)
            _closing_sockets[it->first] = it->second;
        _owned_sockets.clear ();
        if (closing_)
            *closing_ = _closing_sockets;
    }

    size_t socket_count () const
    {
        scoped_lock_t lock (_sync);
        return _owned_sockets.size () + _closing_sockets.size ();
    }

    bool contains_socket (const socket_base_t *socket_) const
    {
        if (!socket_)
            return false;
        scoped_lock_t lock (_sync);
        const int socket_id = socket_->socket_id ();
        return _owned_sockets.count (socket_id) != 0
               || _closing_sockets.count (socket_id) != 0;
    }

    void clear ()
    {
        scoped_lock_t lock (_sync);
        _owned_sockets.clear ();
        _closing_sockets.clear ();
    }

    void erase_closing_socket (int socket_id_)
    {
        scoped_lock_t lock (_sync);
        _closing_sockets.erase (socket_id_);
    }

    void debug_dump (const char *prefix_) const
    {
        if (!debug_env_enabled ("ZLINK_DEBUG_SERVICE_RUNTIME_DRAIN"))
            return;

        scoped_lock_t lock (_sync);
        std::fprintf (stderr, "%s owned=", prefix_);
        for (socket_map_t::const_iterator it =
               _owned_sockets.begin ();
             it != _owned_sockets.end (); ++it)
            std::fprintf (stderr, "%d,", it->first);
        std::fprintf (stderr, " closing=");
        for (socket_map_t::const_iterator it =
               _closing_sockets.begin ();
             it != _closing_sockets.end (); ++it)
            std::fprintf (stderr, "%d,", it->first);
        std::fprintf (stderr, "\n");
        std::fflush (stderr);
    }

    mutable mutex_t _sync;
    socket_map_t _owned_sockets;
    socket_map_t _closing_sockets;

    ZLINK_NON_COPYABLE_NOR_MOVABLE (service_socket_registry_t)
};
}

#endif
