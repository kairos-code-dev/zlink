/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include <string.h>
#include <stdlib.h>

#include "api/service_api_internal.hpp"
#include "api/socket_api_internal.hpp"
#include "api/socket_message_api_internal.hpp"
#include "core/msg.hpp"
#include "core/recv_internal.hpp"
#include "core/recv_tls_view.hpp"

namespace
{
bool is_direct_public_recv_fast_type (int type_)
{
    return type_ == ZLINK_CORE_SOCKET_PAIR || type_ == ZLINK_CORE_SOCKET_DEALER;
}

bool is_direct_public_routed_recv_fast_type (int type_)
{
    return false;
}

bool frame_has_more (const zlink_msg_t &msg_)
{
    return (reinterpret_cast<const zlink::msg_t *> (&msg_)->flags ()
            & zlink::msg_t::more)
           != 0;
}

void close_recv_parts (zlink_msg_t *parts_, size_t part_count_)
{
    if (!parts_)
        return;
    for (size_t i = 0; i < part_count_; ++i)
        zlink_msg_close (&parts_[i]);
}

int copy_topic_to_output (const char *topic_data_,
                          size_t topic_size_,
                          char *topic_id_out_,
                          size_t *topic_id_len_out_)
{
    if (!topic_id_len_out_) {
        errno = EFAULT;
        return -1;
    }

    if (!topic_id_out_) {
        *topic_id_len_out_ = topic_size_;
        errno = 0;
        return 0;
    }

    if (*topic_id_len_out_ < topic_size_) {
        *topic_id_len_out_ = topic_size_;
        errno = EMSGSIZE;
        return -1;
    }

    if (topic_size_ > 0)
        memcpy (topic_id_out_, topic_data_, topic_size_);
    *topic_id_len_out_ = topic_size_;
    return 0;
}

int copy_routing_id_frame (const zlink_msg_t &frame_,
                           zlink_routing_id_t *source_rid_out_)
{
    if (!source_rid_out_)
        return 0;

    source_rid_out_->size = zlink_msg_size (&frame_);
    source_rid_out_->data = source_rid_out_->size == 0
                              ? NULL
                              : static_cast<uint8_t *> (
                                  zlink_msg_data (&const_cast<zlink_msg_t &> (
                                    frame_)));
    return 0;
}

int drain_followup_frames (void *socket_, zlink_msg_t *frame_)
{
    if (!socket_ || !frame_) {
        errno = EFAULT;
        return -1;
    }

    zlink::socket_base_t *socket =
      static_cast<zlink::socket_base_t *> (socket_);
    if (!socket || !socket->check_tag ()) {
        errno = EFAULT;
        return -1;
    }

    bool more = frame_has_more (*frame_);
    zlink_msg_close (frame_);
    while (more) {
        zlink_msg_t next;
        zlink_msg_init (&next);
        if (zlink::recv_followup_msg_socket (socket, &next) < 0) {
            zlink_msg_close (&next);
            return -1;
        }
        more = frame_has_more (next);
        zlink_msg_close (&next);
    }

    errno = 0;
    return 0;
}

int export_payload_sequence (void *socket_,
                             zlink_msg_t *first_payload_,
                             zlink_msg_t **parts_out_,
                             size_t *part_count_out_)
{
    if (!socket_ || !first_payload_ || !parts_out_ || !part_count_out_) {
        errno = EFAULT;
        return -1;
    }

    zlink::socket_base_t *socket =
      static_cast<zlink::socket_base_t *> (socket_);
    if (!socket || !socket->check_tag ()) {
        errno = EFAULT;
        return -1;
    }

    if (!frame_has_more (*first_payload_))
        return zlink::recv_tls_view::export_single (first_payload_, parts_out_,
                                                    part_count_out_);

    zlink_msg_t current;
    zlink_msg_init (&current);
    if (zlink_msg_move (&current, first_payload_) != 0) {
        zlink_msg_close (&current);
        errno = EFAULT;
        return -1;
    }

    while (true) {
        const bool more = frame_has_more (current);
        if (zlink::recv_tls_view::push (&current) != 0) {
            const int saved_errno = errno;
            (void) drain_followup_frames (socket_, &current);
            zlink::recv_tls_view::abort ();
            errno = saved_errno;
            return -1;
        }

        if (!more)
            return zlink::recv_tls_view::commit (parts_out_, part_count_out_);

        zlink_msg_t next;
        zlink_msg_init (&next);
        if (zlink::recv_followup_msg_socket (socket, &next) < 0) {
            zlink_msg_close (&next);
            zlink::recv_tls_view::abort ();
            return -1;
        }
        if (zlink_msg_move (&current, &next) != 0) {
            zlink_msg_close (&next);
            zlink::recv_tls_view::abort ();
            errno = EFAULT;
            return -1;
        }
    }
}

int export_followup_sequence_from_reserved_first (void *socket_,
                                                  zlink_msg_t **parts_out_,
                                                  size_t *part_count_out_)
{
    if (!socket_ || !parts_out_ || !part_count_out_) {
        errno = EFAULT;
        return -1;
    }

    if (zlink::recv_tls_view::reserve_first_slot () != 0)
        return -1;

    while (true) {
        zlink::recv_tls_view::storage_t &tls = zlink::recv_tls_view::storage ();
        const bool more = frame_has_more (tls.parts[tls.count - 1]);
        if (!more)
            return zlink::recv_tls_view::commit (parts_out_, part_count_out_);

        zlink_msg_t next;
        zlink_msg_init (&next);
        if (zlink::recv_followup_msg_socket (
              static_cast<zlink::socket_base_t *> (socket_), &next)
            < 0) {
            zlink_msg_close (&next);
            zlink::recv_tls_view::abort ();
            return -1;
        }

        if (zlink::recv_tls_view::push (&next) != 0) {
            const int saved_errno = errno;
            (void) drain_followup_frames (socket_, &next);
            zlink::recv_tls_view::abort ();
            errno = saved_errno;
            return -1;
        }
    }
}

int recv_socket_subscribe_parts (socket_handle_t handle_,
                                 zlink_routing_id_t *source_rid_out_,
                                 zlink_msg_t **parts_out_,
                                 size_t *part_count_out_,
                                 char *topic_id_out_,
                                 size_t *topic_id_len_out_,
                                 zlink_send_flags_t flags_)
{
    if (!handle_.socket) {
        errno = EFAULT;
        return -1;
    }
    if (!parts_out_ || !part_count_out_ || !topic_id_len_out_) {
        errno = EFAULT;
        return -1;
    }
    if (validate_recv_flags (flags_) != 0)
        return -1;

    zlink_msg_t *first_slot = NULL;
    if (zlink::recv_tls_view::begin_with_first_slot (
          parts_out_, part_count_out_, &first_slot)
        != 0)
        return -1;
    if (source_rid_out_)
        memset (source_rid_out_, 0, sizeof (*source_rid_out_));

    const int type = socket_type (handle_);
    if (type != ZLINK_CORE_SOCKET_SUB && type != ZLINK_CORE_SOCKET_XSUB) {
        errno = ENOTSUP;
        return -1;
    }

    zlink_msg_t topic_frame;
    zlink_msg_init (&topic_frame);
    if (handle_.socket->sub_dispatch_active ()) {
        errno = EBUSY;
        return -1;
    }

    if (handle_.socket->recv (
          reinterpret_cast<zlink::msg_t *> (&topic_frame), flags_)
        < 0) {
        zlink_msg_close (&topic_frame);
        return -1;
    }

    if (copy_topic_to_output (
          static_cast<const char *> (zlink_msg_data (&topic_frame)),
          zlink_msg_size (&topic_frame), topic_id_out_, topic_id_len_out_)
        != 0) {
        zlink_msg_close (&topic_frame);
        return -1;
    }

    if (!frame_has_more (topic_frame)) {
        zlink_msg_close (&topic_frame);
        errno = 0;
        return 0;
    }

    if (zlink::recv_followup_msg_socket (handle_.socket, first_slot) < 0) {
        zlink_msg_close (&topic_frame);
        zlink::recv_tls_view::abort ();
        return -1;
    }
    zlink_msg_close (&topic_frame);

    if (!frame_has_more (*first_slot))
        return zlink::recv_tls_view::commit_reserved_single (parts_out_,
                                                             part_count_out_);

    return export_followup_sequence_from_reserved_first (handle_.socket,
                                                         parts_out_,
                                                         part_count_out_);
}

int recv_socket_parts (socket_handle_t handle_,
                       zlink_routing_id_t *source_rid_out_,
                       zlink_msg_t **parts_out_,
                       size_t *part_count_out_,
                       zlink_send_flags_t flags_)
{
    // Hot path: PAIR/DEALER single-part public recv reaches here on every
    // message in with_zmq single. Keep single-part export lean and do not
    // accidentally fold it back into a heavier multipart path.
    if (!handle_.socket) {
        errno = EFAULT;
        return -1;
    }
    if (!parts_out_ || !part_count_out_) {
        errno = EFAULT;
        return -1;
    }
    if (validate_recv_flags (flags_) != 0)
        return -1;

    const int type = socket_type (handle_);
    if (type == ZLINK_CORE_SOCKET_PUB || type == ZLINK_CORE_SOCKET_XPUB
        || type == ZLINK_CORE_SOCKET_SUB || type == ZLINK_CORE_SOCKET_XSUB) {
        errno = ENOTSUP;
        return -1;
    }
    if (type == ZLINK_CORE_SOCKET_ROUTER) {
        errno = EOPNOTSUPP;
        return -1;
    }

    const bool routed_router_payload =
      type == ZLINK_CORE_SOCKET_ROUTER && source_rid_out_ != NULL;
    const bool strip_recv_routing_id =
      type == ZLINK_CORE_SOCKET_STREAM || routed_router_payload;
    const bool direct_public_recv_fast =
      !strip_recv_routing_id && !source_rid_out_
      && is_direct_public_recv_fast_type (type);
    const bool direct_public_routed_recv_fast =
      routed_router_payload
      && is_direct_public_routed_recv_fast_type (type);

    if (direct_public_recv_fast) {
        zlink_msg_t *first_slot = NULL;
        if (zlink::recv_tls_view::begin_with_first_slot (
              parts_out_, part_count_out_, &first_slot)
            != 0)
            return -1;

        if (handle_.socket->socket_msg_dispatch_active ()) {
            errno = EBUSY;
            return -1;
        }

        if (handle_.socket->recv (
              reinterpret_cast<zlink::msg_t *> (first_slot), flags_)
            < 0)
            return -1;

        if (!frame_has_more (*first_slot))
            return zlink::recv_tls_view::commit_reserved_single (
              parts_out_, part_count_out_);

        return export_followup_sequence_from_reserved_first (
          handle_.socket, parts_out_, part_count_out_);
    }

    if (direct_public_routed_recv_fast) {
        zlink_msg_t *first_slot = NULL;
        if (zlink::recv_tls_view::begin_with_first_slot (
              parts_out_, part_count_out_, &first_slot)
            != 0)
            return -1;

        memset (source_rid_out_, 0, sizeof (*source_rid_out_));
        if (handle_.socket->socket_msg_dispatch_active ()) {
            errno = EBUSY;
            return -1;
        }

        if (handle_.socket->recv_routed (
              reinterpret_cast<zlink::msg_t *> (first_slot), source_rid_out_,
              flags_)
            < 0)
            return -1;

        if (!frame_has_more (*first_slot))
            return zlink::recv_tls_view::commit_reserved_single (
              parts_out_, part_count_out_);

        return export_followup_sequence_from_reserved_first (
          handle_.socket, parts_out_, part_count_out_);
    }

    if (zlink::recv_tls_view::begin (parts_out_, part_count_out_) != 0)
        return -1;
    if (source_rid_out_)
        memset (source_rid_out_, 0, sizeof (*source_rid_out_));

    zlink_msg_t first;
    zlink_msg_init (&first);
    if (type == ZLINK_CORE_SOCKET_ROUTER && source_rid_out_) {
        if (zlink::recv_msg_routed_socket (
              handle_.socket, &first, source_rid_out_, flags_)
            < 0) {
            zlink_msg_close (&first);
            return -1;
        }
    } else if (zlink::recv_msg_socket (handle_.socket, type, &first, flags_) < 0) {
        zlink_msg_close (&first);
        return -1;
    }

    if (type == ZLINK_CORE_SOCKET_STREAM && source_rid_out_)
        handle_.socket->copy_last_recv_source_rid (source_rid_out_);

    if (!frame_has_more (first)) {
        if (strip_recv_routing_id && !routed_router_payload) {
            zlink_msg_close (&first);
            errno = 0;
            return 0;
        }
        return zlink::recv_tls_view::export_single (&first, parts_out_,
                                                    part_count_out_);
    }

    if (strip_recv_routing_id) {
        if (type == ZLINK_CORE_SOCKET_STREAM && source_rid_out_)
            copy_routing_id_frame (first, source_rid_out_);

        if (!routed_router_payload) {
            zlink_msg_close (&first);

            zlink_msg_t payload;
            zlink_msg_init (&payload);
            if (zlink::recv_followup_msg_socket (handle_.socket, &payload) < 0) {
                zlink_msg_close (&payload);
                return -1;
            }
            return export_payload_sequence (handle_.socket, &payload, parts_out_,
                                            part_count_out_);
        }
    }

    return export_payload_sequence (handle_.socket, &first, parts_out_,
                                    part_count_out_);
}

} // namespace

int zlink_socket_xpub_recv_internal (void *socket_,
                                     zlink_routing_id_t *source_rid_out_,
                                     int *subscribed_out_,
                                     char *topic_id_out_,
                                     size_t *topic_id_len_,
                                     zlink_send_flags_t flags_)
{
    socket_handle_t handle = as_socket_handle (socket_);
    if (!handle.socket)
        return -1;
    if (validate_recv_flags (flags_) != 0)
        return -1;
    if (!subscribed_out_ || !topic_id_len_) {
        errno = EFAULT;
        return -1;
    }
    if (!topic_id_out_ && *topic_id_len_ != 0) {
        errno = EFAULT;
        return -1;
    }
    if (socket_type (handle) != ZLINK_CORE_SOCKET_XPUB) {
        errno = EINVAL;
        return -1;
    }

    zlink_msg_t msg;
    zlink_msg_init (&msg);
    if (zlink::recv_msg_socket (
          handle.socket, ZLINK_CORE_SOCKET_XPUB, &msg, flags_)
        < 0) {
        zlink_msg_close (&msg);
        return -1;
    }

    if (source_rid_out_) {
        memset (source_rid_out_, 0, sizeof (*source_rid_out_));
        handle.socket->copy_last_recv_source_rid (source_rid_out_);
    }

    const unsigned char *data =
      static_cast<const unsigned char *> (zlink_msg_data (&msg));
    const size_t size = zlink_msg_size (&msg);
    const size_t topic_len = size > 0 ? size - 1 : 0;
    *subscribed_out_ = size > 0 && data[0] != 0 ? 1 : 0;

    if (*topic_id_len_ < topic_len) {
        *topic_id_len_ = topic_len;
        zlink_msg_close (&msg);
        errno = EMSGSIZE;
        return -1;
    }

    if (topic_id_out_ && topic_len > 0)
        memcpy (topic_id_out_, data + 1, topic_len);
    *topic_id_len_ = topic_len;

    zlink_msg_close (&msg);
    errno = 0;
    return 0;
}

int zlink_socket_recv_internal (void *socket_,
                                zlink_routing_id_t *source_rid_out_,
                                zlink_msg_t **parts_out_,
                                size_t *part_count_out_,
                                zlink_send_flags_t flags_)
{
    socket_handle_t handle = as_socket_handle (socket_);
    if (!handle.socket)
        return -1;
    return recv_socket_parts (handle, source_rid_out_, parts_out_,
                              part_count_out_, flags_);
}

int zlink_socket_subscribe_recv_internal (void *socket_,
                                          zlink_routing_id_t *source_rid_out_,
                                          zlink_msg_t **parts_out_,
                                          size_t *part_count_out_,
                                          char *topic_id_out_,
                                          size_t *topic_id_len_out_,
                                          zlink_send_flags_t flags_)
{
    socket_handle_t handle = as_socket_handle (socket_);
    if (!handle.socket)
        return -1;
    return recv_socket_subscribe_parts (handle, source_rid_out_, parts_out_,
                                        part_count_out_, topic_id_out_,
                                        topic_id_len_out_, flags_);
}
