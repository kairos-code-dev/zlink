/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"
#include "utils/macros.hpp"
#include "sockets/dealer.hpp"
#include "utils/err.hpp"
#include "core/msg.hpp"

namespace
{
const char peer_admission_cmd_name[] = "ADMISSION";
const size_t peer_admission_cmd_name_size = sizeof (peer_admission_cmd_name) - 1;

bool decode_peer_admission_command (const zlink::msg_t &msg_,
                                    zlink_admission_state_t *state_out_)
{
    if (!(msg_.flags () & zlink::msg_t::command))
        return false;
    if (msg_.size () != peer_admission_cmd_name_size + 1)
        return false;
    if (memcmp (const_cast<zlink::msg_t &> (msg_).data (),
                peer_admission_cmd_name, peer_admission_cmd_name_size)
        != 0) {
        return false;
    }

    const unsigned char state =
      static_cast<unsigned char *> (const_cast<zlink::msg_t &> (msg_).data ())[
        peer_admission_cmd_name_size];
    if (state != ZLINK_ADMISSION_SERVING && state != ZLINK_ADMISSION_DRAINING)
        return false;

    if (state_out_)
        *state_out_ = static_cast<zlink_admission_state_t> (state);
    return true;
}

void store_dispatch_part (std::vector<zlink_msg_t> *parts_, zlink::msg_t *msg_)
{
    zlink_msg_t stored;
    memset (&stored, 0, sizeof (stored));
    zlink::msg_t *stored_msg = reinterpret_cast<zlink::msg_t *> (&stored);
    const int init_rc = stored_msg->init ();
    errno_assert (init_rc == 0);
    const int move_rc = stored_msg->move (*msg_);
    errno_assert (move_rc == 0);
    parts_->push_back (stored);
}

}

zlink::dealer_t::dealer_t (class ctx_t *parent_, uint32_t tid_, int sid_) :
    socket_base_t (parent_, tid_, sid_), _probe_router (false)
{
    options.type = ZLINK_CORE_SOCKET_DEALER;
    options.can_send_hello_msg = true;
    options.can_recv_hiccup_msg = true;
}

zlink::dealer_t::~dealer_t ()
{
    close_socket_msg_parts (&_dispatch_parts);
}

void zlink::dealer_t::xattach_pipe (pipe_t *pipe_,
                                  bool subscribe_to_all_,
                                  bool locally_initiated_)
{
    LIBZLINK_UNUSED (subscribe_to_all_);
    LIBZLINK_UNUSED (locally_initiated_);

    zlink_assert (pipe_);

    if (_probe_router) {
        msg_t probe_msg;
        int rc = probe_msg.init ();
        errno_assert (rc == 0);

        rc = pipe_->write_and_flush (&probe_msg);
        // zlink_assert (rc) is not applicable here, since it is not a bug.
        LIBZLINK_UNUSED (rc);

        rc = probe_msg.close ();
        errno_assert (rc == 0);
    }

    _fq.attach (pipe_);
    if (socket_msg_dispatch_active ()) {
        pipe_->check_read ();
        _fq.deactivate (pipe_);
    }
    _lb.attach (pipe_);
}

int zlink::dealer_t::xsetsockopt (int option_,
                                const void *optval_,
                                size_t optvallen_)
{
    const bool is_int = (optvallen_ == sizeof (int));
    int value = 0;
    if (is_int)
        memcpy (&value, optval_, sizeof (int));

    switch (option_) {
        case ZLINK_INTERNAL_OPT_PROBE_ROUTER:
            if (is_int && value >= 0) {
                _probe_router = (value != 0);
                return 0;
            }
            break;

        default:
            break;
    }

    errno = EINVAL;
    return -1;
}

int zlink::dealer_t::xsend (msg_t *msg_)
{
    return sendpipe (msg_, NULL);
}

int zlink::dealer_t::xrecv (msg_t *msg_)
{
    pipe_t *pipe = NULL;
    return recvpipe (msg_, &pipe);
}

bool zlink::dealer_t::xhas_in ()
{
    return _fq.has_in ();
}

bool zlink::dealer_t::xhas_out ()
{
    return _lb.has_out ();
}

void zlink::dealer_t::xread_activated (pipe_t *pipe_)
{
    _fq.activated (pipe_);
    if (!socket_msg_dispatch_active ())
        return;

    msg_t msg;
    const int init_rc = msg.init ();
    errno_assert (init_rc == 0);

    pipe_t *dispatch_pipe = NULL;
    while (recvpipe (&msg, &dispatch_pipe) == 0) {
        const int dispatch_rc = xsocket_msg_dispatch (&msg, dispatch_pipe);
        if (dispatch_rc <= 0)
            break;
    }

    const int close_rc = msg.close ();
    errno_assert (close_rc == 0);
}

void zlink::dealer_t::xwrite_activated (pipe_t *pipe_)
{
    _lb.activated (pipe_);
}

void zlink::dealer_t::xpipe_terminated (pipe_t *pipe_)
{
    _fq.pipe_terminated (pipe_);
    _lb.pipe_terminated (pipe_);
}

int zlink::dealer_t::sendpipe (msg_t *msg_, pipe_t **pipe_)
{
    return _lb.sendpipe (msg_, pipe_);
}

int zlink::dealer_t::recvpipe (msg_t *msg_, pipe_t **pipe_)
{
    return _fq.recvpipe (msg_, pipe_);
}

int zlink::dealer_t::xsocket_msg_dispatch (msg_t *msg_, pipe_t *pipe_)
{
    if (!socket_msg_dispatch_active ())
        return 0;

    store_dispatch_part (&_dispatch_parts, msg_);
    if ((reinterpret_cast<msg_t *> (&_dispatch_parts.back ())->flags ()
         & msg_t::more)
        != 0) {
        return 1;
    }

    zlink_socket_msg_handler_fn handler = socket_msg_handler ();
    if (!handler) {
        close_socket_msg_parts (&_dispatch_parts);
        return 1;
    }

    zlink_routing_id_t source_rid;
    resolve_socket_msg_source_rid (pipe_, &source_rid);
    invoke_socket_msg_handler (handler, &source_rid, &_dispatch_parts[0],
                               _dispatch_parts.size ());
    _dispatch_parts.clear ();
    return 1;
}

int zlink::dealer_t::xpeer_command (msg_t *msg_, pipe_t *pipe_)
{
    zlink_admission_state_t state = ZLINK_ADMISSION_SERVING;
    if (!decode_peer_admission_command (*msg_, &state))
        return 0;
    return apply_peer_admission_state (pipe_, state);
}

void zlink::dealer_t::xlocal_admission_state_changed ()
{
}

void zlink::dealer_t::xarm_socket_msg_dispatch ()
{
    _fq.arm_dispatch ();
}

void zlink::dealer_t::xdispatch_io ()
{
    if (!socket_msg_dispatch_active ())
        return;

    msg_t msg;
    const int init_rc = msg.init ();
    errno_assert (init_rc == 0);

    pipe_t *dispatch_pipe = NULL;
    while (recvpipe (&msg, &dispatch_pipe) == 0) {
        const int dispatch_rc = xsocket_msg_dispatch (&msg, dispatch_pipe);
        if (dispatch_rc <= 0)
            break;
    }

    const int close_rc = msg.close ();
    errno_assert (close_rc == 0);
}

int zlink::dealer_t::apply_peer_admission_state (pipe_t *pipe_,
                                                 zlink_admission_state_t state_)
{
    if (!pipe_)
        return 1;

    const bool admitted = state_ == ZLINK_ADMISSION_SERVING;
    const bool changed = _lb.admitted (pipe_) != admitted;
    _lb.set_admitted (pipe_, admitted);
    if (!changed)
        return 1;
    emit_peer_admission_changed (pipe_, state_);
    return 1;
}
