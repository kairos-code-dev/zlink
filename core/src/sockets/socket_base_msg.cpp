/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "sockets/socket_base.hpp"
#include "utils/err.hpp"
#include "utils/likely.hpp"

namespace
{
void prepare_direct_send_message (zlink::msg_t *msg_, int flags_)
{
    msg_->reset_flags (zlink::msg_t::more);
    if (flags_ & ZLINK_SNDMORE)
        msg_->set_flags (zlink::msg_t::more);
}
}

int zlink::socket_base_t::send (msg_t *msg_, int flags_)
{
    // Hot path: every public single-part send pays this steady-state cost.
    // Keep lifecycle/backpressure logic correct, but avoid thickening the
    // success path with new work that is not contract-critical.
    socket_public_send_scope_t send_scope (
      lifecycle_coordinator (), direct_send_needs_public_api_sync ());
    if (!send_scope.acquired ())
        return -1;

    return send_scoped (msg_, flags_, send_scope);
}

int zlink::socket_base_t::send_scoped (msg_t *msg_,
                                       int flags_,
                                       socket_public_send_scope_t &send_scope)
{
    return send_direct_with_retry (NULL, msg_, flags_, send_scope);
}

int zlink::socket_base_t::send_routed (const zlink_routing_id_t *target_rid_,
                                       msg_t *msg_,
                                       int flags_)
{
    socket_public_send_scope_t send_scope (
      lifecycle_coordinator (), true);
    if (!send_scope.acquired ())
        return -1;

    return send_routed_scoped (target_rid_, msg_, flags_, send_scope);
}

int zlink::socket_base_t::send_routed_scoped (
  const zlink_routing_id_t *target_rid_,
  msg_t *msg_,
  int flags_,
  socket_public_send_scope_t &send_scope)
{
    if (unlikely (!target_rid_)) {
        errno = EFAULT;
        return -1;
    }

    return send_direct_with_retry (target_rid_, msg_, flags_, send_scope);
}

bool zlink::socket_base_t::direct_send_needs_public_api_sync () const
{
    return options.type != ZLINK_CORE_SOCKET_PAIR;
}

int zlink::socket_base_t::send_direct_with_retry (
  const zlink_routing_id_t *target_rid_,
  msg_t *msg_,
  int flags_,
  socket_public_send_scope_t &send_scope)
{
    zlink_assert (send_scope.acquired ());

    if (unlikely (_ctx_terminated)) {
        errno = ETERM;
        return -1;
    }

    if (unlikely (!msg_ || !msg_->check ())) {
        errno = EFAULT;
        return -1;
    }

    int rc = process_commands (0, true);
    if (unlikely (rc != 0))
        return -1;

    prepare_direct_send_message (msg_, flags_);

    rc = target_rid_ ? xsend_routed (target_rid_, msg_) : xsend (msg_);
    if (rc == 0)
        return 0;
    if (unlikely (rc == -2)) {
        if (!((flags_ & ZLINK_DONTWAIT) || options.sndtimeo == 0)) {
            rc = msg_->close ();
            errno_assert (rc == 0);
            rc = msg_->init ();
            errno_assert (rc == 0);
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
        return -1;
    }

    if ((flags_ & ZLINK_DONTWAIT) || options.sndtimeo == 0) {
        arm_send_ready_notification ();
        return -1;
    }

    int timeout = options.sndtimeo;
    const uint64_t end = timeout < 0 ? 0 : (_clock.now_ms () + timeout);
    const bool hold_sync_during_retry =
      send_scope.should_hold_sync_during_retry (send_ready_handler_active ());

    if (!hold_sync_during_retry)
        send_scope.release_sync_for_retry ();

    while (true) {
        rc = process_commands (timeout, false);
        if (unlikely (rc != 0))
            return -1;
        if (!hold_sync_during_retry)
            send_scope.reacquire_sync_after_retry ();
        rc = target_rid_ ? xsend_routed (target_rid_, msg_) : xsend (msg_);
        if (rc == 0)
            break;
        if (!hold_sync_during_retry)
            send_scope.release_sync_for_retry ();
        if (unlikely (errno != EAGAIN))
            return -1;
        if (timeout > 0) {
            timeout = static_cast<int> (end - _clock.now_ms ());
            if (timeout <= 0) {
                errno = EAGAIN;
                return -1;
            }
        }
    }

    return 0;
}

int zlink::socket_base_t::rollback ()
{
    socket_lifecycle_coordinator_t &lifecycle = lifecycle_coordinator ();
    socket_public_api_scope_t admission (lifecycle);
    if (!admission.acquired ())
        return -1;

    if (unlikely (_ctx_terminated)) {
        errno = ETERM;
        return -1;
    }

    socket_public_api_lock_scope_t guard (lifecycle);
    const int rc = xrollback ();
    return rc;
}

int zlink::socket_base_t::rollback_scoped (socket_public_send_scope_t &scope_)
{
    zlink_assert (scope_.acquired ());
    const int rc = xrollback ();
    return rc;
}

int zlink::socket_base_t::recv (msg_t *msg_, int flags_)
{
    // Hot path: every public direct recv eventually reaches here. Keep the
    // no-message and success paths lean; recv-mode costs show up directly in
    // PAIR/DEALER small-message benchmarks.
    if (unlikely (_ctx_terminated)) {
        errno = ETERM;
        return -1;
    }

    if (unlikely (!msg_ || !msg_->check ())) {
        errno = EFAULT;
        return -1;
    }

    if (command_runtime ().should_poll_commands_after_recv (
          inbound_poll_rate)) {
        if (unlikely (process_commands (0, false) != 0))
            return -1;
        command_runtime ().reset_recv_ticks ();
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
        command_runtime ().reset_recv_ticks ();

        rc = xrecv (msg_);
        if (rc < 0)
            return rc;
        extract_flags (msg_);
        return 0;
    }

    int timeout = options.rcvtimeo;
    const uint64_t end = timeout < 0 ? 0 : (_clock.now_ms () + timeout);

    bool block = command_runtime ().should_block_on_recv ();
    while (true) {
        if (unlikely (process_commands (block ? timeout : 0, false) != 0))
            return -1;
        rc = xrecv (msg_);
        if (rc == 0) {
            command_runtime ().reset_recv_ticks ();
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

    if (command_runtime ().should_poll_commands_after_recv (
          inbound_poll_rate)) {
        if (unlikely (process_commands (0, false) != 0)) {
            return -1;
        }
        command_runtime ().reset_recv_ticks ();
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
        command_runtime ().reset_recv_ticks ();

        rc = xrecv_routed (msg_, source_rid_out_);
        if (rc < 0)
            return rc;
        extract_flags (msg_);
        return 0;
    }

    int timeout = options.rcvtimeo;
    const uint64_t end = timeout < 0 ? 0 : (_clock.now_ms () + timeout);

    bool block = command_runtime ().should_block_on_recv ();
    while (true) {
        if (unlikely (process_commands (block ? timeout : 0, false) != 0))
            return -1;
        rc = xrecv_routed (msg_, source_rid_out_);
        if (rc == 0) {
            command_runtime ().reset_recv_ticks ();
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
}
