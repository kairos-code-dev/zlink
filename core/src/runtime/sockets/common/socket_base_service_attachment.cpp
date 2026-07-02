/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "sockets/common/socket_base.hpp"
#include "utils/likely.hpp"

int zlink::socket_base_t::set_peer_weight (uint32_t weight_)
{
    if (weight_ > 100) {
        errno = EINVAL;
        return -1;
    }

    socket_lifecycle_coordinator_t &lifecycle = lifecycle_coordinator ();
    socket_public_api_scope_t admission (lifecycle);
    if (!admission.acquired ())
        return -1;

    if (unlikely (_ctx_terminated)) {
        errno = ETERM;
        return -1;
    }

    {
        socket_public_api_lock_scope_t guard (lifecycle);
        if (_local_peer_weight == weight_)
            return 0;
        _local_peer_weight = weight_;
        options.peer_weight = static_cast<int> (weight_);
        xlocal_peer_weight_changed ();
    }
    return 0;
}

int zlink::socket_base_t::get_peer_weight (uint32_t *weight_out_) const
{
    if (!weight_out_) {
        errno = EFAULT;
        return -1;
    }

    socket_lifecycle_coordinator_t &lifecycle = lifecycle_coordinator ();
    socket_public_api_scope_t admission (lifecycle);
    if (!admission.acquired ())
        return -1;

    if (unlikely (_ctx_terminated)) {
        errno = ETERM;
        return -1;
    }

    socket_public_api_lock_scope_t guard (lifecycle);
    *weight_out_ = _local_peer_weight;
    return 0;
}

uint32_t zlink::socket_base_t::local_peer_weight () const
{
    return _local_peer_weight;
}

int zlink::socket_base_t::close ()
{
    return close (2000);
}

int zlink::socket_base_t::close (int handoff_timeout_ms_)
{
    const bool from_self_callback = socket_send_ready_dispatch_scope_t::dispatching_socket (this);
    if (!lifecycle_coordinator ().begin_close_or_fail_busy (from_self_callback))
        return -1;
    if (from_self_callback)
        return 0;

    finish_close_handoff (handoff_timeout_ms_);
    return 0;
}
