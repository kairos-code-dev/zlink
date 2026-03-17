/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_SERVICES_COMMON_SOCKET_MONITOR_BRIDGE_HPP_INCLUDED__
#define __ZLINK_SERVICES_COMMON_SOCKET_MONITOR_BRIDGE_HPP_INCLUDED__

#include "sockets/socket_base.hpp"
#include "utils/random.hpp"

#include <stdio.h>

namespace zlink
{
static inline void *open_socket_monitor_bridge (socket_base_t *socket_,
                                                int events_)
{
    if (!socket_) {
        errno = EINVAL;
        return NULL;
    }

    char endpoint[128];
    snprintf (endpoint, sizeof endpoint, "inproc://monitor-%p-%u",
              static_cast<void *> (socket_), generate_random ());

    if (socket_->monitor (endpoint, events_, 4, ZLINK_PAIR) != 0)
        return NULL;

    socket_base_t *monitor_socket = socket_->get_ctx ()->create_socket (ZLINK_PAIR);
    if (!monitor_socket) {
        socket_->monitor (NULL, 0, 4, ZLINK_PAIR);
        errno = ENOMEM;
        return NULL;
    }

    if (monitor_socket->connect (endpoint) != 0) {
        monitor_socket->close ();
        socket_->monitor (NULL, 0, 4, ZLINK_PAIR);
        return NULL;
    }

    return static_cast<void *> (monitor_socket);
}

static inline void close_socket_monitor_bridge (socket_base_t *socket_,
                                                socket_base_t *monitor_socket_)
{
    if (socket_)
        (void) socket_->monitor (NULL, 0, 4, ZLINK_PAIR);
    LIBZLINK_UNUSED (monitor_socket_);
}
}

#endif
