/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include <climits>

#include "api/service_api_internal.hpp"
#include "api/socket_api_internal.hpp"
#include "api/socket_message_api_internal.hpp"
#include "api/submit_result_internal.hpp"
#include "api/routing_id_internal.hpp"
#include "sockets/stream_dispatch_internal.hpp"
#include "core/msg.hpp"
#include "core/multipart_send_txn.hpp"
#include "utils/err.hpp"
#include "utils/likely.hpp"

namespace
{
int validate_send_parts (zlink_msg_t *parts_, size_t part_count_)
{
    if ((!parts_ && part_count_ > 0) || part_count_ == 0) {
        errno = EFAULT;
        return -1;
    }

    return 0;
}

void consume_send_frame (zlink_msg_t *part_)
{
    if (!part_)
        return;

    zlink::msg_t *msg = reinterpret_cast<zlink::msg_t *> (part_);
    if (!msg->check ())
        return;

    const int close_rc = msg->close ();
    errno_assert (close_rc == 0);
    const int init_rc = msg->init ();
    errno_assert (init_rc == 0);
}

void consume_send_frames_from (zlink_msg_t *parts_,
                               size_t start_index_,
                               size_t part_count_)
{
    if (!parts_)
        return;

    for (size_t i = start_index_; i < part_count_; ++i)
        consume_send_frame (&parts_[i]);
}

bool try_extract_router_target_rid (const zlink_msg_t *part_,
                                    zlink_routing_id_t *out_)
{
    if (!part_ || !out_)
        return false;

    zlink::msg_t *msg =
      reinterpret_cast<zlink::msg_t *> (const_cast<zlink_msg_t *> (part_));
    if (!msg->check ())
        return false;

    const size_t size = msg->size ();
    if (size == 0 || size > sizeof (out_->data))
        return false;

    out_->size = static_cast<uint8_t> (size);
    memcpy (out_->data, msg->data (), size);
    return true;
}

int s_sendmsg (socket_handle_t handle_,
               zlink_msg_t *msg_,
               zlink_send_flags_t flags_)
{
    size_t sz = zlink_msg_size (msg_);
    int rc = handle_.socket->send (reinterpret_cast<zlink::msg_t *> (msg_),
                                   flags_);
    if (unlikely (rc < 0))
        return -1;

    size_t max_msgsz = INT_MAX;
    return static_cast<int> (sz < max_msgsz ? sz : max_msgsz);
}

int validate_send_flags (int flags_)
{
    if (flags_ != 0 && flags_ != ZLINK_DONTWAIT) {
        errno = ENOTSUP;
        return -1;
    }
    return 0;
}

bool is_singlepart_fast_socket_type (int type_)
{
    return type_ == ZLINK_CORE_SOCKET_PAIR || type_ == ZLINK_CORE_SOCKET_DEALER;
}

int send_socket_singlepart_fast (socket_handle_t handle_,
                                 zlink_msg_t *msg_,
                                 zlink_send_flags_t flags_)
{
    if (!handle_.socket || !msg_) {
        errno = EFAULT;
        return -1;
    }
    if (validate_send_flags (flags_) != 0)
        return -1;

    zlink::msg_t *core_msg = reinterpret_cast<zlink::msg_t *> (msg_);
    if (!core_msg->check ()) {
        errno = EFAULT;
        return -1;
    }

    if (handle_.socket->send (
          core_msg, static_cast<int> (flags_ & ZLINK_DONTWAIT))
        != 0)
        return -1;

    errno = 0;
    return 0;
}

void consume_stream_send_msg (zlink::msg_t *msg_)
{
    if (!msg_ || !msg_->check ())
        return;

    int rc = msg_->close ();
    errno_assert (rc == 0);
    rc = msg_->init ();
    errno_assert (rc == 0);
}

int send_stream_message (socket_handle_t handle_,
                         const zlink_routing_id_t *rid_,
                         zlink_msg_t *msg_,
                         zlink_send_flags_t flags_)
{
    if (!handle_.socket)
        return -1;

    if (!msg_) {
        errno = EINVAL;
        return -1;
    }

    zlink::msg_t *core_msg = reinterpret_cast<zlink::msg_t *> (msg_);
    if (!core_msg->check ()) {
        errno = EFAULT;
        return -1;
    }

    if (!is_stream_type (handle_)) {
        errno = EINVAL;
        return -1;
    }

    bool tried_current_dispatch_send = false;
    if (handle_.socket->stream_dispatch_in_callback ()) {
        const uint32_t current_routing_id =
          zlink::stream_dispatch_context_t::current_routing_id ();
        uint32_t current_target_routing_id = 0;
        if (current_routing_id != 0
            && zlink::routing_id_internal::to_u32 (rid_,
                                                  &current_target_routing_id)
            && current_target_routing_id == current_routing_id) {
            tried_current_dispatch_send = true;
            const int send_rc =
              handle_.socket->stream_dispatch_send_current_msg_from_io (
                core_msg,
                static_cast<zlink_send_flags_t> (flags_ & ZLINK_DONTWAIT));
            if (send_rc >= 0) {
                errno = 0;
                return 0;
            }
            if (errno != EAGAIN) {
                const int err = errno;
                consume_stream_send_msg (core_msg);
                errno = err;
                return -1;
            }
        }

        if (!tried_current_dispatch_send) {
            const int send_rc =
              handle_.socket->stream_dispatch_send_msg_from_io (
                rid_, core_msg,
                static_cast<zlink_send_flags_t> (
                  (flags_ & ZLINK_DONTWAIT) | ZLINK_DONTWAIT));
            if (send_rc >= 0) {
                errno = 0;
                return 0;
            }
            if (errno != EAGAIN) {
                const int err = errno;
                consume_stream_send_msg (core_msg);
                errno = err;
                return -1;
            }
        }
    }

    uint32_t routing_id = 0;
    if (!zlink::routing_id_internal::to_u32 (rid_, &routing_id)) {
        const int err = errno;
        consume_stream_send_msg (core_msg);
        errno = err;
        return -1;
    }

    stream_api_lock_t api_lock (handle_);
    if (core_msg->set_routing_id (routing_id) != 0) {
        const int err = errno;
        consume_stream_send_msg (core_msg);
        errno = err;
        return -1;
    }

    const zlink_send_flags_t base_flags =
      static_cast<zlink_send_flags_t> (flags_ & ZLINK_DONTWAIT);
    const int send_rc =
      s_sendmsg (handle_, reinterpret_cast<zlink_msg_t *> (core_msg),
                 base_flags);
    if (send_rc < 0) {
        const int err = errno;
        if (err != EAGAIN)
            consume_stream_send_msg (core_msg);
        errno = err;
        return -1;
    }

    errno = 0;
    return 0;
}

int validate_socket_send_request (socket_handle_t handle_,
                                  zlink_msg_t *parts_,
                                  size_t part_count_,
                                  zlink_send_flags_t flags_)
{
    if (!handle_.socket) {
        errno = EFAULT;
        return -1;
    }
    if (validate_send_flags (flags_) != 0)
        return -1;
    return validate_send_parts (parts_, part_count_);
}

int send_socket_unrouted_parts (socket_handle_t handle_,
                                zlink_msg_t *parts_,
                                size_t part_count_,
                                zlink_send_flags_t flags_)
{
    // Hot path: PAIR/DEALER single-part public send reaches here on every
    // message. Keep this path free of extra allocation and avoid adding
    // indirection beyond the public contract checks.
    const int type = socket_type (handle_);
    if (type == ZLINK_CORE_SOCKET_PUB || type == ZLINK_CORE_SOCKET_SUB
        || type == ZLINK_CORE_SOCKET_XSUB || type == ZLINK_CORE_SOCKET_XPUB) {
        errno = ENOTSUP;
        return -1;
    }

    if (part_count_ == 1) {
        const int rc = s_sendmsg (
          handle_, &parts_[0],
          static_cast<zlink_send_flags_t> (flags_ & ZLINK_DONTWAIT));
        if (rc < 0)
            return -1;
        errno = 0;
        return 0;
    }

    return zlink::logical_multipart_send (handle_.socket, parts_, part_count_,
                                          flags_);
}

int send_socket_routed_parts (socket_handle_t handle_,
                              const zlink_routing_id_t *target_rid_,
                              zlink_msg_t *parts_,
                              size_t part_count_,
                              zlink_send_flags_t flags_)
{
    const int type = socket_type (handle_);
    if (type == ZLINK_CORE_SOCKET_STREAM) {
        if (part_count_ != 1) {
            errno = ENOTSUP;
            return -1;
        }

        const int rc =
          send_stream_message (handle_, target_rid_, &parts_[0], flags_);
        if (rc < 0)
            return -1;
        errno = 0;
        return 0;
    }

    if (type != ZLINK_CORE_SOCKET_ROUTER) {
        errno = ENOTSUP;
        return -1;
    }

    if (part_count_ == 1) {
        const int rc = handle_.socket->send_routed (
          target_rid_, reinterpret_cast<zlink::msg_t *> (&parts_[0]),
          static_cast<int> (flags_ & ZLINK_DONTWAIT));
        if (rc != 0)
            return -1;
        errno = 0;
        return 0;
    }

    return zlink::logical_multipart_send_routed (handle_.socket, target_rid_,
                                                 parts_, part_count_, flags_);
}

int send_socket_parts (socket_handle_t handle_,
                       const zlink_routing_id_t *target_rid_,
                       zlink_msg_t *parts_,
                       size_t part_count_,
                       zlink_send_flags_t flags_)
{
    if (validate_socket_send_request (handle_, parts_, part_count_, flags_) != 0)
        return -1;

    if (!target_rid_) {
        const int type = socket_type (handle_);
        const bool blocking_send = (flags_ & ZLINK_DONTWAIT) == 0;

        if (type == ZLINK_CORE_SOCKET_ROUTER && blocking_send
            && part_count_ > 1) {
            zlink_routing_id_t target_rid;
            memset (&target_rid, 0, sizeof (target_rid));
            if (try_extract_router_target_rid (&parts_[0], &target_rid)) {
                const int rc = send_socket_routed_parts (
                  handle_, &target_rid, parts_ + 1, part_count_ - 1, flags_);
                consume_send_frame (&parts_[0]);
                if (rc != 0)
                    consume_send_frames_from (parts_, 1, part_count_);
                return rc;
            }
        }

        return send_socket_unrouted_parts (handle_, parts_, part_count_, flags_);
    }

    return send_socket_routed_parts (handle_, target_rid_, parts_, part_count_,
                                     flags_);
}

int publish_socket_parts (socket_handle_t handle_,
                          const char *topic_id_,
                          zlink_msg_t *parts_,
                          size_t part_count_,
                          zlink_send_flags_t flags_,
                          bool fallback_on_missing_sndtimeo_)
{
    if (validate_socket_send_request (handle_, parts_, part_count_, flags_) != 0)
        return -1;

    const int type = socket_type (handle_);
    if (type != ZLINK_CORE_SOCKET_PUB && type != ZLINK_CORE_SOCKET_XPUB) {
        errno = ENOTSUP;
        return -1;
    }

    return zlink::logical_multipart_publish (
      handle_.socket, topic_id_, parts_, part_count_, flags_,
      fallback_on_missing_sndtimeo_);
}

int send_service_or_fault (void *handle_,
                           zlink_msg_t *parts_,
                           size_t part_count_,
                           zlink_send_flags_t flags_)
{
    const int service_rc =
      zlink_service_send_internal (handle_, parts_, part_count_, flags_);
    if (service_rc == 0 || errno != EFAULT)
        return service_rc;

    errno = EFAULT;
    return -1;
}

int send_rid_service_or_fault (void *handle_,
                               const zlink_routing_id_t *target_rid_,
                               zlink_msg_t *parts_,
                               size_t part_count_,
                               zlink_send_flags_t flags_)
{
    const int service_rc = zlink_service_send_rid_internal (
      handle_, target_rid_, parts_, part_count_, flags_);
    if (service_rc == 0 || errno != EFAULT)
        return service_rc;

    errno = EFAULT;
    return -1;
}

int publish_service_or_fault (void *subject_,
                              const char *topic_id_,
                              zlink_msg_t *parts_,
                              size_t part_count_,
                              zlink_send_flags_t flags_)
{
    const int service_rc = zlink_service_publish_internal (
      subject_, topic_id_, parts_, part_count_, flags_);
    if (service_rc == 0 || errno != EFAULT)
        return service_rc;

    errno = EFAULT;
    return -1;
}
}

int zlink_socket_send_internal (void *socket_,
                                zlink_msg_t *parts_,
                                size_t part_count_,
                                zlink_send_flags_t flags_)
{
    socket_handle_t handle = as_socket_handle (socket_);
    if (!handle.socket)
        return -1;

    return send_socket_parts (handle, NULL, parts_, part_count_, flags_);
}

int zlink_socket_send_rid_internal (void *socket_,
                                    const zlink_routing_id_t *target_rid_,
                                    zlink_msg_t *parts_,
                                    size_t part_count_,
                                    zlink_send_flags_t flags_)
{
    socket_handle_t handle = as_socket_handle (socket_);
    if (!handle.socket)
        return -1;

    return send_socket_parts (handle, target_rid_, parts_, part_count_, flags_);
}

int zlink_socket_publish_internal (void *socket_,
                                   const char *topic_id_,
                                   zlink_msg_t *parts_,
                                   size_t part_count_,
                                   zlink_send_flags_t flags_)
{
    socket_handle_t handle = as_socket_handle (socket_);
    if (!handle.socket)
        return -1;

    return publish_socket_parts (
      handle, topic_id_, parts_, part_count_, flags_,
      (flags_ & ZLINK_DONTWAIT) == 0);
}

zlink_submit_result_t zlink_send (void *s_,
                                  zlink_msg_t *parts_,
                                  size_t part_count_,
                                  zlink_send_flags_t flags_)
{
    if (!s_) {
        errno = EFAULT;
        return ZLINK_SUBMIT_INVALID_HANDLE;
    }

    zlink::socket_base_t *socket = try_as_socket (s_);
    if (socket) {
        socket_handle_t handle = make_socket_handle (socket);
        if (part_count_ == 1
            && is_singlepart_fast_socket_type (socket->socket_type ())) {
            return zlink::submit_result_internal::from_rc (
              send_socket_singlepart_fast (handle, &parts_[0], flags_));
        }
        return zlink::submit_result_internal::from_rc (
          send_socket_parts (handle, NULL, parts_, part_count_, flags_));
    }

    return zlink::submit_result_internal::from_rc (
      send_service_or_fault (s_, parts_, part_count_, flags_));
}

zlink_submit_result_t zlink_publish (void *subject_,
                                     const char *topic_id_,
                                     zlink_msg_t *parts_,
                                     size_t part_count_,
                                     zlink_send_flags_t flags_)
{
    if (!subject_) {
        errno = EFAULT;
        return ZLINK_SUBMIT_INVALID_HANDLE;
    }

    zlink::socket_base_t *socket = try_as_socket (subject_);
    if (socket)
        return zlink::submit_result_internal::from_rc (
          publish_socket_parts (make_socket_handle (socket), topic_id_, parts_,
                                part_count_, flags_,
                                (flags_ & ZLINK_DONTWAIT) == 0));

    return zlink::submit_result_internal::from_rc (publish_service_or_fault (
      subject_, topic_id_, parts_, part_count_, flags_));
}

zlink_submit_result_t zlink_send_rid (void *s_,
                                      const zlink_routing_id_t *target_rid_,
                                      zlink_msg_t *parts_,
                                      size_t part_count_,
                                      zlink_send_flags_t flags_)
{
    if (!s_) {
        errno = EFAULT;
        return ZLINK_SUBMIT_INVALID_HANDLE;
    }

    zlink::socket_base_t *socket = try_as_socket (s_);
    if (socket) {
        socket_handle_t handle = make_socket_handle (socket);
        if (target_rid_ && socket_type (handle) == ZLINK_CORE_SOCKET_STREAM) {
            if (validate_send_flags (flags_) != 0)
                return zlink::submit_result_internal::from_rc (-1);
            if ((!parts_ && part_count_ > 0) || part_count_ == 0) {
                errno = EFAULT;
                return zlink::submit_result_internal::from_rc (-1);
            }
            if (part_count_ != 1) {
                errno = ENOTSUP;
                return zlink::submit_result_internal::from_rc (-1);
            }

            return zlink::submit_result_internal::from_rc (
              send_stream_message (handle, target_rid_, &parts_[0], flags_));
        }

        return zlink::submit_result_internal::from_rc (
          send_socket_parts (handle, target_rid_, parts_, part_count_, flags_));
    }

    return zlink::submit_result_internal::from_rc (
      send_rid_service_or_fault (s_, target_rid_, parts_, part_count_, flags_));
}
