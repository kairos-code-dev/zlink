/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "core/recv_internal.hpp"

#include "core/msg.hpp"
#include "sockets/socket_base.hpp"
#include "utils/clock.hpp"
#include "utils/sleep.hpp"

#include <climits>
#include <string.h>

int zlink::recv_msg_socket (socket_base_t *socket_,
                            int type_,
                            zlink_msg_t *msg_,
                            int flags_)
{
    // Hot path: direct recv-mode entry for single-message public recv. Changes
    // here affect PAIR/DEALER small-message throughput even without contention.
    if (!socket_ || !msg_) {
        errno = EFAULT;
        return -1;
    }

    switch (type_) {
        case ZLINK_CORE_SOCKET_SUB:
        case ZLINK_CORE_SOCKET_XSUB:
            if (socket_->sub_dispatch_active ()) {
                errno = EBUSY;
                return -1;
            }
            break;

        case ZLINK_CORE_SOCKET_XPUB:
            if (socket_->xpub_dispatch_active ()) {
                errno = EBUSY;
                return -1;
            }
            break;

        case ZLINK_CORE_SOCKET_STREAM:
            if (socket_->stream_dispatch_active ()) {
                errno = EBUSY;
                return -1;
            }
            break;

        default:
            if (socket_->socket_msg_dispatch_active ()) {
                errno = EBUSY;
                return -1;
            }
            break;
    }

    const int rc = socket_->recv (reinterpret_cast<msg_t *> (msg_), flags_);
    if (rc < 0)
        return -1;

    const size_t size = zlink_msg_size (msg_);
    return static_cast<int> (size < static_cast<size_t> (INT_MAX) ? size
                                                                   : INT_MAX);
}

int zlink::recv_msg_internal (void *socket_, zlink_msg_t *msg_, int flags_)
{
    socket_base_t *socket = static_cast<socket_base_t *> (socket_);
    if (!socket || !socket->check_tag ()) {
        errno = EFAULT;
        return -1;
    }

    return recv_msg_socket (socket, socket->socket_type (), msg_, flags_);
}

int zlink::recv_msg_routed_socket (socket_base_t *socket_,
                                   zlink_msg_t *msg_,
                                   zlink_routing_id_t *source_rid_out_,
                                   int flags_)
{
    if (!socket_ || !msg_) {
        errno = EFAULT;
        return -1;
    }

    if (socket_->socket_msg_dispatch_active ()) {
        errno = EBUSY;
        return -1;
    }

    const int rc =
      socket_->recv_routed (reinterpret_cast<msg_t *> (msg_), source_rid_out_,
                            flags_);
    if (rc < 0)
        return -1;

    const size_t size = zlink_msg_size (msg_);
    return static_cast<int> (size < static_cast<size_t> (INT_MAX) ? size
                                                                   : INT_MAX);
}

int zlink::recv_msg_routed_internal (void *socket_,
                                     zlink_msg_t *msg_,
                                     zlink_routing_id_t *source_rid_out_,
                                     int flags_)
{
    socket_base_t *socket = static_cast<socket_base_t *> (socket_);
    if (!socket || !socket->check_tag ()) {
        errno = EFAULT;
        return -1;
    }

    const int type = socket->socket_type ();
    if (type != ZLINK_CORE_SOCKET_ROUTER) {
        errno = ENOTSUP;
        return -1;
    }

    return recv_msg_routed_socket (socket, msg_, source_rid_out_, flags_);
}

int zlink::recv_followup_msg_socket (socket_base_t *socket_, zlink_msg_t *msg_)
{
    if (!socket_ || !msg_) {
        errno = EFAULT;
        return -1;
    }

    const int rc = socket_->recv (reinterpret_cast<msg_t *> (msg_),
                                  ZLINK_DONTWAIT);
    if (rc >= 0) {
        const size_t size = zlink_msg_size (msg_);
        return static_cast<int> (
          size < static_cast<size_t> (INT_MAX) ? size : INT_MAX);
    }

    if (errno == EAGAIN || errno == EINTR)
        errno = EPROTO;
    return -1;
}

int zlink::recv_followup_msg_internal (void *socket_, zlink_msg_t *msg_)
{
    socket_base_t *socket = static_cast<socket_base_t *> (socket_);
    if (!socket || !socket->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return recv_followup_msg_socket (socket, msg_);
}

int zlink::recv_buffer_internal (void *socket_,
                                 void *buf_,
                                 size_t len_,
                                 int flags_)
{
    zlink_msg_t msg;
    if (zlink_msg_init (&msg) != 0) {
        errno = EFAULT;
        return -1;
    }

    const int nbytes = recv_msg_internal (socket_, &msg, flags_);
    if (nbytes < 0) {
        const int err = errno;
        zlink_msg_close (&msg);
        errno = err;
        return -1;
    }

    const size_t to_copy =
      static_cast<size_t> (nbytes) < len_ ? static_cast<size_t> (nbytes) : len_;
    if (to_copy > 0 && buf_)
        memcpy (buf_, zlink_msg_data (&msg), to_copy);

    zlink_msg_close (&msg);
    return nbytes;
}

int zlink::wait_socket_events_internal (void *socket_,
                                        short events_,
                                        long timeout_ms_)
{
    socket_base_t *socket = static_cast<socket_base_t *> (socket_);
    if (!socket || !socket->check_tag ()) {
        errno = EFAULT;
        return -1;
    }

    zlink::clock_t clock;
    const uint64_t deadline =
      timeout_ms_ >= 0 ? clock.now_ms () + static_cast<uint64_t> (timeout_ms_) : 0;

    while (true) {
        uint32_t ready = 0;
        const short non_out_events = events_ & ~ZLINK_POLLOUT;
        if (non_out_events != 0) {
            if (socket->get_events_internal (non_out_events, &ready) != 0)
                return -1;
        }
        if ((events_ & ZLINK_POLLOUT) != 0 && socket->transport_has_out ())
            ready |= ZLINK_POLLOUT;
        if ((ready & events_) == static_cast<uint32_t> (events_))
            return 1;

        if (timeout_ms_ == 0)
            return 0;
        if (timeout_ms_ > 0 && clock.now_ms () >= deadline)
            return 0;

        zlink::sleep_ms (1);
    }
}
