/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include <climits>
#include <string>
#include <string.h>
#include <stdlib.h>
#include <vector>

#include "api/poller_api_internal.hpp"
#include "api/service_api_internal.hpp"
#include "api/socket_api_internal.hpp"
#include "core/msg.hpp"
#include "core/multipart_send_txn.hpp"
#include "core/recv_internal.hpp"
#include "core/send_internal.hpp"
#include "utils/err.hpp"
#include "utils/likely.hpp"

namespace
{
bool frame_has_more (const zlink_msg_t &msg_)
{
    return (reinterpret_cast<const zlink::msg_t *> (&msg_)->flags ()
            & zlink::msg_t::more)
           != 0;
}

void discard_socket_parts (const zlink_routing_id_t *,
                           zlink_msg_t *parts_,
                           size_t part_count_,
                           void *)
{
    zlink_multipart_close (parts_, part_count_);
}

bool is_valid_pubsub_filter (const char *filter_,
                             std::string *raw_filter_out_,
                             bool *is_pattern_out_)
{
    if (!filter_ || filter_[0] == '\0')
        return false;

    const size_t len = strlen (filter_);
    if (len == 0 || len > 255)
        return false;

    const char *star = strchr (filter_, '*');
    if (!star) {
        if (raw_filter_out_)
            *raw_filter_out_ = std::string (filter_, len);
        if (is_pattern_out_)
            *is_pattern_out_ = false;
        return true;
    }

    if (star != filter_ + len - 1 || len < 2)
        return false;

    if (strchr (star + 1, '*'))
        return false;

    if (raw_filter_out_)
        *raw_filter_out_ = std::string (filter_, len - 1);
    if (is_pattern_out_)
        *is_pattern_out_ = true;
    return true;
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

    if (topic_id_out_ && topic_size_ > 0)
        memcpy (topic_id_out_, topic_data_, topic_size_);
    *topic_id_len_out_ = topic_size_;
    return 0;
}

void close_spot_parts (zlink_msg_t *parts_, size_t part_count_)
{
    if (!parts_)
        return;
    for (size_t i = 0; i < part_count_; ++i)
        zlink_msg_close (&parts_[i]);
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

    std::vector<zlink_msg_t> frames;
    zlink_msg_t first;
    zlink_msg_init (&first);
    if (zlink::recv_msg_internal (handle_.socket, &first, flags_) < 0) {
        zlink_msg_close (&first);
        return -1;
    }

    if (copy_topic_to_output (
          static_cast<const char *> (zlink_msg_data (&first)),
          zlink_msg_size (&first), topic_id_out_, topic_id_len_out_)
        != 0) {
        zlink_msg_close (&first);
        return -1;
    }

    if (!frame_has_more (first)) {
        zlink_msg_close (&first);
        errno = 0;
        return 0;
    }

    zlink_msg_t second;
    zlink_msg_init (&second);
    if (zlink::recv_msg_internal (handle_.socket, &second, 0) < 0) {
        zlink_msg_close (&second);
        zlink_msg_close (&first);
        return -1;
    }
    zlink_msg_close (&first);

    if (!frame_has_more (second))
        return move_single_part_to_output (&second, parts_out_, part_count_out_);

    frames.push_back (second);

    while (frame_has_more (frames.back ())) {
        zlink_msg_t frame;
        zlink_msg_init (&frame);
        if (zlink::recv_msg_internal (handle_.socket, &frame, 0) < 0) {
            zlink_msg_close (&frame);
            close_spot_parts (frames.data (), frames.size ());
            return -1;
        }
        frames.push_back (frame);
    }

    const size_t payload_count = frames.size ();
    if (payload_count == 0) {
        close_spot_parts (frames.data (), frames.size ());
        errno = 0;
        return 0;
    }

    zlink_msg_t *parts = static_cast<zlink_msg_t *> (
      malloc (payload_count * sizeof (zlink_msg_t)));
    if (!parts) {
        close_spot_parts (frames.data (), frames.size ());
        errno = ENOMEM;
        return -1;
    }
    for (size_t i = 0; i < payload_count; ++i) {
        if (relocate_msg_to_output (&frames[i], &parts[i]) != 0) {
            for (size_t j = 0; j < i; ++j)
                zlink_msg_close (&parts[j]);
            free (parts);
            close_spot_parts (frames.data (), frames.size ());
            errno = EFAULT;
            return -1;
        }
    }

    *parts_out_ = parts;
    *part_count_out_ = payload_count;
    errno = 0;
    return 0;
}

int subscribe_socket_filter (socket_handle_t handle_,
                             int option_,
                             const char *filter_)
{
    if (!handle_.socket) {
        errno = EFAULT;
        return -1;
    }

    const int type = socket_type (handle_);
    if (type != ZLINK_CORE_SOCKET_SUB && type != ZLINK_CORE_SOCKET_XSUB) {
        errno = ENOTSUP;
        return -1;
    }

    std::string raw_filter;
    bool is_pattern = false;
    if (!is_valid_pubsub_filter (filter_, &raw_filter, &is_pattern)
        || raw_filter.empty ()) {
        errno = EINVAL;
        return -1;
    }

    return handle_.socket->setsockopt (option_, raw_filter.data (),
                                       raw_filter.size ());
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
    if (type == ZLINK_CORE_SOCKET_PUB) {
        errno = ENOTSUP;
        return -1;
    }
    if (type == ZLINK_CORE_SOCKET_XPUB) {
        errno = ENOTSUP;
        return -1;
    }
    if (type == ZLINK_CORE_SOCKET_SUB || type == ZLINK_CORE_SOCKET_XSUB) {
        errno = ENOTSUP;
        return -1;
    }

    const bool routed_router_payload =
      (type == ZLINK_CORE_SOCKET_ROUTER && source_rid_out_ != NULL);
    const bool strip_recv_routing_id =
      (type == ZLINK_CORE_SOCKET_STREAM) || routed_router_payload;

    zlink_msg_t first;
    zlink_msg_init (&first);
    if (type == ZLINK_CORE_SOCKET_ROUTER && source_rid_out_) {
        if (zlink::recv_msg_routed_internal (
              handle_.socket, &first, source_rid_out_, flags_)
            < 0) {
            zlink_msg_close (&first);
            return -1;
        }
    } else {
        if (zlink::recv_msg_internal (handle_.socket, &first, flags_) < 0) {
            zlink_msg_close (&first);
            return -1;
        }
    }

    if (type == ZLINK_CORE_SOCKET_STREAM && source_rid_out_)
        handle_.socket->copy_last_recv_source_rid (source_rid_out_);

    std::vector<zlink_msg_t> frames;
    if (!frame_has_more (first)) {
        if (strip_recv_routing_id && !routed_router_payload) {
            zlink_msg_close (&first);
            errno = 0;
            return 0;
        }
        return move_single_part_to_output (&first, parts_out_, part_count_out_);
    }

    frames.push_back (first);
    while (frame_has_more (frames.back ())) {
        zlink_msg_t frame;
        zlink_msg_init (&frame);
        if (zlink::recv_msg_internal (handle_.socket, &frame, 0) < 0) {
            zlink_msg_close (&frame);
            close_spot_parts (frames.data (), frames.size ());
            return -1;
        }
        frames.push_back (frame);
    }

    size_t payload_offset = 0;
    size_t payload_count = frames.size ();
    if (strip_recv_routing_id) {
        if (frames.empty ()) {
            errno = EFAULT;
            return -1;
        }

        if (type == ZLINK_CORE_SOCKET_STREAM && source_rid_out_)
            copy_routing_id_frame (frames[0], source_rid_out_);

        payload_offset = routed_router_payload ? 0 : 1;
        payload_count = frames.size () > payload_offset
                          ? frames.size () - payload_offset
                          : 0;
        if (!routed_router_payload)
            zlink_msg_close (&frames[0]);
        if (payload_count == 0) {
            errno = 0;
            return 0;
        }
    }

    zlink_msg_t *parts =
      static_cast<zlink_msg_t *> (malloc (payload_count * sizeof (zlink_msg_t)));
    if (!parts) {
        close_spot_parts (frames.data (), frames.size ());
        errno = ENOMEM;
        return -1;
    }
    for (size_t i = 0; i < payload_count; ++i) {
        if (relocate_msg_to_output (&frames[i + payload_offset], &parts[i])
            != 0) {
            for (size_t j = 0; j < i; ++j)
                zlink_msg_close (&parts[j]);
            free (parts);
            close_spot_parts (frames.data (), frames.size ());
            errno = EFAULT;
            return -1;
        }
    }

    *parts_out_ = parts;
    *part_count_out_ = payload_count;
    errno = 0;
    return 0;
}

} // namespace

int zlink_recv_spot_handler (void *s_,
                             zlink_subscribe_handler_fn handler_,
                             void *userdata_);

int zlink_recv_handler (void *s_,
                        zlink_socket_msg_handler_fn handler_,
                        void *userdata_)
{
    if (!handler_) {
        errno = EINVAL;
        return -1;
    }

    const int service_rc =
      zlink_service_msg_recv_handler_internal (s_, handler_, userdata_);
    if (service_rc == 0 || errno != EFAULT)
        return service_rc;

    socket_handle_t handle = as_socket_handle (s_);
    if (!handle.socket)
        return -1;

    if (handler_ == &discard_socket_parts) {
        errno = EINVAL;
        return -1;
    }

    const int type = socket_type (handle);
    switch (type) {
        case ZLINK_CORE_SOCKET_PAIR:
        case ZLINK_CORE_SOCKET_DEALER:
        case ZLINK_CORE_SOCKET_ROUTER:
            return handle.socket->socket_set_msg_handler_with_userdata (
              handler_, NULL, userdata_);
        case ZLINK_CORE_SOCKET_SUB:
        case ZLINK_CORE_SOCKET_XSUB:
        case ZLINK_CORE_SOCKET_PUB:
        case ZLINK_CORE_SOCKET_XPUB:
            errno = ENOTSUP;
            return -1;
        case ZLINK_CORE_SOCKET_STREAM:
            return handle.socket->stream_set_msg_handler_with_userdata (
              handler_, userdata_);
        default:
            errno = ENOTSUP;
            return -1;
    }
}

int zlink_subscribe_handler (void *s_,
                             zlink_subscribe_handler_fn handler_,
                             void *userdata_)
{
    return zlink_recv_spot_handler (s_, handler_, userdata_);
}

int zlink_recv_spot_handler (void *s_,
                             zlink_subscribe_handler_fn handler_,
                             void *userdata_)
{
    if (!handler_) {
        errno = EINVAL;
        return -1;
    }
    const int service_rc =
      zlink_service_recv_handler_internal (s_, handler_, userdata_);
    if (service_rc == 0 || errno != EFAULT)
        return service_rc;

    socket_handle_t handle = as_socket_handle (s_);
    if (!handle.socket)
        return -1;

    const int type = socket_type (handle);
    switch (type) {
        case ZLINK_CORE_SOCKET_SUB:
        case ZLINK_CORE_SOCKET_XSUB:
            return handle.socket->socket_set_spot_handler_with_userdata (
              handler_, userdata_);
        default:
            errno = ENOTSUP;
            return -1;
    }
}

static int socket_send_ready_handler_internal (
  void *s_,
  zlink_send_ready_handler_fn handler_,
  void *userdata_)
{
    socket_handle_t handle = as_socket_handle (s_);
    if (!handle.socket)
        return -1;

    if (!handler_) {
        errno = EINVAL;
        return -1;
    }

    const int type = socket_type (handle);
    switch (type) {
        case ZLINK_CORE_SOCKET_PAIR:
        case ZLINK_CORE_SOCKET_PUB:
        case ZLINK_CORE_SOCKET_XPUB:
        case ZLINK_CORE_SOCKET_DEALER:
        case ZLINK_CORE_SOCKET_ROUTER:
        case ZLINK_CORE_SOCKET_STREAM:
            break;
        default:
            errno = ENOTSUP;
            return -1;
    }

    return handle.socket->socket_set_send_ready_handler_with_userdata (
      handler_, NULL, userdata_);
}

int zlink_send_ready_handler (void *s_,
                              zlink_send_ready_handler_fn handler_,
                              void *userdata_)
{
    if (!handler_) {
        errno = EINVAL;
        return -1;
    }

    const int service_rc =
      zlink_service_send_ready_handler_internal (s_, handler_, userdata_);
    if (service_rc == 0 || errno != EFAULT)
        return service_rc;

    return socket_send_ready_handler_internal (s_, handler_, userdata_);
}

int validate_socket_callback_poller_events (socket_handle_t handle_,
                                            short events_)
{
    if (!handle_.socket)
        return 0;
    const int type = socket_type (handle_);
    if ((events_ & ZLINK_POLLIN) != 0) {
        if (handle_.socket->socket_msg_dispatch_active ()
            || ((type == ZLINK_CORE_SOCKET_SUB
                 || type == ZLINK_CORE_SOCKET_XSUB)
                && handle_.socket->sub_dispatch_active ())
            || (type == ZLINK_CORE_SOCKET_STREAM
                && handle_.socket->stream_dispatch_active ())) {
            errno = EBUSY;
            return -1;
        }
    }
    if ((events_ & ZLINK_POLLOUT) != 0
        && handle_.socket->send_ready_handler_active ()) {
        errno = EBUSY;
        return -1;
    }
    return 0;
}

int zlink_xpub_recv (void *s_,
                     zlink_routing_id_t *source_rid_out_,
                     int *subscribed_out_,
                     char *topic_id_out_,
                     size_t *topic_id_len_,
                     zlink_send_flags_t flags_)
{
    socket_handle_t handle = as_socket_handle (s_);
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

int zlink_recv (void *s_,
                zlink_routing_id_t *source_rid_out_,
                zlink_msg_t **parts_out_,
                size_t *part_count_out_,
                zlink_send_flags_t flags_)
{
    if (!s_) {
        errno = EFAULT;
        return -1;
    }
    if (zlink::socket_base_t *socket = try_as_socket (s_)) {
        socket_handle_t handle;
        handle.socket = socket;
        return recv_socket_parts (handle, source_rid_out_, parts_out_,
                                  part_count_out_, flags_);
    }

    const int service_rc = zlink_service_recv_internal (
      s_, source_rid_out_, parts_out_, part_count_out_, flags_);
    if (service_rc == 0 || errno != EFAULT)
        return service_rc;

    errno = EFAULT;
    return -1;
}

int zlink_subscribe (void *subject_,
                     zlink_routing_id_t *source_rid_out_,
                     zlink_msg_t **parts_out_,
                     size_t *part_count_out_,
                     char *topic_id_out_,
                     size_t *topic_id_len_out_,
                     zlink_send_flags_t flags_)
{
    if (!subject_) {
        errno = EFAULT;
        return -1;
    }

    if (zlink::socket_base_t *socket = try_as_socket (subject_)) {
        socket_handle_t handle;
        handle.socket = socket;
        return recv_socket_subscribe_parts (handle, source_rid_out_, parts_out_,
                                            part_count_out_, topic_id_out_,
                                            topic_id_len_out_, flags_);
    }

    const int service_rc = zlink_service_subscribe_recv_internal (
      subject_, source_rid_out_, parts_out_, part_count_out_, topic_id_out_,
      topic_id_len_out_, flags_);
    if (service_rc == 0 || errno != EFAULT)
        return service_rc;

    errno = EFAULT;
    return -1;
}

int zlink_subscription_event (void *subject_,
                              zlink_routing_id_t *source_rid_out_,
                              int *subscribed_out_,
                              char *topic_id_out_,
                              size_t *topic_id_len_out_,
                              zlink_send_flags_t flags_)
{
    return zlink_xpub_recv (subject_, source_rid_out_, subscribed_out_,
                            topic_id_out_, topic_id_len_out_, flags_);
}
