/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include <limits>
#include <string>
#include <vector>

#include "api/request_reply_protocol_internal.hpp"
#include "api/socket_request_reply_internal.hpp"
#include "core/c_api_copy_internal.hpp"
#include "core/multipart_send_txn.hpp"
#include "core/recv_internal.hpp"
#include "core/recv_tls_view.hpp"
#include "sockets/socket_base.hpp"

namespace zlink
{
namespace socket_reqrep_internal
{
namespace
{
void assign_routing_id_compact (zlink_routing_id_t *dest_,
                                const zlink_routing_id_t &src_)
{
    if (!dest_)
        return;

    zlink::copy_routing_id_from_bytes (src_.data, src_.size, dest_);
}

struct router_mandatory_scope_t
{
    router_mandatory_scope_t () :
        socket (NULL),
        restore_required (false),
        original_value (0)
    {
    }

    ~router_mandatory_scope_t ()
    {
        restore ();
    }

    int arm (socket_handle_t handle_)
    {
        if (!handle_.socket || socket_type (handle_) != ZLINK_CORE_SOCKET_ROUTER)
            return 0;

        size_t size = sizeof (original_value);
        if (handle_.socket->getsockopt (ZLINK_INTERNAL_OPT_ROUTER_MANDATORY,
                                        &original_value, &size)
            != 0)
            return -1;

        if (original_value != 0)
            return 0;

        const int mandatory = 1;
        if (handle_.socket->setsockopt (ZLINK_INTERNAL_OPT_ROUTER_MANDATORY,
                                        &mandatory, sizeof (mandatory))
            != 0)
            return -1;

        socket = handle_.socket;
        restore_required = true;
        return 0;
    }

    void restore ()
    {
        if (!restore_required || !socket)
            return;

        (void) socket->setsockopt (ZLINK_INTERNAL_OPT_ROUTER_MANDATORY,
                                   &original_value, sizeof (original_value));
        restore_required = false;
        socket = NULL;
    }

    zlink::socket_base_t *socket;
    bool restore_required;
    int original_value;
};

int recv_internal_queue_frame (zlink::socket_base_t *socket_,
                               zlink_msg_t *msg_,
                               int flags_,
                               int timeout_ms_,
                               uint64_t deadline_ms_)
{
    if (!socket_ || !msg_) {
        errno = EFAULT;
        return -1;
    }

    while (socket_->recv (reinterpret_cast<zlink::msg_t *> (msg_),
                          ZLINK_DONTWAIT)
           != 0) {
        const int saved_errno = errno;
        if (saved_errno != EAGAIN) {
            errno = saved_errno;
            return -1;
        }

        if ((flags_ & ZLINK_DONTWAIT) != 0 || timeout_ms_ == 0) {
            errno = EAGAIN;
            return -1;
        }

        long wait_ms = -1;
        if (timeout_ms_ > 0) {
            zlink::clock_t clock;
            const uint64_t now_ms = clock.now_ms ();
            if (now_ms >= deadline_ms_) {
                errno = EAGAIN;
                return -1;
            }
            wait_ms = static_cast<long> (deadline_ms_ - now_ms);
        }

        const int wait_rc =
          zlink::wait_socket_events_internal (socket_, ZLINK_POLLIN, wait_ms);
        if (wait_rc < 0)
            return -1;
        if (wait_rc == 0) {
            errno = EAGAIN;
            return -1;
        }
    }

    return 0;
}

int recv_router_followup_frame (zlink::socket_base_t *socket_,
                                zlink_msg_t *msg_)
{
    if (!socket_ || !msg_) {
        errno = EFAULT;
        return -1;
    }

    if (socket_->recv (reinterpret_cast<zlink::msg_t *> (msg_), 0) != 0)
        return -1;

    return 0;
}

bool router_raw_part_has_more (const zlink_msg_t *part_)
{
    if (!part_)
        return false;

    const zlink::msg_t *msg =
      reinterpret_cast<const zlink::msg_t *> (part_);
    if (!msg->check ())
        return false;

    return (msg->flags () & zlink::msg_t::more) != 0;
}
}

bool has_valid_routing_id (const zlink_routing_id_t *peer_rid_)
{
    return peer_rid_ && peer_rid_->size > 0
           && peer_rid_->size <= sizeof (peer_rid_->data);
}

std::string routing_id_key (const zlink_routing_id_t *peer_rid_)
{
    if (!has_valid_routing_id (peer_rid_))
        return std::string ();

    return std::string (reinterpret_cast<const char *> (peer_rid_->data),
                        peer_rid_->size);
}

int queue_router_message (socket_request_reply_state_t *state_,
                          const zlink_routing_id_t *source_node_rid_,
                          const zlink_routing_id_t *source_spot_rid_,
                          uint64_t request_seq_,
                          zlink_msg_t *parts_,
                          size_t part_count_)
{
    if (!state_ || !parts_) {
        errno = EFAULT;
        return -1;
    }

    if (zlink::internal_pair_queue::ensure (
          state_->socket ? state_->socket->get_ctx () : NULL,
          "zlink.router.reqrep.recv", &state_->recv_queue)
        != 0)
        return -1;

    unsigned char seq_buf[8];
    zlink::request_reply::encode_u64_be (request_seq_, seq_buf);
    const void *source_node_data =
      has_valid_routing_id (source_node_rid_) ? source_node_rid_->data : NULL;
    const size_t source_node_size =
      has_valid_routing_id (source_node_rid_) ? source_node_rid_->size : 0;
    const void *source_spot_data =
      has_valid_routing_id (source_spot_rid_) ? source_spot_rid_->data : NULL;
    const size_t source_spot_size =
      has_valid_routing_id (source_spot_rid_) ? source_spot_rid_->size : 0;
    if (zlink::internal_pair_queue::send_buffer_frame (
          state_->recv_queue.tx, source_node_data, source_node_size,
          ZLINK_SNDMORE)
        != 0
        || zlink::internal_pair_queue::send_buffer_frame (
             state_->recv_queue.tx, source_spot_data, source_spot_size,
             ZLINK_SNDMORE)
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
    return 0;
}

int dispatch_router_message (socket_request_reply_state_t *state_,
                             const zlink_routing_id_t *source_node_rid_,
                             const zlink_routing_id_t *source_spot_rid_,
                             uint64_t request_seq_,
                             zlink_msg_t *parts_,
                             size_t part_count_)
{
    if (!state_ || !parts_) {
        errno = EFAULT;
        return -1;
    }
    return queue_router_message (
      state_, source_node_rid_, source_spot_rid_, request_seq_, parts_,
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
                                const zlink_routing_id_t **source_spot_rid_out_,
                                uint64_t *request_seq_out_,
                                zlink_msg_t **parts_out_,
                                size_t *part_count_out_,
                                int flags_,
                                int timeout_ms_)
{
    if (!queue_ || !queue_->rx || !source_node_rid_out_
        || !source_spot_rid_out_ || !request_seq_out_ || !parts_out_
        || !part_count_out_) {
        errno = EFAULT;
        return -1;
    }

    zlink_msg_t source_node_frame;
    zlink_msg_t source_spot_frame;
    zlink_msg_t seq_frame;
    zlink_msg_t *first_payload = NULL;
    zlink_msg_init (&source_node_frame);
    zlink_msg_init (&source_spot_frame);
    zlink_msg_init (&seq_frame);

    if (zlink::recv_tls_view::begin_with_first_slot (
          parts_out_, part_count_out_, &first_payload)
        != 0)
        return -1;

    zlink::clock_t clock;
    const uint64_t deadline_ms =
      timeout_ms_ > 0 ? clock.now_ms () + static_cast<uint64_t> (timeout_ms_) : 0;

    if (recv_internal_queue_frame (queue_->rx, &source_node_frame, flags_,
                                   timeout_ms_, deadline_ms)
        != 0) {
        const int saved_errno = errno;
        zlink_msg_close (&source_node_frame);
        zlink::recv_tls_view::abort ();
        errno = saved_errno;
        return -1;
    }
    if (zlink::internal_pair_queue::recv_followup_with_retry (
          queue_->rx, &source_spot_frame, flags_)
        != 0) {
        const int saved_errno = errno;
        zlink_msg_close (&source_node_frame);
        zlink_msg_close (&source_spot_frame);
        zlink_msg_close (&seq_frame);
        zlink::recv_tls_view::abort ();
        errno = saved_errno;
        return -1;
    }
    if (zlink::internal_pair_queue::recv_followup_with_retry (
          queue_->rx, &seq_frame, flags_)
        != 0) {
        const int saved_errno = errno;
        zlink_msg_close (&source_node_frame);
        zlink_msg_close (&source_spot_frame);
        zlink_msg_close (&seq_frame);
        zlink::recv_tls_view::abort ();
        errno = saved_errno;
        return -1;
    }
    if (zlink_msg_size (&seq_frame) != 8) {
        zlink_msg_close (&source_node_frame);
        zlink_msg_close (&source_spot_frame);
        zlink_msg_close (&seq_frame);
        zlink::recv_tls_view::abort ();
        errno = EPROTO;
        return -1;
    }
    if (zlink::internal_pair_queue::recv_followup_with_retry (
          queue_->rx, first_payload, flags_)
        != 0) {
        const int saved_errno = errno;
        zlink_msg_close (&source_node_frame);
        zlink_msg_close (&source_spot_frame);
        zlink_msg_close (&seq_frame);
        zlink::recv_tls_view::abort ();
        errno = saved_errno;
        return -1;
    }

    zlink::copy_routing_id_from_msg (source_node_frame,
                                     &g_router_recv_source_rid);
    zlink::copy_routing_id_from_msg (source_spot_frame,
                                     &g_router_recv_source_spot_rid);
    *source_node_rid_out_ = &g_router_recv_source_rid;
    *source_spot_rid_out_ = &g_router_recv_source_spot_rid;
    *request_seq_out_ = zlink::request_reply::decode_u64_be (
      static_cast<const unsigned char *> (zlink_msg_data (&seq_frame)));
    zlink_msg_close (&source_node_frame);
    zlink_msg_close (&source_spot_frame);
    zlink_msg_close (&seq_frame);

    if (!zlink::msg_frame_has_more (*first_payload))
        return zlink::recv_tls_view::commit_reserved_single (parts_out_,
                                                             part_count_out_);

    if (zlink::recv_tls_view::reserve_first_slot () != 0) {
        zlink::recv_tls_view::abort ();
        errno = EMSGSIZE;
        return -1;
    }
    return zlink::export_reserved_followup_msg_sequence (
      queue_->rx, parts_out_, part_count_out_, true);
}

int recv_router_message_direct (socket_handle_t handle_,
                                const zlink_routing_id_t **source_node_rid_out_,
                                const zlink_routing_id_t **source_spot_rid_out_,
                                uint64_t *request_seq_out_,
                                zlink_msg_t **parts_out_,
                                size_t *part_count_out_,
                                int flags_)
{
    if (!handle_.socket || !source_node_rid_out_ || !source_spot_rid_out_
        || !request_seq_out_ || !parts_out_ || !part_count_out_) {
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
        if (zlink::recv_tls_view::begin_with_first_slot (
              parts_out_, part_count_out_, &first_slot)
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

        assign_routing_id_compact (&g_router_recv_source_rid, source_rid);
        g_router_recv_source_spot_rid.size = 0;
        *source_node_rid_out_ = &g_router_recv_source_rid;
        *source_spot_rid_out_ = &g_router_recv_source_spot_rid;
        *request_seq_out_ = 0;
        return zlink::recv_tls_view::commit_reserved_single (parts_out_,
                                                             part_count_out_);
    }

    std::vector<zlink_msg_t> raw_parts;
    while (true) {
        raw_parts.push_back (zlink_msg_t ());
        zlink_msg_init (&raw_parts.back ());
        if (reinterpret_cast<zlink::msg_t *> (&raw_parts.back ())
              ->move (current)
            != 0) {
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
    const bool parsed = zlink::request_reply::parse_envelope (
      raw_parts.data (), raw_parts.size (), &envelope);
    const size_t start_index =
      parsed ? zlink::request_reply::control_part_count : 0;

    if (parsed) {
        *request_seq_out_ = envelope.request_seq;
        for (size_t i = 0; i < start_index; ++i)
            zlink_msg_close (&raw_parts[i]);
    } else
        *request_seq_out_ = 0;

    for (size_t i = start_index; i < raw_parts.size (); ++i) {
        if (zlink::recv_tls_view::push (&raw_parts[i]) != 0) {
            const int saved_errno = errno;
            zlink::recv_tls_view::abort ();
            for (size_t j = i; j < raw_parts.size (); ++j)
                zlink_msg_close (&raw_parts[j]);
            errno = saved_errno;
            return -1;
        }
    }

    g_router_recv_source_rid = source_rid;
    memset (&g_router_recv_source_spot_rid, 0,
            sizeof (g_router_recv_source_spot_rid));
    *source_node_rid_out_ = &g_router_recv_source_rid;
    *source_spot_rid_out_ = &g_router_recv_source_spot_rid;
    return zlink::recv_tls_view::commit (parts_out_, part_count_out_);
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

    const bool routed = has_valid_routing_id (peer_rid_);
    const size_t total_part_count =
      zlink::request_reply::control_part_count + part_count_;
    std::vector<zlink_msg_t> combined (total_part_count);
    for (size_t i = 0; i < total_part_count; ++i)
        zlink_msg_init (&combined[i]);

    unsigned char protocol_id = zlink::request_reply::protocol_id;
    unsigned char version = zlink::request_reply::version;
    unsigned char type = message_type_;
    unsigned char seq_buf[8];
    zlink::request_reply::encode_u64_be (request_seq_, seq_buf);

    if (zlink::request_reply::init_control_part (&combined[0], &protocol_id, 1)
          != 0
        || zlink::request_reply::init_control_part (&combined[1], &version, 1)
             != 0
        || zlink::request_reply::init_control_part (&combined[2], &type, 1)
             != 0
        || zlink::request_reply::init_control_part (&combined[3], seq_buf,
                                                    sizeof (seq_buf))
             != 0) {
        const int saved_errno = errno;
        zlink::request_reply::close_built_parts (&combined);
        zlink::request_reply::consume_send_frames_from (parts_, 0, part_count_);
        errno = saved_errno;
        return -1;
    }

    for (size_t i = 0; i < part_count_; ++i) {
        if (zlink_msg_move (&combined[zlink::request_reply::control_part_count
                                      + i],
                            &parts_[i])
            != 0) {
            const int saved_errno = errno;
            zlink::request_reply::close_built_parts (&combined);
            zlink::request_reply::consume_send_frames_from (parts_, i,
                                                            part_count_);
            errno = saved_errno;
            return -1;
        }
    }

    router_mandatory_scope_t mandatory_scope;
    if (routed && mandatory_scope.arm (handle) != 0) {
        const int saved_errno = errno;
        zlink::request_reply::close_built_parts (&combined);
        errno = saved_errno;
        return -1;
    }

    const zlink_send_flags_t effective_flags =
      (routed && socket_type (handle) == ZLINK_CORE_SOCKET_ROUTER)
        ? static_cast<zlink_send_flags_t> (flags_ | ZLINK_DONTWAIT)
        : flags_;

    const int rc = routed
                     ? zlink::logical_multipart_send_routed (
                         handle.socket, peer_rid_, &combined[0],
                         total_part_count, effective_flags)
                     : zlink::logical_multipart_send (
                         handle.socket, &combined[0], total_part_count,
                         effective_flags);
    if (rc != 0)
        return -1;

    errno = 0;
    return 0;
}
}
}
