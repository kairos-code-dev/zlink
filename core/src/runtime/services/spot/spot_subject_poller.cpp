/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/spot/spot_subject_access.hpp"

#include "api/service/service_handle_internal.hpp"
#include "api/service/service_mode_internal.hpp"
#include "services/spot/spot_internal_receiver.hpp"
#include "services/spot/spot_node.hpp"
#include "services/spot/spot_node_access.hpp"
#include "services/spot/spot_pub.hpp"
#include "services/spot/spot_sub.hpp"

zlink::socket_base_t *spot_pub_poller_socket (void *spot_pub_)
{
    zlink::spot_pub_t *pub = as_spot_pub_side_handle (spot_pub_);
    if (!pub) {
        errno = EFAULT;
        return NULL;
    }
    return pub->poller_socket ();
}

zlink::socket_base_t *spot_sub_poller_socket (void *spot_sub_)
{
    zlink::spot_sub_t *sub = as_spot_sub_side_handle (spot_sub_);
    if (!sub) {
        errno = EFAULT;
        return NULL;
    }
    return sub->poller_socket ();
}

zlink::socket_base_t *resolve_spot_pub_subject_poller_socket (
  void *spot_or_node_)
{
    if (is_registered_spot_handle (spot_or_node_)) {
        spot_handle_t *spot = static_cast<spot_handle_t *> (spot_or_node_);
        zlink::service_public_api_scope_t admission (spot->public_api);
        if (!admission.acquired ())
            return NULL;
        errno = ENOTSUP;
        return NULL;
    }
    if (is_registered_spot_node_handle (spot_or_node_)) {
        errno = ENOTSUP;
        return NULL;
    }
    errno = EFAULT;
    return NULL;
}

zlink::socket_base_t *resolve_spot_sub_subject_poller_socket (
  void *spot_or_node_)
{
    if (is_registered_spot_handle (spot_or_node_)) {
        spot_handle_t *spot = static_cast<spot_handle_t *> (spot_or_node_);
        zlink::service_public_api_scope_t admission (spot->public_api);
        if (!admission.acquired ())
            return NULL;
        if (!spot->logical_state || !spot->node) {
            errno = EFAULT;
            return NULL;
        }
        errno = ENOTSUP;
        return NULL;
    }
    if (is_registered_spot_node_handle (spot_or_node_)) {
        zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (spot_or_node_);
        zlink::service_public_api_scope_t admission (node->public_api_guard ());
        if (!admission.acquired ())
            return NULL;
        zlink::spot_internal_receiver_t *receiver =
          zlink::spot_node_access_t::ensure_internal_receiver (node);
        if (receiver && receiver->impl ())
            return receiver->impl ()->poller_socket ();
        zlink::spot_sub_t *sub = node->ensure_default_sub ();
        return sub ? sub->poller_socket () : NULL;
    }
    errno = EFAULT;
    return NULL;
}

int resolve_spot_sub_subject_poller_fd (void *spot_or_node_,
                                        zlink_fd_t *fd_out_)
{
    if (!fd_out_) {
        errno = EFAULT;
        return -1;
    }
    *fd_out_ = zlink::retired_fd;

    if (!is_registered_spot_handle (spot_or_node_)) {
        errno = EFAULT;
        return -1;
    }

    spot_handle_t *spot = static_cast<spot_handle_t *> (spot_or_node_);
    zlink::service_public_api_scope_t admission (spot->public_api);
    if (!admission.acquired ())
        return -1;
    if (!spot->logical_state || !spot->node) {
        errno = EFAULT;
        return -1;
    }
    if (!spot->logical_state->subscribe_signaler.valid ()) {
        errno = EFAULT;
        return -1;
    }

    bool should_signal = false;
    {
        zlink::scoped_lock_t lock (spot->logical_state->pubsub_sync);
        if (!spot->logical_state->subscribe_queue.empty ()
            && !spot->logical_state->subscribe_signal_armed) {
            spot->logical_state->subscribe_signal_armed = true;
            should_signal = true;
        }
    }
    if (should_signal)
        spot->logical_state->subscribe_signaler.send ();

    *fd_out_ = spot->logical_state->subscribe_signaler.get_fd ();
    return 0;
}

int resolve_spot_pub_subject_poller_fd (void *spot_or_node_,
                                        zlink_fd_t *fd_out_)
{
    if (!fd_out_) {
        errno = EFAULT;
        return -1;
    }
    *fd_out_ = zlink::retired_fd;

    if (is_registered_spot_handle (spot_or_node_)) {
        spot_handle_t *spot = static_cast<spot_handle_t *> (spot_or_node_);
        zlink::service_public_api_scope_t admission (spot->public_api);
        if (!admission.acquired ())
            return -1;
        if (!spot->logical_state || !spot->node
            || !spot->logical_state->send_ready_signaler.valid ()) {
            errno = EFAULT;
            return -1;
        }
        *fd_out_ = spot->logical_state->send_ready_signaler.get_fd ();
        return 0;
    }

    if (is_registered_spot_node_handle (spot_or_node_)) {
        zlink::spot_node_t *node =
          static_cast<zlink::spot_node_t *> (spot_or_node_);
        zlink::service_public_api_scope_t admission (node->public_api_guard ());
        if (!admission.acquired ())
            return -1;
        return node->send_ready_fd (fd_out_);
    }

    errno = EFAULT;
    return -1;
}

void drain_spot_send_ready_signal (void *spot_or_node_)
{
    if (is_registered_spot_handle (spot_or_node_)) {
        spot_handle_t *spot = static_cast<spot_handle_t *> (spot_or_node_);
        zlink::service_public_api_scope_t admission (spot->public_api);
        if (!admission.acquired () || !spot->logical_state)
            return;
        while (spot->logical_state->send_ready_signaler.recv_failable () == 0) {
        }
        zlink::scoped_lock_t lock (spot->logical_state->pubsub_sync);
        spot->logical_state->send_ready_signal_armed = false;
        return;
    }
    if (is_registered_spot_node_handle (spot_or_node_)) {
        zlink::spot_node_t *node =
          static_cast<zlink::spot_node_t *> (spot_or_node_);
        node->drain_send_ready_signal ();
    }
}

void notify_spot_send_ready_recovery (zlink::spot_node_t *node_)
{
    if (node_)
        node_->notify_send_ready_recovery ();
}

int resolve_spot_send_timeout_ms (void *spot_or_node_)
{
    if (is_registered_spot_handle (spot_or_node_)) {
        spot_handle_t *spot = static_cast<spot_handle_t *> (spot_or_node_);
        if (spot->pending_pub_defaults.sndtimeo.enabled)
            return spot->pending_pub_defaults.sndtimeo.value;
        if (spot->node) {
            zlink::spot_node_pub_defaults_t defaults = spot->node->load_pub_defaults ();
            if (defaults.sndtimeo.enabled)
                return defaults.sndtimeo.value;
        }
        return -1;
    }
    if (is_registered_spot_node_handle (spot_or_node_)) {
        zlink::spot_node_t *node =
          static_cast<zlink::spot_node_t *> (spot_or_node_);
        zlink::spot_node_pub_defaults_t defaults = node->load_pub_defaults ();
        if (defaults.sndtimeo.enabled)
            return defaults.sndtimeo.value;
    }
    return -1;
}
