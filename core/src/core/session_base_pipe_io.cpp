/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "core/session_base.hpp"
#include "utils/err.hpp"

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
    if ((msg_->flags () & msg_t::command) && !msg_->is_subscribe ()
        && !msg_->is_cancel ()) {
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

    if (_socket) {
        const int dispatch_rc = _socket->socket_msg_dispatch_from_io (msg_, _pipe);
        if (dispatch_rc < 0)
            return -1;
        if (dispatch_rc > 0) {
            const int rc = msg_->close ();
            errno_assert (rc == 0);
            return msg_->init ();
        }
    }

    if (options.type == ZLINK_CORE_SOCKET_STREAM && _socket && _pipe) {
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
        const int rc = msg_->init ();
        errno_assert (rc == 0);
        return 0;
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
        errno_assert (rc == 0);
        rc = msg.close ();
        errno_assert (rc == 0);
    }
}

