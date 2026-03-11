/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "core/recv_internal.hpp"

#include "core/msg.hpp"
#include "sockets/socket_base.hpp"
#include "utils/clock.hpp"

#include <climits>
#include <string.h>
#include <unistd.h>

int zlink::recv_msg_internal (void *socket_, zlink_msg_t *msg_, int flags_)
{
    socket_base_t *socket = static_cast<socket_base_t *> (socket_);
    if (!socket || !socket->check_tag ()) {
        errno = EFAULT;
        return -1;
    }

    if (socket->socket_msg_dispatch_active ()) {
        errno = EBUSY;
        return -1;
    }

    int type = -1;
    size_t type_len = sizeof (type);
    if (socket->getsockopt (ZLINK_SOCKOPT_TYPE, &type, &type_len) != 0)
        return -1;

    if (type == ZLINK_STREAM && socket->stream_dispatch_active ()) {
        errno = EBUSY;
        return -1;
    }

    const int rc = socket->recv (reinterpret_cast<msg_t *> (msg_), flags_);
    if (rc < 0)
        return -1;

    const size_t size = zlink_msg_size (msg_);
    return static_cast<int> (size < static_cast<size_t> (INT_MAX) ? size
                                                                   : INT_MAX);
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
        if (socket->get_events_internal (events_, &ready) != 0)
            return -1;
        if ((ready & events_) == static_cast<uint32_t> (events_))
            return 1;

        if (timeout_ms_ == 0)
            return 0;
        if (timeout_ms_ > 0 && clock.now_ms () >= deadline)
            return 0;

        usleep (1000);
    }
}
