/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "core/session_base.hpp"
#include "sockets/socket_base.hpp"
#include "utils/err.hpp"
#include "utils/debug_log.hpp"

namespace
{
const bool spot_direct_route_trace_on =
  zlink::debug_env_enabled ("ZLINK_DEBUG_SPOT_DIRECT_ROUTE");
}

int zlink::session_base_t::pull_msg (msg_t *msg_)
{
    if (!_pipe || !_pipe->read (msg_)) {
        errno = EAGAIN;
        return -1;
    }

    _incomplete_in = (msg_->flags () & msg_t::more) != 0;

    return 0;
}

int zlink::session_base_t::push_msg (msg_t *msg_)
{
    const bool trace_direct_route =
      spot_direct_route_trace_on && _socket != NULL;

    if ((msg_->flags () & msg_t::command) && !msg_->is_subscribe ()
        && !msg_->is_cancel ()) {
        if (trace_direct_route && _socket->socket_type () == ZLINK_CORE_SOCKET_ROUTER) {
            static std::atomic<int> g_router_command_logs (0);
            if (g_router_command_logs.fetch_add (1, std::memory_order_acq_rel)
                < 64) {
                std::fprintf (
                  stderr,
                  "[spot-direct] session push command socket=%d size=%zu flags=%u\n",
                  _socket->socket_id (),
                  msg_->size (),
                  static_cast<unsigned> (msg_->flags ()));
            }
        }
        if (_socket) {
            const int control_rc = _socket->peer_command_from_io (msg_, _pipe);
            if (control_rc < 0)
                return -1;
            if (control_rc > 0) {
                const int rc = msg_->close ();
                errno_assert (rc == 0);
                return msg_->init ();
            }
        }
        return 0;
    }

    if (trace_direct_route && _socket->socket_type () == ZLINK_CORE_SOCKET_ROUTER) {
        static std::atomic<int> g_router_payload_logs (0);
        if (g_router_payload_logs.fetch_add (1, std::memory_order_acq_rel)
            < 64) {
            std::fprintf (
              stderr,
              "[spot-direct] session push payload socket=%d size=%zu flags=%u more=%d routing_id=%d\n",
              _socket->socket_id (),
              msg_->size (),
              static_cast<unsigned> (msg_->flags ()),
              (msg_->flags () & msg_t::more) != 0 ? 1 : 0,
              msg_->is_routing_id () ? 1 : 0);
        }
    }

    if (_socket && _socket->socket_msg_dispatch_active ()) {
        const int dispatch_rc = _socket->socket_msg_dispatch_from_io (msg_, _pipe);
        if (dispatch_rc < 0)
            return -1;
        if (dispatch_rc > 0) {
            const int rc = msg_->close ();
            errno_assert (rc == 0);
            return msg_->init ();
        }
    }

    if (options.type == ZLINK_CORE_SOCKET_STREAM && _socket && _pipe
        && _socket->stream_dispatch_active ()) {
        const int dispatch_rc = _socket->stream_dispatch_msg_from_io (msg_, _pipe);
        if (dispatch_rc < 0)
            return -1;
        if (dispatch_rc > 1)
            return 0;
        if (dispatch_rc > 0) {
            const int rc = msg_->close ();
            errno_assert (rc == 0);
            return msg_->init ();
        }
    }

    if (_pipe && _pipe->write (msg_)) {
        if (trace_direct_route && _socket->socket_type () == ZLINK_CORE_SOCKET_ROUTER) {
            static std::atomic<int> g_router_write_logs (0);
            if (g_router_write_logs.fetch_add (1, std::memory_order_acq_rel)
                < 64) {
                std::fprintf (
                  stderr,
                  "[spot-direct] session wrote pipe socket=%d size=%zu flags=%u more=%d routing_id=%d\n",
                  _socket->socket_id (),
                  msg_->size (),
                  static_cast<unsigned> (msg_->flags ()),
                  (msg_->flags () & msg_t::more) != 0 ? 1 : 0,
                  msg_->is_routing_id () ? 1 : 0);
            }
        }
        const int rc = msg_->init ();
        errno_assert (rc == 0);
        return 0;
    }

    if (trace_direct_route && _socket->socket_type () == ZLINK_CORE_SOCKET_ROUTER) {
        static std::atomic<int> g_router_write_fail_logs (0);
        if (g_router_write_fail_logs.fetch_add (1, std::memory_order_acq_rel)
            < 64) {
            std::fprintf (
              stderr,
              "[spot-direct] session pipe write failed socket=%d size=%zu flags=%u\n",
              _socket->socket_id (),
              msg_->size (),
              static_cast<unsigned> (msg_->flags ()));
        }
    }
    errno = EAGAIN;
    return -1;
}

void zlink::session_base_t::reset ()
{
}

void zlink::session_base_t::flush ()
{
    if (_pipe)
        _pipe->flush ();
}

void zlink::session_base_t::rollback ()
{
    if (_pipe)
        _pipe->rollback ();
}

void zlink::session_base_t::clean_pipes ()
{
    zlink_assert (_pipe != NULL);

    _pipe->rollback ();
    _pipe->flush ();

    while (_incomplete_in) {
        msg_t msg;
        int rc = msg.init ();
        errno_assert (rc == 0);
        rc = pull_msg (&msg);
        if (rc != 0) {
            const int saved_errno = errno;
            rc = msg.close ();
            errno_assert (rc == 0);
            if (saved_errno == EAGAIN) {
                _incomplete_in = false;
                break;
            }
            errno_assert (false);
        }
        rc = msg.close ();
        errno_assert (rc == 0);
    }
}
