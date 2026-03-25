/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include <climits>

#include "api/service_api_internal.hpp"
#include "api/socket_api_internal.hpp"
#include "core/msg.hpp"
#include "core/multipart_send_txn.hpp"
#include "utils/err.hpp"
#include "utils/likely.hpp"

namespace
{
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

bool parse_stream_routing_id (const zlink_routing_id_t *rid_,
                              uint32_t *routing_id_out_)
{
    if (!rid_ || !routing_id_out_ || rid_->size == 0
        || rid_->size > sizeof (rid_->data) || rid_->size != 4) {
        errno = EINVAL;
        return false;
    }

    *routing_id_out_ = (static_cast<uint32_t> (rid_->data[0]) << 24)
                       | (static_cast<uint32_t> (rid_->data[1]) << 16)
                       | (static_cast<uint32_t> (rid_->data[2]) << 8)
                       | static_cast<uint32_t> (rid_->data[3]);
    return true;
}

int clone_stream_send_msg (zlink::msg_t *src_, zlink::msg_t *dst_)
{
    if (!src_ || !dst_) {
        errno = EFAULT;
        return -1;
    }

    const int init_rc = dst_->init ();
    errno_assert (init_rc == 0);
    if (dst_->copy (*src_) != 0) {
        const int err = errno;
        (void) dst_->close ();
        errno = err;
        return -1;
    }

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

int stream_payload_result (size_t size_)
{
    return static_cast<int> (size_ < static_cast<size_t> (INT_MAX) ? size_
                                                                    : INT_MAX);
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

    uint32_t routing_id = 0;
    if (!parse_stream_routing_id (rid_, &routing_id))
        return -1;

    zlink::msg_t outbound_msg;
    if (clone_stream_send_msg (core_msg, &outbound_msg) != 0)
        return -1;

    const size_t payload_size = core_msg->size ();
    stream_api_lock_t api_lock (handle_);
    if (outbound_msg.set_routing_id (routing_id) != 0) {
        const int err = errno;
        (void) outbound_msg.close ();
        errno = err;
        return -1;
    }

    const int base_flags = flags_ & ZLINK_DONTWAIT;
    const int send_rc =
      s_sendmsg (handle_, reinterpret_cast<zlink_msg_t *> (&outbound_msg),
                 base_flags);
    if (send_rc < 0) {
        const int err = errno;
        (void) outbound_msg.close ();
        errno = err;
        return -1;
    }

    (void) outbound_msg.close ();
    consume_stream_send_msg (core_msg);
    errno = 0;
    return stream_payload_result (payload_size);
}

int send_socket_parts (socket_handle_t handle_,
                       const zlink_routing_id_t *target_rid_,
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
    if ((!parts_ && part_count_ > 0) || part_count_ == 0) {
        errno = EFAULT;
        return -1;
    }

    const int type = socket_type (handle_);

    if (target_rid_) {
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

        return zlink::logical_multipart_send_routed (handle_.socket,
                                                     target_rid_, parts_,
                                                     part_count_, flags_);
    }

    if (type == ZLINK_CORE_SOCKET_PUB || type == ZLINK_CORE_SOCKET_SUB
        || type == ZLINK_CORE_SOCKET_XSUB || type == ZLINK_CORE_SOCKET_XPUB) {
        errno = ENOTSUP;
        return -1;
    }

    return zlink::logical_multipart_send (handle_.socket, parts_, part_count_,
                                          flags_);
}

int publish_socket_parts (socket_handle_t handle_,
                          const char *topic_id_,
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
    if ((!parts_ && part_count_ > 0) || part_count_ == 0) {
        errno = EFAULT;
        return -1;
    }

    const int type = socket_type (handle_);
    if (type != ZLINK_CORE_SOCKET_PUB && type != ZLINK_CORE_SOCKET_XPUB) {
        errno = ENOTSUP;
        return -1;
    }
    return zlink::logical_multipart_publish (handle_.socket, topic_id_, parts_,
                                             part_count_, flags_);
}

int publish_socket_parts_blocking (socket_handle_t handle_,
                                   const char *topic_id_,
                                   zlink_msg_t *parts_,
                                   size_t part_count_,
                                   zlink_send_flags_t flags_)
{
    return zlink::logical_multipart_publish (handle_.socket, topic_id_, parts_,
                                             part_count_, flags_, true);
}
}

int zlink_send (void *s_,
                zlink_msg_t *parts_,
                size_t part_count_,
                zlink_send_flags_t flags_)
{
    if (!s_) {
        errno = EFAULT;
        return -1;
    }

    if (zlink::socket_base_t *socket = try_as_socket (s_)) {
        socket_handle_t handle;
        handle.socket = socket;
        return send_socket_parts (handle, NULL, parts_, part_count_, flags_);
    }

    const int service_rc =
      zlink_service_send_internal (s_, parts_, part_count_, flags_);
    if (service_rc == 0 || errno != EFAULT)
        return service_rc;

    errno = EFAULT;
    return -1;
}

int zlink_publish (void *subject_,
                   const char *topic_id_,
                   zlink_msg_t *parts_,
                   size_t part_count_,
                   zlink_send_flags_t flags_)
{
    if (!subject_) {
        errno = EFAULT;
        return -1;
    }

    if (zlink::socket_base_t *socket = try_as_socket (subject_)) {
        socket_handle_t handle;
        handle.socket = socket;
        if ((flags_ & ZLINK_DONTWAIT) != 0)
            return publish_socket_parts (handle, topic_id_, parts_, part_count_,
                                         flags_);
        return publish_socket_parts_blocking (handle, topic_id_, parts_,
                                              part_count_, flags_);
    }

    const int service_rc = zlink_service_publish_internal (
      subject_, topic_id_, parts_, part_count_, flags_);
    if (service_rc == 0 || errno != EFAULT)
        return service_rc;

    errno = EFAULT;
    return -1;
}

int zlink_send_rid (void *s_,
                    const zlink_routing_id_t *target_rid_,
                    zlink_msg_t *parts_,
                    size_t part_count_,
                    zlink_send_flags_t flags_)
{
    if (!s_) {
        errno = EFAULT;
        return -1;
    }

    if (zlink::socket_base_t *socket = try_as_socket (s_)) {
        socket_handle_t handle;
        handle.socket = socket;
        return send_socket_parts (handle, target_rid_, parts_, part_count_,
                                  flags_);
    }

    const int service_rc = zlink_service_send_rid_internal (
      s_, target_rid_, parts_, part_count_, flags_);
    if (service_rc == 0 || errno != EFAULT)
        return service_rc;

    errno = EFAULT;
    return -1;
}
