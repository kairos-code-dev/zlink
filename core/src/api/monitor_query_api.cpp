/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "api/config_result_internal.hpp"
#include "api/monitor_api_internal.hpp"
#include "api/recv_result_internal.hpp"
#include "api/socket_api_internal.hpp"

#include <cstring>

#include "core/c_api_copy_internal.hpp"
#include "core/recv_internal.hpp"

int socket_monitor_snapshot_provider (void *subject_,
                                      zlink_monitor_snapshot_t *out_)
{
    zlink::socket_base_t *socket =
      static_cast<zlink::socket_base_t *> (subject_);
    if (!socket || !out_) {
        errno = EINVAL;
        return -1;
    }
    return socket->monitor_snapshot (out_);
}

int recv_socket_monitor_event_unchecked (void *monitor_socket_,
                                         zlink_monitor_event_t *event_,
                                         int flags_)
{
    if (!monitor_socket_ || !event_) {
        errno = EINVAL;
        return -1;
    }

    zlink_msg_t msg;
    zlink_msg_init (&msg);
    int rc = zlink::recv_msg_internal (monitor_socket_, &msg, flags_);
    if (rc == -1) {
        zlink_msg_close (&msg);
        return -1;
    }

    memset (event_, 0, sizeof (*event_));

    if (zlink_msg_size (&msg) == sizeof (*event_)) {
        memcpy (event_, zlink_msg_data (&msg), sizeof (*event_));
        zlink_msg_close (&msg);
        return 0;
    }

    if (zlink_msg_size (&msg) < sizeof (uint64_t)) {
        zlink_msg_close (&msg);
        errno = EPROTO;
        return -1;
    }

    memcpy (&event_->event, zlink_msg_data (&msg), sizeof (uint64_t));
    zlink_msg_close (&msg);

    const int follow_flags = flags_ & ~ZLINK_DONTWAIT;

    zlink_msg_init (&msg);
    rc = zlink::recv_msg_internal (monitor_socket_, &msg, follow_flags);
    if (rc == -1) {
        zlink_msg_close (&msg);
        return -1;
    }
    if (zlink_msg_size (&msg) < sizeof (uint64_t)) {
        zlink_msg_close (&msg);
        errno = EPROTO;
        return -1;
    }

    uint64_t value_count = 0;
    memcpy (&value_count, zlink_msg_data (&msg), sizeof (uint64_t));
    zlink_msg_close (&msg);

    for (uint64_t i = 0; i < value_count; ++i) {
        zlink_msg_init (&msg);
        rc = zlink::recv_msg_internal (monitor_socket_, &msg, follow_flags);
        if (rc == -1) {
            zlink_msg_close (&msg);
            return -1;
        }
        if (i == 0 && zlink_msg_size (&msg) >= sizeof (uint64_t))
            memcpy (&event_->value, zlink_msg_data (&msg), sizeof (uint64_t));
        zlink_msg_close (&msg);
    }

    zlink_msg_init (&msg);
    rc = zlink::recv_msg_internal (monitor_socket_, &msg, follow_flags);
    if (rc == -1) {
        zlink_msg_close (&msg);
        return -1;
    }
    zlink::copy_routing_id_from_msg (msg, &event_->routing_id);
    zlink_msg_close (&msg);

    zlink_msg_init (&msg);
    rc = zlink::recv_msg_internal (monitor_socket_, &msg, follow_flags);
    if (rc == -1) {
        zlink_msg_close (&msg);
        return -1;
    }
    zlink::copy_fixed_c_string_from_bytes (
      event_->local_addr, sizeof (event_->local_addr), zlink_msg_data (&msg),
      zlink_msg_size (&msg));
    zlink_msg_close (&msg);

    zlink_msg_init (&msg);
    rc = zlink::recv_msg_internal (monitor_socket_, &msg, follow_flags);
    if (rc == -1) {
        zlink_msg_close (&msg);
        return -1;
    }
    zlink::copy_fixed_c_string_from_bytes (
      event_->remote_addr, sizeof (event_->remote_addr), zlink_msg_data (&msg),
      zlink_msg_size (&msg));
    zlink_msg_close (&msg);

    return 0;
}

zlink_recv_result_t zlink_socket_monitor_recv (void *monitor_,
                                               zlink_socket_monitor_event_t *out_,
                                               zlink_recv_flags_t flags_)
{
    if (require_monitor_recv_model (monitor_) != 0)
        return zlink::recv_result_internal::from_errno (errno);
    return zlink::recv_result_internal::from_rc (
      recv_socket_monitor_event_unchecked (monitor_, out_,
                                           static_cast<int> (flags_)));
}

zlink_config_result_t zlink_monitor_snapshot (void *monitor_,
                                              zlink_monitor_snapshot_t *out_)
{
    if (!out_) {
        errno = EINVAL;
        return ZLINK_CONFIG_INVALID_ARGUMENT;
    }

    socket_handle_t handle = as_socket_handle (monitor_);
    if (!handle.socket)
        return ZLINK_CONFIG_INVALID_HANDLE;

    monitor_handler_state_t *state = find_monitor_handler_state (handle.socket);
    if (!state) {
        errno = EINVAL;
        return ZLINK_CONFIG_INVALID_ARGUMENT;
    }

    monitor_snapshot_provider_fn provider =
      state->snapshot_provider.load (std::memory_order_acquire);
    void *subject = state->snapshot_subject.load (std::memory_order_acquire);
    if (!provider) {
        errno = ENOTSUP;
        return ZLINK_CONFIG_NOT_SUPPORTED;
    }

    return zlink::config_result_internal::from_rc (provider (subject, out_));
}
