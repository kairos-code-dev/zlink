/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "sockets/socket_base.hpp"
#include "utils/err.hpp"
#include "utils/likely.hpp"

int zlink::socket_base_t::send (msg_t *msg_, int flags_)
{
    if (!enter_public_api ())
        return -1;

    if (unlikely (_ctx_terminated)) {
        leave_public_api ();
        errno = ETERM;
        return -1;
    }

    if (unlikely (!msg_ || !msg_->check ())) {
        leave_public_api ();
        errno = EFAULT;
        return -1;
    }

    int rc = process_commands (0, true);
    if (unlikely (rc != 0)) {
        leave_public_api ();
        return -1;
    }

    msg_->reset_flags (msg_t::more);
    if (flags_ & ZLINK_SNDMORE)
        msg_->set_flags (msg_t::more);
    msg_->reset_metadata ();

    {
        lock_public_api_sync ();
        rc = xsend (msg_);
        unlock_public_api_sync ();
    }
    if (rc == 0) {
        leave_public_api ();
        return 0;
    }
    if (unlikely (rc == -2)) {
        if (!((flags_ & ZLINK_DONTWAIT) || options.sndtimeo == 0)) {
            rc = msg_->close ();
            errno_assert (rc == 0);
            rc = msg_->init ();
            errno_assert (rc == 0);
            leave_public_api ();
            return 0;
        }
    }
    if (unlikely (errno != EAGAIN)) {
        if ((flags_ & ZLINK_DONTWAIT) || options.sndtimeo == 0) {
            if (errno == ENOTCONN || errno == EHOSTUNREACH
                || errno == ETIMEDOUT) {
                arm_send_ready_notification ();
            }
        }
        leave_public_api ();
        return -1;
    }

    if ((flags_ & ZLINK_DONTWAIT) || options.sndtimeo == 0) {
        arm_send_ready_notification ();
        leave_public_api ();
        return -1;
    }

    int timeout = options.sndtimeo;
    const uint64_t end = timeout < 0 ? 0 : (_clock.now_ms () + timeout);

    while (true) {
        rc = process_commands (timeout, false);
        if (unlikely (rc != 0)) {
            leave_public_api ();
            return -1;
        }
        {
            lock_public_api_sync ();
            rc = xsend (msg_);
            unlock_public_api_sync ();
        }
        if (rc == 0)
            break;
        if (unlikely (errno != EAGAIN)) {
            leave_public_api ();
            return -1;
        }
        if (timeout > 0) {
            timeout = static_cast<int> (end - _clock.now_ms ());
            if (timeout <= 0) {
                errno = EAGAIN;
                leave_public_api ();
                return -1;
            }
        }
    }

    leave_public_api ();
    return 0;
}

int zlink::socket_base_t::send_routed (const zlink_routing_id_t *target_rid_,
                                       msg_t *msg_,
                                       int flags_)
{
    if (!enter_public_api ())
        return -1;

    if (unlikely (_ctx_terminated)) {
        leave_public_api ();
        errno = ETERM;
        return -1;
    }

    if (unlikely (!target_rid_ || !msg_ || !msg_->check ())) {
        leave_public_api ();
        errno = EFAULT;
        return -1;
    }

    int rc = process_commands (0, true);
    if (unlikely (rc != 0)) {
        leave_public_api ();
        return -1;
    }

    msg_->reset_flags (msg_t::more);
    if (flags_ & ZLINK_SNDMORE)
        msg_->set_flags (msg_t::more);
    msg_->reset_metadata ();

    {
        lock_public_api_sync ();
        rc = xsend_routed (target_rid_, msg_);
        unlock_public_api_sync ();
    }
    if (rc == 0) {
        leave_public_api ();
        return 0;
    }
    if (unlikely (rc == -2)) {
        if (!((flags_ & ZLINK_DONTWAIT) || options.sndtimeo == 0)) {
            rc = msg_->close ();
            errno_assert (rc == 0);
            rc = msg_->init ();
            errno_assert (rc == 0);
            leave_public_api ();
            return 0;
        }
    }
    if (unlikely (errno != EAGAIN)) {
        if ((flags_ & ZLINK_DONTWAIT) || options.sndtimeo == 0) {
            if (errno == ENOTCONN || errno == EHOSTUNREACH
                || errno == ETIMEDOUT) {
                arm_send_ready_notification ();
            }
        }
        leave_public_api ();
        return -1;
    }

    if ((flags_ & ZLINK_DONTWAIT) || options.sndtimeo == 0) {
        arm_send_ready_notification ();
        leave_public_api ();
        return -1;
    }

    int timeout = options.sndtimeo;
    const uint64_t end = timeout < 0 ? 0 : (_clock.now_ms () + timeout);

    while (true) {
        rc = process_commands (timeout, false);
        if (unlikely (rc != 0)) {
            leave_public_api ();
            return -1;
        }
        {
            lock_public_api_sync ();
            rc = xsend_routed (target_rid_, msg_);
            unlock_public_api_sync ();
        }
        if (rc == 0)
            break;
        if (unlikely (errno != EAGAIN)) {
            leave_public_api ();
            return -1;
        }
        if (timeout > 0) {
            timeout = static_cast<int> (end - _clock.now_ms ());
            if (timeout <= 0) {
                errno = EAGAIN;
                leave_public_api ();
                return -1;
            }
        }
    }

    leave_public_api ();
    return 0;
}

int zlink::socket_base_t::rollback ()
{
    if (!enter_public_api ())
        return -1;

    if (unlikely (_ctx_terminated)) {
        leave_public_api ();
        errno = ETERM;
        return -1;
    }

    lock_public_api_sync ();
    const int rc = xrollback ();
    unlock_public_api_sync ();
    leave_public_api ();
    return rc;
}

int zlink::socket_base_t::recv (msg_t *msg_, int flags_)
{
    if (unlikely (_ctx_terminated)) {
        errno = ETERM;
        return -1;
    }

    if (unlikely (!msg_ || !msg_->check ())) {
        errno = EFAULT;
        return -1;
    }

    if (++_ticks == inbound_poll_rate) {
        if (unlikely (process_commands (0, false) != 0))
            return -1;
        _ticks = 0;
    }

    int rc = xrecv (msg_);
    if (unlikely (rc != 0 && errno != EAGAIN))
        return -1;

    if (rc == 0) {
        extract_flags (msg_);
        return 0;
    }

    if ((flags_ & ZLINK_DONTWAIT) || options.rcvtimeo == 0) {
        if (unlikely (process_commands (0, false) != 0))
            return -1;
        _ticks = 0;

        rc = xrecv (msg_);
        if (rc < 0)
            return rc;
        extract_flags (msg_);
        return 0;
    }

    int timeout = options.rcvtimeo;
    const uint64_t end = timeout < 0 ? 0 : (_clock.now_ms () + timeout);

    bool block = (_ticks != 0);
    while (true) {
        if (unlikely (process_commands (block ? timeout : 0, false) != 0))
            return -1;
        rc = xrecv (msg_);
        if (rc == 0) {
            _ticks = 0;
            break;
        }
        if (unlikely (errno != EAGAIN))
            return -1;
        block = true;
        if (timeout > 0) {
            timeout = static_cast<int> (end - _clock.now_ms ());
            if (timeout <= 0) {
                errno = EAGAIN;
                return -1;
            }
        }
    }

    extract_flags (msg_);
    return 0;
}

int zlink::socket_base_t::recv_routed (msg_t *msg_,
                                       zlink_routing_id_t *source_rid_out_,
                                       int flags_)
{
    if (source_rid_out_)
        memset (source_rid_out_, 0, sizeof (*source_rid_out_));

    if (unlikely (_ctx_terminated)) {
        errno = ETERM;
        return -1;
    }

    if (unlikely (!msg_ || !msg_->check ())) {
        errno = EFAULT;
        return -1;
    }

    if (++_ticks == inbound_poll_rate) {
        if (unlikely (process_commands (0, false) != 0)) {
            return -1;
        }
        _ticks = 0;
    }

    int rc = xrecv_routed (msg_, source_rid_out_);
    if (unlikely (rc != 0 && errno != EAGAIN))
        return -1;

    if (rc == 0) {
        extract_flags (msg_);
        return 0;
    }

    if ((flags_ & ZLINK_DONTWAIT) || options.rcvtimeo == 0) {
        if (unlikely (process_commands (0, false) != 0))
            return -1;
        _ticks = 0;

        rc = xrecv_routed (msg_, source_rid_out_);
        if (rc < 0)
            return rc;
        extract_flags (msg_);
        return 0;
    }

    int timeout = options.rcvtimeo;
    const uint64_t end = timeout < 0 ? 0 : (_clock.now_ms () + timeout);

    bool block = (_ticks != 0);
    while (true) {
        if (unlikely (process_commands (block ? timeout : 0, false) != 0))
            return -1;
        rc = xrecv_routed (msg_, source_rid_out_);
        if (rc == 0) {
            _ticks = 0;
            break;
        }
        if (unlikely (errno != EAGAIN))
            return -1;
        block = true;
        if (timeout > 0) {
            timeout = static_cast<int> (end - _clock.now_ms ());
            if (timeout <= 0) {
                errno = EAGAIN;
                return -1;
            }
        }
    }

    extract_flags (msg_);
    return 0;
}

void zlink::socket_base_t::extract_flags (const msg_t *msg_)
{
    if (unlikely (msg_->flags () & msg_t::routing_id))
        zlink_assert (options.recv_routing_id);

    _rcvmore = (msg_->flags () & msg_t::more) != 0;
}
