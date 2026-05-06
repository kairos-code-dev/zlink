/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include <algorithm>

#include "api/request_reply_protocol_internal.hpp"
#include "api/service_spot_request_reply_internal.hpp"
#include "api/service_spot_request_reply_utils_internal.hpp"
#include "core/c_api_copy_internal.hpp"
#include "core/multipart_send_txn.hpp"
#include "core/recv_internal.hpp"
#include "core/recv_tls_view.hpp"
#include "services/spot/spot_handle.hpp"
#include "services/spot/spot_node_access.hpp"
#include "services/spot/spot_runtime.hpp"
#include "services/spot/spot_subject_access.hpp"

namespace
{
using zlink::spot_reqrep_internal::g_spot_recv_source_rid;
using zlink::spot_reqrep_internal::g_spot_recv_spot_rid;
using zlink::spot_reqrep_internal::has_valid_routing_id;
using zlink::spot_reqrep_internal::maybe_dispatch_spot_info;
using zlink::spot_reqrep_internal::queued_spot_subscribe_message_t;
using zlink::spot_reqrep_internal::resolve_spot_runtime;
using zlink::spot_reqrep_internal::spot_request_reply_state_t;
using zlink::spot_reqrep_internal::spot_subscribe_dispatch_queue_t;

int init_frame_from_buffer_local (zlink_msg_t *part_,
                                  const void *data_,
                                  size_t size_)
{
    if (!part_) {
        errno = EFAULT;
        return -1;
    }

    zlink_msg_init (part_);
    if (zlink_msg_init_size (part_, size_) != 0)
        return -1;
    if (size_ > 0)
        memcpy (zlink_msg_data (part_), data_, size_);
    return 0;
}

void close_routed_serialized_parts_local (zlink_msg_t *parts_, size_t count_)
{
    if (!parts_)
        return;
    for (size_t i = 0; i < count_; ++i)
        zlink_msg_close (&parts_[i]);
}

bool reserve_routed_queue_slot_local (
  const std::shared_ptr<zlink::spot_reqrep_internal::spot_request_reply_state_t> &state_)
{
    if (!state_) {
        errno = EFAULT;
        return false;
    }

    std::lock_guard<std::mutex> lock (state_->mutex);
    if (!state_->recv.routed_recv_queue.signaler.valid ()) {
        errno = ETERM;
        return false;
    }
    ++state_->recv.routed_recv_queue.pending_count;
    return true;
}

void release_routed_queue_slot_local (
  const std::shared_ptr<zlink::spot_reqrep_internal::spot_request_reply_state_t> &state_)
{
    if (!state_)
        return;

    std::lock_guard<std::mutex> lock (state_->mutex);
    if (state_->recv.routed_recv_queue.pending_count > 0)
        --state_->recv.routed_recv_queue.pending_count;
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

    zlink::spot_runtime_t *runtime = resolve_spot_runtime (state_->owner);
    if (!runtime || !runtime->execution.data_plane_running) {
        errno = ETERM;
        return -1;
    }

    std::shared_ptr<spot_request_reply_state_t> state =
      zlink::spot_reqrep_internal::try_find_spot_state (state_->owner);
    if (!state || zlink::spot_reqrep_internal::ensure_spot_recv_ready (state) != 0) {
        zlink::request_reply::consume_send_frames_from (parts_, 0, part_count_);
        return -1;
    }

    if (!reserve_routed_queue_slot_local (state)) {
        const int saved_errno = errno;
        errno = saved_errno;
        zlink::request_reply::consume_send_frames_from (parts_, 0, part_count_);
        return -1;
    }
    zlink::spot_reqrep_internal::queued_routed_message_t message;
    if (has_valid_routing_id (source_rid_))
        message.source_rid = *source_rid_;
    if (has_valid_routing_id (spot_rid_))
        message.spot_rid = *spot_rid_;
    message.request_seq = request_seq_;
    message.parts.reserve (part_count_);
    for (size_t i = 0; i < part_count_; ++i) {
        zlink_msg_t part;
        zlink_msg_init (&part);
        if (zlink_msg_move (&part, &parts_[i]) != 0) {
            zlink_msg_close (&part);
            zlink::request_reply::consume_send_frames_from (
              parts_, i, part_count_);
            release_routed_queue_slot_local (state);
            return -1;
        }
        message.parts.push_back (part);
    }

    bool should_signal = false;
    {
        std::lock_guard<std::mutex> lock (state->recv.routed_recv_queue.mutex);
        state->recv.routed_recv_queue.pending.push_back (std::move (message));
        if (!state->recv.routed_recv_queue.signal_armed) {
            state->recv.routed_recv_queue.signal_armed = true;
            should_signal = true;
        }
    }
    if (should_signal)
        state->recv.routed_recv_queue.signaler.send ();

    maybe_dispatch_spot_info (state.get (),
                              ZLINK_SPOT_DISPATCH_EVENT_ROUTED_READABLE,
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
        std::lock_guard<std::mutex> lock (state_->recv.subscribe_queue.mutex);
        if (state_->recv.subscribe_queue.closed) {
            errno = ETERM;
            return -1;
        }
        state_->recv.subscribe_queue.messages.push_back (std::move (message));
    }
    state_->recv.subscribe_queue.cv.notify_one ();

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
  spot_request_reply_state_t *state_,
  const zlink_routing_id_t **source_rid_out_,
  const zlink_routing_id_t **spot_rid_out_,
  uint64_t *request_seq_out_,
  zlink_msg_t **parts_out_,
  size_t *part_count_out_,
  int flags_)
{
    if (!state_ || !source_rid_out_ || !spot_rid_out_
        || !request_seq_out_ || !parts_out_ || !part_count_out_) {
        errno = EFAULT;
        return -1;
    }
    std::shared_ptr<spot_request_reply_state_t> state =
      zlink::spot_reqrep_internal::try_find_spot_state (state_->owner);
    if ((flags_ & ZLINK_DONTWAIT) == 0) {
        bool input_ready = false;
        bool signal_ready = false;
        const int wait_rc = zlink::spot_reqrep_internal::spot_routed_recv_wait (
          state, -1, &input_ready, &signal_ready);
        LIBZLINK_UNUSED (signal_ready);
        if (wait_rc <= 0) {
            if (wait_rc == 0)
                errno = EAGAIN;
            return -1;
        }
        if (!input_ready) {
            errno = EAGAIN;
            return -1;
        }
    }

    zlink::spot_reqrep_internal::queued_routed_message_t message;
    bool drain_signal = false;
    bool resignal = false;
    {
        std::lock_guard<std::mutex> lock (state_->recv.routed_recv_queue.mutex);
        if (state_->recv.routed_recv_queue.pending.empty ()) {
            errno = EAGAIN;
            return -1;
        }
        message = std::move (state_->recv.routed_recv_queue.pending.front ());
        state_->recv.routed_recv_queue.pending.pop_front ();
        drain_signal = state_->recv.routed_recv_queue.signal_armed;
        if (state_->recv.routed_recv_queue.pending.empty ()) {
            state_->recv.routed_recv_queue.signal_armed = false;
        } else if (state_->recv.routed_recv_queue.signal_armed) {
            resignal = true;
        }
    }
    if (drain_signal
        && state_->recv.routed_recv_queue.signaler.recv_failable () != 0) {
        errno = 0;
    }
    if (resignal)
        state_->recv.routed_recv_queue.signaler.send ();

    if (zlink::recv_tls_view::begin (parts_out_, part_count_out_) != 0) {
        release_routed_queue_slot_local (state);
        return -1;
    }

    for (size_t i = 0; i < message.parts.size (); ++i) {
        if (zlink::recv_tls_view::push (&message.parts[i]) != 0) {
            zlink::recv_tls_view::abort ();
            release_routed_queue_slot_local (state);
            return -1;
        }
    }

    g_spot_recv_source_rid = message.source_rid;
    g_spot_recv_spot_rid = message.spot_rid;

    *request_seq_out_ = message.request_seq;
    release_routed_queue_slot_local (state);
    *source_rid_out_ = &g_spot_recv_source_rid;
    *spot_rid_out_ = g_spot_recv_spot_rid.size > 0 ? &g_spot_recv_spot_rid
                                                    : NULL;
    return zlink::recv_tls_view::commit (parts_out_, part_count_out_);
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

    if (zlink::copy_bytes_to_sized_output (
          message.topic.data (), message.topic.size (), topic_id_out_,
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
