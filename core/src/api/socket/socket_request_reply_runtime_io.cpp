/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include <limits>
#include <string>
#include <vector>

#include "api/socket/request_reply_protocol_internal.hpp"
#include "api/socket/socket_request_reply_internal.hpp"
#include "api/socket/socket_request_reply_runtime_io_helpers.hpp"
#include "api/socket/socket_request_reply_router_state_internal.hpp"
#include "core/c_api_copy_internal.hpp"
#include "core/multipart_send_txn.hpp"
#include "core/recv_internal.hpp"
#include "core/recv_tls_view.hpp"
#include "sockets/common/socket_base.hpp"
#include "utils/routing_id.hpp"

namespace zlink
{
namespace socket_reqrep_internal
{
const size_t stack_request_reply_part_capacity = 8;

int queue_router_message (socket_request_reply_state_t *state_,
                          const zlink_routing_id_t *source_node_rid_,
                          uint64_t request_seq_,
                          zlink_msg_t *parts_,
                          size_t part_count_)
{
    if (!state_ || !parts_) {
        errno = EFAULT;
        return -1;
    }

    if (zlink::internal_pair_queue::ensure (state_->socket ? state_->socket->get_ctx () : NULL,
                                            "zlink.router.reqrep.recv", &state_->recv_queue)
        != 0)
        return -1;

    unsigned char seq_buf[8];
    zlink::request_reply::encode_u64_be (request_seq_, seq_buf);
    const routing_id_frame_view_t source_node = routing_id_frame_view (source_node_rid_);
    zlink::socket_base_t *queue_tx = state_->recv_queue.tx_socket ();
    if (zlink::internal_pair_queue::send_buffer_frame (queue_tx, source_node.data, source_node.size,
                                                       ZLINK_SNDMORE)
          != 0
        || zlink::internal_pair_queue::send_buffer_frame (queue_tx, seq_buf, sizeof (seq_buf),
                                                          ZLINK_SNDMORE)
             != 0) {
        zlink::request_reply::consume_send_frames_from (parts_, 0, part_count_);
        return -1;
    }

    for (size_t i = 0; i < part_count_; ++i) {
        const int flags = (i + 1 < part_count_) ? ZLINK_SNDMORE : 0;
        if (queue_tx->send (reinterpret_cast<zlink::msg_t *> (&parts_[i]), flags) != 0) {
            zlink::request_reply::consume_send_frames_from (parts_, i, part_count_);
            return -1;
        }
    }
    return 0;
}

uint64_t allocate_dealer_reply_token (socket_request_reply_state_t *state_)
{
    if (!state_)
        return 0;

    for (uint64_t i = 0; i < std::numeric_limits<uint64_t>::max (); ++i) {
        uint64_t token = state_->dealer_next_reply_token++;
        if (state_->dealer_next_reply_token == 0)
            state_->dealer_next_reply_token = 1;
        if (token != 0 && state_->dealer_reply_targets.count (token) == 0)
            return token;
    }
    errno = EAGAIN;
    return 0;
}

int queue_dealer_message (socket_request_reply_state_t *state_,
                          uint8_t message_type_,
                          uint64_t request_seq_,
                          zlink::pipe_t *source_pipe_,
                          zlink_msg_t *parts_,
                          size_t part_count_)
{
    if (!state_ || !parts_) {
        errno = EFAULT;
        return -1;
    }

    uint64_t exported_request_seq = request_seq_;
    bool stored_reply_target = false;
    if (message_type_ == ZLINK_DEALER_MESSAGE_REQUEST) {
        if (!source_pipe_ || request_seq_ == 0) {
            errno = EPROTO;
            return -1;
        }
        std::lock_guard<std::mutex> lock (state_->mutex);
        exported_request_seq = allocate_dealer_reply_token (state_);
        if (exported_request_seq == 0)
            return -1;
        dealer_reply_target_t target;
        target.pipe = source_pipe_;
        target.request_seq = request_seq_;
        state_->dealer_reply_targets[exported_request_seq] = target;
        stored_reply_target = true;
    }

    if (zlink::internal_pair_queue::ensure (state_->socket ? state_->socket->get_ctx () : NULL,
                                            "zlink.dealer.reqrep.recv", &state_->recv_queue)
        != 0) {
        if (stored_reply_target) {
            std::lock_guard<std::mutex> lock (state_->mutex);
            state_->dealer_reply_targets.erase (exported_request_seq);
        }
        return -1;
    }

    unsigned char type_buf[1] = {message_type_};
    unsigned char seq_buf[8];
    zlink::request_reply::encode_u64_be (exported_request_seq, seq_buf);
    zlink::socket_base_t *queue_tx = state_->recv_queue.tx_socket ();
    if (zlink::internal_pair_queue::send_buffer_frame (queue_tx, type_buf, sizeof (type_buf),
                                                       ZLINK_SNDMORE)
          != 0
        || zlink::internal_pair_queue::send_buffer_frame (queue_tx, seq_buf, sizeof (seq_buf),
                                                          ZLINK_SNDMORE)
             != 0) {
        if (stored_reply_target) {
            std::lock_guard<std::mutex> lock (state_->mutex);
            state_->dealer_reply_targets.erase (exported_request_seq);
        }
        zlink::request_reply::consume_send_frames_from (parts_, 0, part_count_);
        return -1;
    }

    for (size_t i = 0; i < part_count_; ++i) {
        const int flags = (i + 1 < part_count_) ? ZLINK_SNDMORE : 0;
        if (queue_tx->send (reinterpret_cast<zlink::msg_t *> (&parts_[i]), flags) != 0) {
            if (stored_reply_target) {
                std::lock_guard<std::mutex> lock (state_->mutex);
                state_->dealer_reply_targets.erase (exported_request_seq);
            }
            zlink::request_reply::consume_send_frames_from (parts_, i, part_count_);
            return -1;
        }
    }
    return 0;
}

int dispatch_router_message (socket_request_reply_state_t *state_,
                             const zlink_routing_id_t *source_node_rid_,
                             uint64_t request_seq_,
                             zlink_msg_t *parts_,
                             size_t part_count_)
{
    if (!state_ || !parts_) {
        errno = EFAULT;
        return -1;
    }
    return queue_router_message (state_, source_node_rid_, request_seq_, parts_, part_count_);
}

int dispatch_dealer_message (socket_request_reply_state_t *state_,
                             uint8_t message_type_,
                             uint64_t request_seq_,
                             zlink::pipe_t *source_pipe_,
                             zlink_msg_t *parts_,
                             size_t part_count_)
{
    if (!state_ || !parts_) {
        errno = EFAULT;
        return -1;
    }
    return queue_dealer_message (state_, message_type_, request_seq_, source_pipe_, parts_,
                                 part_count_);
}

int validate_request_parts (zlink_msg_t *parts_, size_t part_count_)
{
    if ((!parts_ && part_count_ > 0) || part_count_ == 0) {
        errno = EFAULT;
        return -1;
    }

    return 0;
}

int recv_internal_router_queue (zlink::internal_pair_queue::queue_t *queue_,
                                const zlink_routing_id_t **source_node_rid_out_,
                                uint64_t *request_seq_out_,
                                zlink_msg_t **parts_out_,
                                size_t *part_count_out_,
                                int flags_,
                                int timeout_ms_)
{
    if (!queue_ || !queue_->rx_socket () || !source_node_rid_out_ || !request_seq_out_
        || !parts_out_ || !part_count_out_) {
        errno = EFAULT;
        return -1;
    }

    router_control_frames_t control_frames;
    zlink_msg_t *first_payload = NULL;

    if (zlink::recv_tls_view::begin_with_first_slot (parts_out_, part_count_out_, &first_payload)
        != 0)
        return -1;

    zlink::clock_t clock;
    const uint64_t deadline_ms =
      timeout_ms_ > 0 ? clock.now_ms () + static_cast<uint64_t> (timeout_ms_) : 0;
    zlink::socket_base_t *queue_rx = queue_->rx_socket ();

    if (recv_internal_queue_frame (queue_rx, &control_frames.source_node, flags_, timeout_ms_,
                                   deadline_ms)
        != 0)
        return control_frames.fail (errno);
    if (zlink::internal_pair_queue::recv_followup_with_retry (queue_rx, &control_frames.seq, flags_)
        != 0)
        return control_frames.fail (errno);
    if (zlink_msg_size (&control_frames.seq) != 8)
        return control_frames.fail (EPROTO);
    if (zlink::internal_pair_queue::recv_followup_with_retry (queue_rx, first_payload, flags_) != 0)
        return control_frames.fail (errno);

    router_recv_metadata_tls_t &metadata = router_recv_metadata_tls ();
    zlink::copy_routing_id_from_msg (control_frames.source_node, &metadata.source_rid);
    *source_node_rid_out_ = &metadata.source_rid;
    *request_seq_out_ = zlink::request_reply::decode_u64_be (
      static_cast<const unsigned char *> (zlink_msg_data (&control_frames.seq)));
    control_frames.close ();

    if (!zlink::msg_frame_has_more (*first_payload))
        return zlink::recv_tls_view::commit_reserved_single (parts_out_, part_count_out_);

    if (zlink::recv_tls_view::reserve_first_slot () != 0) {
        zlink::recv_tls_view::abort ();
        errno = EMSGSIZE;
        return -1;
    }
    return zlink::export_reserved_followup_msg_sequence (queue_rx, parts_out_, part_count_out_,
                                                         true);
}

int recv_internal_dealer_queue (zlink::internal_pair_queue::queue_t *queue_,
                                uint8_t *message_type_out_,
                                uint64_t *request_seq_out_,
                                zlink_msg_t **parts_out_,
                                size_t *part_count_out_,
                                int flags_,
                                int timeout_ms_)
{
    if (!queue_ || !queue_->rx_socket () || !message_type_out_ || !request_seq_out_ || !parts_out_
        || !part_count_out_) {
        errno = EFAULT;
        return -1;
    }

    zlink_msg_t type_frame;
    zlink_msg_t seq_frame;
    zlink_msg_init (&type_frame);
    zlink_msg_init (&seq_frame);
    zlink_msg_t *first_payload = NULL;

    if (zlink::recv_tls_view::begin_with_first_slot (parts_out_, part_count_out_, &first_payload)
        != 0)
        return -1;

    zlink::clock_t clock;
    const uint64_t deadline_ms =
      timeout_ms_ > 0 ? clock.now_ms () + static_cast<uint64_t> (timeout_ms_) : 0;
    zlink::socket_base_t *queue_rx = queue_->rx_socket ();

    if (recv_internal_queue_frame (queue_rx, &type_frame, flags_, timeout_ms_, deadline_ms) != 0) {
        const int saved_errno = errno;
        zlink_msg_close (&type_frame);
        zlink_msg_close (&seq_frame);
        zlink::recv_tls_view::abort ();
        errno = saved_errno;
        return -1;
    }
    if (zlink::internal_pair_queue::recv_followup_with_retry (queue_rx, &seq_frame, flags_) != 0
        || zlink_msg_size (&type_frame) != 1 || zlink_msg_size (&seq_frame) != 8
        || zlink::internal_pair_queue::recv_followup_with_retry (queue_rx, first_payload, flags_)
             != 0) {
        const int saved_errno = errno != 0 ? errno : EPROTO;
        zlink_msg_close (&type_frame);
        zlink_msg_close (&seq_frame);
        zlink::recv_tls_view::abort ();
        errno = saved_errno;
        return -1;
    }

    *message_type_out_ = static_cast<const unsigned char *> (zlink_msg_data (&type_frame))[0];
    *request_seq_out_ = zlink::request_reply::decode_u64_be (
      static_cast<const unsigned char *> (zlink_msg_data (&seq_frame)));
    zlink_msg_close (&type_frame);
    zlink_msg_close (&seq_frame);

    if (!zlink::msg_frame_has_more (*first_payload))
        return zlink::recv_tls_view::commit_reserved_single (parts_out_, part_count_out_);

    if (zlink::recv_tls_view::reserve_first_slot () != 0) {
        zlink::recv_tls_view::abort ();
        errno = EMSGSIZE;
        return -1;
    }
    return zlink::export_reserved_followup_msg_sequence (queue_rx, parts_out_, part_count_out_,
                                                         true);
}

int take_dealer_reply_target (const std::shared_ptr<socket_request_reply_state_t> &state_,
                              uint64_t request_token_,
                              dealer_reply_target_t *target_out_)
{
    if (!state_ || request_token_ == 0 || !target_out_) {
        errno = EFAULT;
        return -1;
    }

    std::lock_guard<std::mutex> lock (state_->mutex);
    std::unordered_map<uint64_t, dealer_reply_target_t>::iterator it =
      state_->dealer_reply_targets.find (request_token_);
    if (it == state_->dealer_reply_targets.end ()) {
        errno = ENOENT;
        return -1;
    }

    *target_out_ = it->second;
    state_->dealer_reply_targets.erase (it);
    return 0;
}

void restore_dealer_reply_target (const std::shared_ptr<socket_request_reply_state_t> &state_,
                                  uint64_t request_token_,
                                  const dealer_reply_target_t &target_)
{
    if (!state_ || request_token_ == 0)
        return;

    std::lock_guard<std::mutex> lock (state_->mutex);
    state_->dealer_reply_targets[request_token_] = target_;
}

int export_router_payload_parts (zlink_msg_t *parts_,
                                 size_t part_count_,
                                 size_t start_index_,
                                 zlink_msg_t **parts_out_,
                                 size_t *part_count_out_)
{
    if (start_index_ >= part_count_) {
        errno = EPROTO;
        return -1;
    }

    for (size_t i = start_index_; i < part_count_; ++i) {
        if (zlink::recv_tls_view::push (&parts_[i]) != 0) {
            const int saved_errno = errno;
            zlink::recv_tls_view::abort ();
            for (size_t j = i; j < part_count_; ++j)
                zlink_msg_close (&parts_[j]);
            errno = saved_errno;
            return -1;
        }
    }

    return zlink::recv_tls_view::commit (parts_out_, part_count_out_);
}

int recv_router_message_direct (socket_handle_t handle_,
                                const zlink_routing_id_t **source_node_rid_out_,
                                uint64_t *request_seq_out_,
                                zlink_msg_t **parts_out_,
                                size_t *part_count_out_,
                                int flags_)
{
    if (!handle_.socket || !source_node_rid_out_ || !request_seq_out_ || !parts_out_
        || !part_count_out_) {
        errno = EFAULT;
        return -1;
    }

    zlink::msg_t current;
    const int current_init_rc = current.init ();
    errno_assert (current_init_rc == 0);
    zlink_routing_id_t source_rid;
    memset (&source_rid, 0, sizeof (source_rid));
    if (handle_.socket->recv_routed (&current, &source_rid, flags_) != 0) {
        return -1;
    }

    if ((current.flags () & zlink::msg_t::more) == 0) {
        zlink_msg_t *first_slot = NULL;
        if (zlink::recv_tls_view::begin_with_first_slot (parts_out_, part_count_out_, &first_slot)
            != 0) {
            const int saved_errno = errno;
            const int close_rc = current.close ();
            errno_assert (close_rc == 0);
            errno = saved_errno;
            return -1;
        }

        if (reinterpret_cast<zlink::msg_t *> (first_slot)->move (current) != 0) {
            const int saved_errno = errno;
            const int close_rc = current.close ();
            errno_assert (close_rc == 0);
            zlink::recv_tls_view::abort ();
            errno = saved_errno != 0 ? saved_errno : EFAULT;
            return -1;
        }

        router_recv_metadata_tls_t &metadata = router_recv_metadata_tls ();
        assign_routing_id_compact (&metadata.source_rid, source_rid);
        *source_node_rid_out_ = &metadata.source_rid;
        *request_seq_out_ = 0;
        return zlink::recv_tls_view::commit_reserved_single (parts_out_, part_count_out_);
    }

    std::vector<zlink_msg_t> raw_parts;
    raw_parts.reserve (stack_request_reply_part_capacity);
    while (true) {
        raw_parts.push_back (zlink_msg_t ());
        zlink_msg_init (&raw_parts.back ());
        if (reinterpret_cast<zlink::msg_t *> (&raw_parts.back ())->move (current) != 0) {
            zlink::close_msg_frames (&raw_parts);
            const int close_rc = current.close ();
            errno_assert (close_rc == 0);
            errno = EFAULT;
            return -1;
        }

        if (!router_raw_part_has_more (&raw_parts.back ()))
            break;

        zlink_msg_t next;
        zlink_msg_init (&next);
        if (recv_router_followup_frame (handle_.socket, &next) != 0) {
            zlink_msg_close (&next);
            zlink::close_msg_frames (&raw_parts);
            return -1;
        }
        if (current.move (*reinterpret_cast<zlink::msg_t *> (&next)) != 0) {
            zlink_msg_close (&next);
            zlink::close_msg_frames (&raw_parts);
            errno = EFAULT;
            return -1;
        }
    }

    if (zlink::recv_tls_view::begin (parts_out_, part_count_out_) != 0) {
        zlink::close_msg_frames (&raw_parts);
        return -1;
    }

    zlink::request_reply::parsed_envelope_t envelope;
    const bool parsed =
      zlink::request_reply::parse_envelope (raw_parts.data (), raw_parts.size (), &envelope);
    const size_t start_index = parsed ? zlink::request_reply::control_part_count : 0;

    if (parsed) {
        *request_seq_out_ = envelope.request_seq;
        for (size_t i = 0; i < start_index; ++i)
            zlink_msg_close (&raw_parts[i]);
    } else
        *request_seq_out_ = 0;

    router_recv_metadata_tls_t &metadata = router_recv_metadata_tls ();
    metadata.source_rid = source_rid;
    *source_node_rid_out_ = &metadata.source_rid;
    return export_router_payload_parts (raw_parts.data (), raw_parts.size (), start_index,
                                        parts_out_, part_count_out_);
}

int send_request_reply_message (void *socket_handle_,
                                const zlink_routing_id_t *peer_rid_,
                                zlink_msg_t *parts_,
                                size_t part_count_,
                                zlink_send_flags_t flags_,
                                uint8_t message_type_,
                                uint64_t request_seq_)
{
    if (!socket_handle_ || !parts_ || part_count_ == 0 || request_seq_ == 0) {
        errno = EINVAL;
        return -1;
    }

    const socket_handle_t handle = as_socket_handle (socket_handle_);
    if (!handle.socket) {
        errno = EFAULT;
        return -1;
    }

    const bool routed = zlink::valid_routing_id (peer_rid_);
    const size_t total_part_count = zlink::request_reply::control_part_count + part_count_;
    zlink_msg_t stack_combined[stack_request_reply_part_capacity];
    std::vector<zlink_msg_t> heap_combined;
    zlink_msg_t *combined =
      total_part_count <= stack_request_reply_part_capacity ? stack_combined : NULL;
    if (!combined) {
        heap_combined.resize (total_part_count);
        combined = &heap_combined[0];
    }
    for (size_t i = 0; i < total_part_count; ++i)
        zlink_msg_init (&combined[i]);

    if (zlink::request_reply::init_envelope_control_parts (combined, message_type_, request_seq_)
        != 0) {
        const int saved_errno = errno;
        zlink::request_reply::close_built_parts (combined, total_part_count);
        zlink::request_reply::consume_send_frames_from (parts_, 0, part_count_);
        errno = saved_errno;
        return -1;
    }

    for (size_t i = 0; i < part_count_; ++i) {
        if (zlink_msg_move (&combined[zlink::request_reply::control_part_count + i], &parts_[i])
            != 0) {
            const int saved_errno = errno;
            zlink::request_reply::close_built_parts (combined, total_part_count);
            zlink::request_reply::consume_send_frames_from (parts_, i, part_count_);
            errno = saved_errno;
            return -1;
        }
    }

    router_mandatory_scope_t mandatory_scope;
    if (routed && mandatory_scope.arm (handle) != 0) {
        const int saved_errno = errno;
        zlink::request_reply::close_built_parts (combined, total_part_count);
        errno = saved_errno;
        return -1;
    }

    const int rc =
      routed ? zlink::logical_multipart_send_routed (handle.socket, peer_rid_, combined,
                                                     total_part_count, flags_)
             : zlink::logical_multipart_send (handle.socket, combined, total_part_count, flags_);
    if (rc != 0)
        return -1;

    errno = 0;
    return 0;
}
}
}
