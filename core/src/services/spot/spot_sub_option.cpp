/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "services/spot/spot_sub.hpp"

#include "sockets/socket_base.hpp"

#include <string.h>

namespace zlink
{
int spot_sub_t::set_option (int option_,
                            const void *optval_,
                            size_t optvallen_)
{
    socket_base_t *socket = this->socket ();
    if (!socket) {
        errno = EFAULT;
        return -1;
    }
    if (!optval_ || optvallen_ == 0) {
        errno = EINVAL;
        return -1;
    }

    int socket_option = -1;
    switch (option_) {
        case ZLINK_SPOT_SUB_OPT_RCVHWM:
            socket_option = ZLINK_INTERNAL_OPT_RCVHWM;
            break;
        case ZLINK_SPOT_SUB_OPT_LINGER:
            socket_option = ZLINK_INTERNAL_OPT_LINGER;
            break;
        case ZLINK_SPOT_SUB_OPT_SNDBUF:
            socket_option = ZLINK_INTERNAL_OPT_SNDBUF;
            break;
        case ZLINK_SPOT_SUB_OPT_RCVBUF:
            socket_option = ZLINK_INTERNAL_OPT_RCVBUF;
            break;
        case ZLINK_SPOT_SUB_OPT_RCVTIMEO:
            socket_option = ZLINK_INTERNAL_OPT_RCVTIMEO;
            break;
        default:
            errno = EINVAL;
            return -1;
    }

    scoped_lock_t lock (_sync);
    return socket->setsockopt (socket_option, optval_, optvallen_);
}

int spot_sub_t::set_routing_id (const void *data_, size_t size_)
{
    if (!data_ || size_ == 0 || size_ > sizeof (_routing_id.data)) {
        errno = EINVAL;
        return -1;
    }

    scoped_lock_t lock (_sync);
    if (_routing_id_locked) {
        errno = EFSM;
        return -1;
    }
    _routing_id.size = static_cast<uint8_t> (size_);
    memcpy (_routing_id.data, data_, size_);
    return 0;
}

int spot_sub_t::routing_id (zlink_routing_id_t *out_) const
{
    if (!out_) {
        errno = EINVAL;
        return -1;
    }
    scoped_lock_t lock (const_cast<mutex_t &> (_sync));
    *out_ = _routing_id;
    return 0;
}

int spot_sub_t::fill_monitor_snapshot (zlink_monitor_snapshot_t *out_) const
{
    if (!out_) {
        errno = EINVAL;
        return -1;
    }
    socket_base_t *socket = this->socket ();
    if (!socket) {
        errno = EFAULT;
        return -1;
    }
    if (socket->monitor_snapshot (out_) != 0)
        return -1;
    out_->source_kind = ZLINK_MONITOR_SOURCE_SPOT_SUB;
    out_->state_flags &= ~ZLINK_MONITOR_STATE_READY;
    return 0;
}

void *spot_sub_t::monitor_open (int events_)
{
    lock_routing_id ();
    if (ensure_monitor_bridge_started () != 0)
        return NULL;
    return _monitor.open (events_);
}
}
