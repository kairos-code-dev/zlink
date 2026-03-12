/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_SERVICES_COMMON_SERVICE_RUNTIME_BASE_HPP_INCLUDED__
#define __ZLINK_SERVICES_COMMON_SERVICE_RUNTIME_BASE_HPP_INCLUDED__

#include "core/ctx.hpp"
#include "sockets/socket_base.hpp"
#include "utils/clock.hpp"
#include "utils/mutex.hpp"

#include <map>
#include <vector>

#ifndef ZLINK_HAVE_WINDOWS
#include <unistd.h>
#endif

namespace zlink
{
enum service_lifecycle_state_t
{
    service_state_idle = 0,
    service_state_starting = 1,
    service_state_running = 2,
    service_state_stopping = 3,
    service_state_stopped = 4,
    service_state_faulted = 5
};

class service_runtime_base_t
{
  public:
    explicit service_runtime_base_t (ctx_t *ctx_ = NULL) :
        _ctx (ctx_),
        _state (service_state_idle),
        _fault_errno (0)
    {
    }

    void set_ctx (ctx_t *ctx_)
    {
        scoped_lock_t lock (_sync);
        _ctx = ctx_;
    }

    bool transition_to (service_lifecycle_state_t target_)
    {
        scoped_lock_t lock (_sync);
        bool valid = false;
        switch (target_) {
            case service_state_starting:
                valid = (_state == service_state_idle);
                break;
            case service_state_running:
                valid = (_state == service_state_starting);
                break;
            case service_state_stopping:
                valid = (_state == service_state_running);
                break;
            case service_state_stopped:
                valid = (_state == service_state_stopping);
                break;
            case service_state_faulted:
                valid = (_state == service_state_starting
                         || _state == service_state_running
                         || _state == service_state_stopping);
                break;
            default:
                break;
        }
        if (!valid) {
            errno = EFSM;
            return false;
        }
        _state = target_;
        return true;
    }

    service_lifecycle_state_t state () const
    {
        scoped_lock_t lock (const_cast<mutex_t &> (_sync));
        return _state;
    }

    bool is_running () const
    {
        return state () == service_state_running;
    }

    bool is_stopping () const
    {
        return state () == service_state_stopping;
    }

    bool is_stopped () const
    {
        return state () == service_state_stopped;
    }

    void mark_faulted (int err_)
    {
        scoped_lock_t lock (_sync);
        _state = service_state_faulted;
        _fault_errno = err_;
    }

    int fault_errno () const
    {
        scoped_lock_t lock (const_cast<mutex_t &> (_sync));
        return _fault_errno;
    }

    void register_socket (socket_base_t *socket_)
    {
        if (!socket_)
            return;
        scoped_lock_t lock (_sync);
        if (_state >= service_state_stopping)
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

    int close_socket (socket_base_t *&socket_, int timeout_ms_ = 10000)
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
        socket->stop ();
        socket->close ();
        socket_ = NULL;
        LIBZLINK_UNUSED (timeout_ms_);
        return 0;
    }

    int close_socket_and_wait (socket_base_t *&socket_, int timeout_ms_ = 10000)
    {
        if (!_ctx)
            return close_socket (socket_, timeout_ms_);
        if (!socket_)
            return 0;

        socket_base_t *socket = socket_;
        const int socket_id = socket->socket_id ();
        {
            scoped_lock_t lock (_sync);
            _owned_sockets.erase (socket_id);
            _closing_sockets[socket_id] = socket;
        }
        socket_ = NULL;
        const int rc = _ctx->close_socket_and_wait (socket, timeout_ms_);
        if (rc == 0)
            erase_closing_socket (socket_id);
        return rc;
    }

    int wait_drained (int timeout_ms_)
    {
        if (!_ctx)
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

            int remaining_ms = -1;
            if (timeout_ms_ >= 0) {
                const uint64_t now_ms = zlink::clock_t ().now_ms ();
                if (now_ms >= deadline_ms) {
                    errno = ETIMEDOUT;
                    return -1;
                }
                remaining_ms = static_cast<int> (deadline_ms - now_ms);
            }

            if (sockets.empty ()) {
#ifdef ZLINK_HAVE_WINDOWS
                Sleep (1);
#else
                usleep (1000);
#endif
                continue;
            }

            for (std::map<int, const socket_base_t *>::const_iterator it =
                   sockets.begin ();
                 it != sockets.end (); ++it) {
                int socket_timeout_ms = remaining_ms;
                if (_ctx->wait_for_socket_removal (it->second, socket_timeout_ms)
                    != 0)
                    return -1;
                erase_closing_socket (it->first);
                if (timeout_ms_ >= 0) {
                    const uint64_t now_ms = zlink::clock_t ().now_ms ();
                    if (now_ms >= deadline_ms) {
                        errno = ETIMEDOUT;
                        return -1;
                    }
                    remaining_ms = static_cast<int> (deadline_ms - now_ms);
                }
            }
        }
    }

    int force_wait_remaining (int timeout_ms_)
    {
        if (!_ctx)
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

            int remaining_ms = -1;
            if (timeout_ms_ >= 0) {
                const uint64_t now_ms = zlink::clock_t ().now_ms ();
                if (now_ms >= deadline_ms) {
                    errno = ETIMEDOUT;
                    return -1;
                }
                remaining_ms = static_cast<int> (deadline_ms - now_ms);
            }

            for (std::map<int, const socket_base_t *>::const_iterator it =
                   owned.begin ();
                 it != owned.end (); ++it) {
                socket_base_t *socket =
                  const_cast<socket_base_t *> (it->second);
                if (!socket)
                    continue;
                socket->stop ();
                socket->close ();
                if (_ctx->wait_for_socket_removal (socket, remaining_ms) != 0)
                    return -1;
                erase_closing_socket (it->first);
                if (timeout_ms_ >= 0) {
                    const uint64_t now_ms = zlink::clock_t ().now_ms ();
                    if (now_ms >= deadline_ms) {
                        errno = ETIMEDOUT;
                        return -1;
                    }
                    remaining_ms = static_cast<int> (deadline_ms - now_ms);
                }
            }

            for (std::map<int, const socket_base_t *>::const_iterator it =
                   closing.begin ();
                 it != closing.end (); ++it) {
                if (owned.find (it->first) != owned.end ())
                    continue;
                if (_ctx->wait_for_socket_removal (it->second, remaining_ms)
                    != 0)
                    return -1;
                erase_closing_socket (it->first);
                if (timeout_ms_ >= 0) {
                    const uint64_t now_ms = zlink::clock_t ().now_ms ();
                    if (now_ms >= deadline_ms) {
                        errno = ETIMEDOUT;
                        return -1;
                    }
                    remaining_ms = static_cast<int> (deadline_ms - now_ms);
                }
            }
        }
    }

    size_t owned_socket_count () const
    {
        scoped_lock_t lock (const_cast<mutex_t &> (_sync));
        return _owned_sockets.size () + _closing_sockets.size ();
    }

  private:
    void erase_closing_socket (int socket_id_)
    {
        scoped_lock_t lock (_sync);
        _closing_sockets.erase (socket_id_);
    }

    ctx_t *_ctx;
    service_lifecycle_state_t _state;
    int _fault_errno;
    mutable mutex_t _sync;
    std::map<int, const socket_base_t *> _owned_sockets;
    std::map<int, const socket_base_t *> _closing_sockets;

    ZLINK_NON_COPYABLE_NOR_MOVABLE (service_runtime_base_t)
};
}

#endif
