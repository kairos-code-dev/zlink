/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include <algorithm>

#include "api/request_reply_protocol_internal.hpp"
#include "api/service_api_internal.hpp"
#include "api/service_spot_request_reply_internal.hpp"
#include "core/ctx.hpp"
#include "core/recv_internal.hpp"
#include "core/recv_tls_view.hpp"
#include "services/spot/spot_node_access.hpp"

namespace
{
using zlink::spot_reqrep_internal::g_spot_recv_source_rid;
using zlink::spot_reqrep_internal::g_spot_recv_spot_rid;
using zlink::spot_reqrep_internal::maybe_dispatch_spot_info;
using zlink::spot_reqrep_internal::queued_spot_subscribe_message_t;
using zlink::spot_reqrep_internal::spot_request_reply_state_t;
using zlink::spot_reqrep_internal::spot_subscribe_dispatch_queue_t;

zlink::ctx_t *resolve_spot_ctx (void *spot_)
{
    spot_handle_t *spot = as_spot_handle (spot_);
    if (!spot || !spot->node) {
        errno = EFAULT;
        return NULL;
    }
    return zlink::spot_node_access_t::ctx (spot->node);
}

bool has_valid_routing_id (const zlink_routing_id_t *peer_rid_)
{
    return peer_rid_ && peer_rid_->size > 0
           && peer_rid_->size <= sizeof (peer_rid_->data);
}

int copy_topic_to_output_local (const char *topic_data_,
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
    errno = 0;
    return 0;
}

int copy_routing_id_frame_local (const zlink_msg_t &frame_,
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
        memcpy (source_rid_out_->data,
                zlink_msg_data (&const_cast<zlink_msg_t &> (frame_)),
                routing_id_copy);
    }
    return 0;
}
}

void zlink::spot_reqrep_internal::close_spot_dispatch_parts (
  zlink_msg_t *parts_,
  size_t part_count_)
{
    if (!parts_)
        return;
    for (size_t i = 0; i < part_count_; ++i)
        zlink_msg_close (&parts_[i]);
}

int zlink::spot_reqrep_internal::queue_spot_message (
  spot_request_reply_state_t *state_,
  const zlink_routing_id_t *source_rid_,
  const zlink_routing_id_t *spot_rid_,
  uint64_t request_seq_,
  zlink_msg_t *parts_,
  size_t part_count_)
{
    if (!state_ || !parts_) {
        errno = EFAULT;
        return -1;
    }

    if (zlink::internal_pair_queue::ensure (resolve_spot_ctx (state_->owner),
                                            "zlink.spot.routed.recv",
                                            &state_->recv_queue)
        != 0)
        return -1;

    unsigned char seq_buf[8];
    zlink::request_reply::encode_u64_be (request_seq_, seq_buf);
    const void *source_data =
      has_valid_routing_id (source_rid_) ? source_rid_->data : NULL;
    const size_t source_size =
      has_valid_routing_id (source_rid_) ? source_rid_->size : 0;
    const void *spot_data =
      has_valid_routing_id (spot_rid_) ? spot_rid_->data : NULL;
    const size_t spot_size =
      has_valid_routing_id (spot_rid_) ? spot_rid_->size : 0;
    if (zlink::internal_pair_queue::send_buffer_frame (
          state_->recv_queue.tx, source_data, source_size, ZLINK_SNDMORE)
        != 0
        || zlink::internal_pair_queue::send_buffer_frame (
             state_->recv_queue.tx, spot_data, spot_size, ZLINK_SNDMORE)
             != 0
        || zlink::internal_pair_queue::send_buffer_frame (
             state_->recv_queue.tx, seq_buf, sizeof (seq_buf), ZLINK_SNDMORE)
             != 0) {
        zlink::request_reply::consume_send_frames_from (parts_, 0, part_count_);
        return -1;
    }
    for (size_t i = 0; i < part_count_; ++i) {
        const int flags = (i + 1 < part_count_) ? ZLINK_SNDMORE : 0;
        if (state_->recv_queue.tx->send (
              reinterpret_cast<zlink::msg_t *> (&parts_[i]), flags)
            != 0) {
            zlink::request_reply::consume_send_frames_from (parts_, i,
                                                            part_count_);
            return -1;
        }
    }

    maybe_dispatch_spot_info (state_, ZLINK_SPOT_DISPATCH_EVENT_ROUTED_READABLE,
                              ZLINK_SPOT_DISPATCH_SUBJECT_SPOT,
                              state_->owner);
    return 0;
}

int zlink::spot_reqrep_internal::queue_spot_subscribe_message (
  spot_request_reply_state_t *state_,
  const zlink_routing_id_t *source_rid_,
  const char *topic_,
  size_t topic_len_,
  zlink_msg_t *parts_,
  size_t part_count_)
{
    if (!state_ || !topic_ || (!parts_ && part_count_ > 0)) {
        close_spot_dispatch_parts (parts_, part_count_);
        errno = EFAULT;
        return -1;
    }

    queued_spot_subscribe_message_t message;
    if (has_valid_routing_id (source_rid_))
        message.source_rid = *source_rid_;
    message.topic.assign (topic_, topic_len_);
    message.parts.reserve (part_count_);

    for (size_t i = 0; i < part_count_; ++i) {
        zlink_msg_t part;
        zlink_msg_init (&part);
        if (zlink_msg_move (&part, &parts_[i]) != 0) {
            zlink_msg_close (&part);
            close_spot_dispatch_parts (&parts_[i], part_count_ - i);
            return -1;
        }
        message.parts.push_back (part);
    }

    {
        std::lock_guard<std::mutex> lock (state_->subscribe_queue.mutex);
        if (state_->subscribe_queue.closed) {
            errno = ETERM;
            return -1;
        }
        state_->subscribe_queue.messages.push_back (std::move (message));
    }
    state_->subscribe_queue.cv.notify_one ();

    maybe_dispatch_spot_info (state_,
                              ZLINK_SPOT_DISPATCH_EVENT_SUBSCRIBE_READABLE,
                              ZLINK_SPOT_DISPATCH_SUBJECT_SPOT,
                              state_->owner);
    return 0;
}

void zlink::spot_reqrep_internal::close_spot_subscribe_dispatch_queue (
  spot_subscribe_dispatch_queue_t *queue_)
{
    if (!queue_)
        return;

    {
        std::lock_guard<std::mutex> lock (queue_->mutex);
        queue_->closed = true;
        queue_->messages.clear ();
    }
    queue_->cv.notify_all ();
}

int zlink::spot_reqrep_internal::recv_internal_spot_queue (
  zlink::internal_pair_queue::queue_t *queue_,
  const zlink_routing_id_t **source_rid_out_,
  const zlink_routing_id_t **spot_rid_out_,
  uint64_t *request_seq_out_,
  zlink_msg_t **parts_out_,
  size_t *part_count_out_,
  int flags_)
{
    if (!queue_ || !queue_->rx || !source_rid_out_ || !spot_rid_out_
        || !request_seq_out_ || !parts_out_ || !part_count_out_) {
        errno = EFAULT;
        return -1;
    }

    zlink_msg_t source_frame;
    zlink_msg_t spot_frame;
    zlink_msg_t seq_frame;
    zlink_msg_t *first_payload = NULL;
    zlink_msg_init (&source_frame);
    zlink_msg_init (&spot_frame);
    zlink_msg_init (&seq_frame);

    if (zlink::recv_tls_view::begin_with_first_slot (
          parts_out_, part_count_out_, &first_payload)
        != 0)
        return -1;

    while (queue_->rx->recv (reinterpret_cast<zlink::msg_t *> (&source_frame),
                             flags_)
           != 0) {
        const int saved_errno = errno;
        zlink_msg_close (&source_frame);
        if ((flags_ & ZLINK_DONTWAIT) != 0 || saved_errno != EAGAIN) {
            zlink::recv_tls_view::abort ();
            errno = saved_errno;
            return -1;
        }
        if (zlink::wait_socket_events_internal (queue_->rx, ZLINK_POLLIN, -1)
            <= 0) {
            zlink::recv_tls_view::abort ();
            errno = saved_errno;
            return -1;
        }
        zlink_msg_init (&source_frame);
    }
    if (zlink::internal_pair_queue::recv_followup_with_retry (
          queue_->rx, &spot_frame, flags_)
        != 0) {
        const int saved_errno = errno;
        zlink_msg_close (&source_frame);
        zlink_msg_close (&spot_frame);
        zlink_msg_close (&seq_frame);
        zlink::recv_tls_view::abort ();
        errno = saved_errno;
        return -1;
    }
    if (zlink::internal_pair_queue::recv_followup_with_retry (
          queue_->rx, &seq_frame, flags_)
        != 0) {
        const int saved_errno = errno;
        zlink_msg_close (&source_frame);
        zlink_msg_close (&spot_frame);
        zlink_msg_close (&seq_frame);
        zlink::recv_tls_view::abort ();
        errno = saved_errno;
        return -1;
    }
    if (zlink_msg_size (&seq_frame) != 8) {
        zlink_msg_close (&source_frame);
        zlink_msg_close (&spot_frame);
        zlink_msg_close (&seq_frame);
        zlink::recv_tls_view::abort ();
        errno = EPROTO;
        return -1;
    }
    if (zlink::internal_pair_queue::recv_followup_with_retry (
          queue_->rx, first_payload, flags_)
        != 0) {
        const int saved_errno = errno;
        zlink_msg_close (&source_frame);
        zlink_msg_close (&spot_frame);
        zlink_msg_close (&seq_frame);
        zlink::recv_tls_view::abort ();
        errno = saved_errno;
        return -1;
    }

    g_spot_recv_source_rid.size =
      static_cast<uint8_t> (std::min (zlink_msg_size (&source_frame),
                                      sizeof (g_spot_recv_source_rid.data)));
    if (g_spot_recv_source_rid.size > 0) {
        memcpy (g_spot_recv_source_rid.data, zlink_msg_data (&source_frame),
                g_spot_recv_source_rid.size);
    }
    g_spot_recv_spot_rid.size =
      static_cast<uint8_t> (std::min (zlink_msg_size (&spot_frame),
                                      sizeof (g_spot_recv_spot_rid.data)));
    if (g_spot_recv_spot_rid.size > 0) {
        memcpy (g_spot_recv_spot_rid.data, zlink_msg_data (&spot_frame),
                g_spot_recv_spot_rid.size);
    }
    *source_rid_out_ = &g_spot_recv_source_rid;
    *spot_rid_out_ = g_spot_recv_spot_rid.size > 0 ? &g_spot_recv_spot_rid
                                                    : NULL;
    *request_seq_out_ = zlink::request_reply::decode_u64_be (
      static_cast<const unsigned char *> (zlink_msg_data (&seq_frame)));
    zlink_msg_close (&source_frame);
    zlink_msg_close (&spot_frame);
    zlink_msg_close (&seq_frame);

    if (!zlink::internal_pair_queue::frame_has_more (*first_payload))
        return zlink::recv_tls_view::commit_reserved_single (parts_out_,
                                                             part_count_out_);
    return zlink::internal_pair_queue::export_followup_sequence_from_reserved_first (
      queue_->rx, parts_out_, part_count_out_);
}

int zlink::spot_reqrep_internal::recv_internal_spot_subscribe_queue (
  spot_subscribe_dispatch_queue_t *queue_,
  zlink_routing_id_t *source_rid_out_,
  zlink_msg_t **parts_out_,
  size_t *part_count_out_,
  char *topic_id_out_,
  size_t *topic_id_len_out_,
  int flags_)
{
    if (!queue_ || !parts_out_ || !part_count_out_ || !topic_id_len_out_) {
        errno = EFAULT;
        return -1;
    }

    if (source_rid_out_)
        memset (source_rid_out_, 0, sizeof (*source_rid_out_));

    queued_spot_subscribe_message_t message;
    {
        std::unique_lock<std::mutex> lock (queue_->mutex);
        while (queue_->messages.empty ()) {
            if ((flags_ & ZLINK_DONTWAIT) != 0) {
                errno = EAGAIN;
                return -1;
            }
            if (queue_->closed) {
                errno = ETERM;
                return -1;
            }
            queue_->cv.wait (lock);
        }
        message = std::move (queue_->messages.front ());
        queue_->messages.pop_front ();
    }

    if (source_rid_out_)
        *source_rid_out_ = message.source_rid;

    if (copy_topic_to_output_local (message.topic.data (),
                                    message.topic.size (),
                                    topic_id_out_,
                                    topic_id_len_out_)
        != 0) {
        return -1;
    }

    if (zlink::recv_tls_view::begin (parts_out_, part_count_out_) != 0)
        return -1;

    for (size_t i = 0; i < message.parts.size (); ++i) {
        if (zlink::recv_tls_view::push (&message.parts[i]) != 0) {
            zlink::recv_tls_view::abort ();
            return -1;
        }
    }

    return zlink::recv_tls_view::commit (parts_out_, part_count_out_);
}
