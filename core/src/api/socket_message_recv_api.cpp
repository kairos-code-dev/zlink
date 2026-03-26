/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include <vector>
#include <string.h>
#include <stdlib.h>

#include "api/service_api_internal.hpp"
#include "api/socket_api_internal.hpp"
#include "api/socket_message_api_internal.hpp"
#include "core/msg.hpp"
#include "core/recv_internal.hpp"

namespace
{
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

    const size_t routing_id_size = zlink_msg_size (&frame_);
    const size_t routing_id_copy =
      routing_id_size > sizeof (source_rid_out_->data)
        ? sizeof (source_rid_out_->data)
        : routing_id_size;
    source_rid_out_->size = static_cast<uint8_t> (routing_id_copy);
    if (routing_id_copy > 0) {
        memcpy (
          source_rid_out_->data,
          zlink_msg_data (&const_cast<zlink_msg_t &> (frame_)),
          routing_id_copy);
    }
    return 0;
}

int relocate_msg_to_output (zlink_msg_t *src_, zlink_msg_t *dst_)
{
    if (!src_ || !dst_) {
        errno = EFAULT;
        return -1;
    }

    zlink::msg_t *src = reinterpret_cast<zlink::msg_t *> (src_);
    if (!src->check ()) {
        errno = EFAULT;
        return -1;
    }

    *reinterpret_cast<zlink::msg_t *> (dst_) = *src;
    if (src->init () != 0) {
        zlink_msg_close (dst_);
        errno = EFAULT;
        return -1;
    }

    return 0;
}

int move_single_part_to_output (zlink_msg_t *src_,
                                zlink_msg_t **parts_out_,
                                size_t *part_count_out_)
{
    if (!src_ || !parts_out_ || !part_count_out_) {
        errno = EFAULT;
        return -1;
    }

    zlink_msg_t *parts =
      static_cast<zlink_msg_t *> (malloc (sizeof (zlink_msg_t)));
    if (!parts) {
        errno = ENOMEM;
        return -1;
    }
    if (relocate_msg_to_output (src_, parts) != 0) {
        free (parts);
        return -1;
    }

    *parts_out_ = parts;
    *part_count_out_ = 1;
    errno = 0;
    return 0;
}

int collect_recv_frames (socket_handle_t handle_,
                         zlink_msg_t *first_frame_,
                         std::vector<zlink_msg_t> *frames_out_)
{
    if (!handle_.socket || !first_frame_ || !frames_out_) {
        errno = EFAULT;
        return -1;
    }

    frames_out_->clear ();
    frames_out_->push_back (*first_frame_);

    while (frame_has_more (frames_out_->back ())) {
        zlink_msg_t frame;
        zlink_msg_init (&frame);
        if (zlink::recv_msg_internal (handle_.socket, &frame, 0) < 0) {
            zlink_msg_close (&frame);
            close_recv_parts (frames_out_->data (), frames_out_->size ());
            frames_out_->clear ();
            return -1;
        }
        frames_out_->push_back (frame);
    }

    return 0;
}

int export_recv_frames (std::vector<zlink_msg_t> *frames_,
                        size_t payload_offset_,
                        zlink_msg_t **parts_out_,
                        size_t *part_count_out_)
{
    if (!frames_ || !parts_out_ || !part_count_out_) {
        errno = EFAULT;
        return -1;
    }

    const size_t payload_count =
      frames_->size () > payload_offset_ ? frames_->size () - payload_offset_ : 0;
    if (payload_count == 0) {
        *parts_out_ = NULL;
        *part_count_out_ = 0;
        errno = 0;
        return 0;
    }

    zlink_msg_t *parts = static_cast<zlink_msg_t *> (
      malloc (payload_count * sizeof (zlink_msg_t)));
    if (!parts) {
        close_recv_parts (frames_->data (), frames_->size ());
        frames_->clear ();
        errno = ENOMEM;
        return -1;
    }

    for (size_t i = 0; i < payload_count; ++i) {
        if (relocate_msg_to_output (&(*frames_)[i + payload_offset_], &parts[i])
            != 0) {
            for (size_t j = 0; j < i; ++j)
                zlink_msg_close (&parts[j]);
            free (parts);
            close_recv_parts (frames_->data (), frames_->size ());
            frames_->clear ();
            errno = EFAULT;
            return -1;
        }
    }

    *parts_out_ = parts;
    *part_count_out_ = payload_count;
    errno = 0;
    return 0;
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

    *parts_out_ = NULL;
    *part_count_out_ = 0;
    if (source_rid_out_)
        memset (source_rid_out_, 0, sizeof (*source_rid_out_));

    const int type = socket_type (handle_);
    if (type != ZLINK_CORE_SOCKET_SUB && type != ZLINK_CORE_SOCKET_XSUB) {
        errno = ENOTSUP;
        return -1;
    }

    zlink_msg_t topic_frame;
    zlink_msg_init (&topic_frame);
    if (zlink::recv_msg_internal (handle_.socket, &topic_frame, flags_) < 0) {
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

    zlink_msg_t payload_frame;
    zlink_msg_init (&payload_frame);
    if (zlink::recv_msg_internal (handle_.socket, &payload_frame, 0) < 0) {
        zlink_msg_close (&payload_frame);
        zlink_msg_close (&topic_frame);
        return -1;
    }
    zlink_msg_close (&topic_frame);

    if (!frame_has_more (payload_frame))
        return move_single_part_to_output (&payload_frame, parts_out_,
                                           part_count_out_);

    std::vector<zlink_msg_t> frames;
    if (collect_recv_frames (handle_, &payload_frame, &frames) != 0)
        return -1;

    return export_recv_frames (&frames, 0, parts_out_, part_count_out_);
}

int recv_socket_parts (socket_handle_t handle_,
                       zlink_routing_id_t *source_rid_out_,
                       zlink_msg_t **parts_out_,
                       size_t *part_count_out_,
                       zlink_send_flags_t flags_)
{
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

    *parts_out_ = NULL;
    *part_count_out_ = 0;
    if (source_rid_out_)
        memset (source_rid_out_, 0, sizeof (*source_rid_out_));

    const int type = socket_type (handle_);
    if (type == ZLINK_CORE_SOCKET_PUB || type == ZLINK_CORE_SOCKET_XPUB
        || type == ZLINK_CORE_SOCKET_SUB || type == ZLINK_CORE_SOCKET_XSUB) {
        errno = ENOTSUP;
        return -1;
    }

    const bool routed_router_payload =
      type == ZLINK_CORE_SOCKET_ROUTER && source_rid_out_ != NULL;
    const bool strip_recv_routing_id =
      type == ZLINK_CORE_SOCKET_STREAM || routed_router_payload;

    zlink_msg_t first;
    zlink_msg_init (&first);
    if (type == ZLINK_CORE_SOCKET_ROUTER && source_rid_out_) {
        if (zlink::recv_msg_routed_internal (
              handle_.socket, &first, source_rid_out_, flags_)
            < 0) {
            zlink_msg_close (&first);
            return -1;
        }
    } else if (zlink::recv_msg_internal (handle_.socket, &first, flags_) < 0) {
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
        return move_single_part_to_output (&first, parts_out_, part_count_out_);
    }

    std::vector<zlink_msg_t> frames;
    if (collect_recv_frames (handle_, &first, &frames) != 0)
        return -1;

    size_t payload_offset = 0;
    if (strip_recv_routing_id) {
        if (frames.empty ()) {
            errno = EFAULT;
            return -1;
        }

        if (type == ZLINK_CORE_SOCKET_STREAM && source_rid_out_)
            copy_routing_id_frame (frames[0], source_rid_out_);

        payload_offset = routed_router_payload ? 0 : 1;
        if (!routed_router_payload)
            zlink_msg_close (&frames[0]);
    }

    return export_recv_frames (&frames, payload_offset, parts_out_,
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
    if (zlink::recv_msg_internal (handle.socket, &msg, flags_) < 0) {
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
