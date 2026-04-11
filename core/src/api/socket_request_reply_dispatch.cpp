/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include <limits>
#include <string>
#include <vector>

#include "api/request_reply_protocol_internal.hpp"
#include "api/socket_request_reply_internal.hpp"
#include "core/recv_internal.hpp"
#include "core/recv_tls_view.hpp"
#include "sockets/socket_base.hpp"

namespace zlink
{
namespace socket_reqrep_internal
{
thread_local zlink_routing_id_t g_router_recv_source_rid;

namespace
{
void close_router_raw_parts (std::vector<zlink_msg_t> *parts_)
{
    if (!parts_)
        return;

    for (size_t i = 0; i < parts_->size (); ++i)
        zlink_msg_close (&(*parts_)[i]);
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

int queue_router_message (socket_request_reply_state_t *state_,
                          const zlink_routing_id_t *peer_rid_,
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
    const void *peer_data =
      has_valid_routing_id (peer_rid_) ? peer_rid_->data : NULL;
    const size_t peer_size = has_valid_routing_id (peer_rid_) ? peer_rid_->size
                                                              : 0;
    if (zlink::internal_pair_queue::send_buffer_frame (
          state_->recv_queue.tx, peer_data, peer_size, ZLINK_SNDMORE)
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

void dispatch_router_message (socket_request_reply_state_t *state_,
                              const zlink_routing_id_t *peer_rid_,
                              uint64_t request_seq_,
                              zlink_msg_t *parts_,
                              size_t part_count_)
{
    zlink_router_handler_fn handler = NULL;
    void *handler_userdata = NULL;
    {
        std::lock_guard<std::mutex> lock (state_->mutex);
        handler = state_->router_handler;
        handler_userdata = state_->router_handler_userdata;
    }

    if (handler) {
        handler (peer_rid_, request_seq_, parts_, part_count_, handler_userdata);
        return;
    }

    if (queue_router_message (state_, peer_rid_, request_seq_, parts_, part_count_)
        != 0) {
        zlink::request_reply::close_request_reply_parts (parts_, part_count_);
    }
}

void socket_request_reply_dispatch (const zlink_routing_id_t *source_rid_,
                                    zlink_msg_t *parts_,
                                    size_t part_count_,
                                    void *userdata_)
{
    socket_request_reply_state_t *state =
      static_cast<socket_request_reply_state_t *> (userdata_);
    if (!state) {
        zlink::request_reply::close_request_reply_parts (parts_, part_count_);
        return;
    }

    zlink::request_reply::parsed_envelope_t envelope;
    if (!zlink::request_reply::parse_envelope (parts_, part_count_, &envelope)) {
        dispatch_router_message (state, source_rid_, 0, parts_, part_count_);
        return;
    }

    if (envelope.message_type == zlink::request_reply::request_type) {
        if (state->socket_type == ZLINK_CORE_SOCKET_ROUTER
            && has_valid_routing_id (source_rid_)) {
            dispatch_router_message (state, source_rid_, envelope.request_seq,
                                     envelope.payload_parts,
                                     envelope.payload_part_count);
        } else {
            zlink::request_reply::close_request_reply_parts (parts_, part_count_);
        }
        return;
    }

    pending_key_t key;
    key.request_seq = envelope.request_seq;
    if (state->socket_type == ZLINK_CORE_SOCKET_ROUTER
        && has_valid_routing_id (source_rid_)) {
        key.peer_rid = routing_id_key (source_rid_);
    }

    pending_request_t pending;
    bool found = false;
    {
        std::lock_guard<std::mutex> lock (state->mutex);
        std::map<pending_key_t, pending_request_t>::iterator it =
          state->pending_requests.find (key);
        if (it != state->pending_requests.end ()) {
            pending = it->second;
            state->pending_sequences.erase (key.request_seq);
            state->pending_requests.erase (it);
            found = true;
        }
    }

    zlink::request_timeout::cancel (pending.timeout_task);

    if (!found) {
        if (state->socket_type == ZLINK_CORE_SOCKET_ROUTER
            && has_valid_routing_id (source_rid_)) {
            dispatch_router_message (state, source_rid_, envelope.request_seq,
                                     envelope.payload_parts,
                                     envelope.payload_part_count);
        } else {
            zlink::request_reply::close_request_reply_parts (parts_, part_count_);
        }
        return;
    }

    int callback_errno = 0;
    zlink_msg_t *callback_parts = envelope.payload_parts;
    size_t callback_part_count = envelope.payload_part_count;
    if (zlink::request_reply::decode_reply_completion (
          envelope.message_type, envelope.payload_parts,
          envelope.payload_part_count, &callback_errno, &callback_parts,
          &callback_part_count)
        != 0) {
        zlink::request_reply::close_request_reply_parts (parts_, part_count_);
        return;
    }

    zlink::request_reply::complete_reply_callback (
      pending.handler, callback_errno, callback_parts, callback_part_count,
      pending.userdata);
    zlink::request_reply::close_request_reply_parts (parts_, part_count_);
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

int validate_request_parts (zlink_msg_t *parts_, size_t part_count_)
{
    if ((!parts_ && part_count_ > 0) || part_count_ == 0) {
        errno = EFAULT;
        return -1;
    }

    return 0;
}

int recv_internal_router_queue (zlink::internal_pair_queue::queue_t *queue_,
                                const zlink_routing_id_t **peer_rid_out_,
                                uint64_t *request_seq_out_,
                                zlink_msg_t **parts_out_,
                                size_t *part_count_out_,
                                int flags_)
{
    if (!queue_ || !queue_->rx || !peer_rid_out_ || !request_seq_out_
        || !parts_out_ || !part_count_out_) {
        errno = EFAULT;
        return -1;
    }

    zlink_msg_t peer_frame;
    zlink_msg_t seq_frame;
    zlink_msg_t *first_payload = NULL;
    zlink_msg_init (&peer_frame);
    zlink_msg_init (&seq_frame);

    if (zlink::recv_tls_view::begin_with_first_slot (
          parts_out_, part_count_out_, &first_payload)
        != 0)
        return -1;

    while (queue_->rx->recv (reinterpret_cast<zlink::msg_t *> (&peer_frame),
                             flags_)
           != 0) {
        const int saved_errno = errno;
        zlink_msg_close (&peer_frame);
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
        zlink_msg_init (&peer_frame);
    }
    if (zlink::internal_pair_queue::recv_followup_with_retry (
          queue_->rx, &seq_frame, flags_)
        != 0) {
        const int saved_errno = errno;
        zlink_msg_close (&peer_frame);
        zlink_msg_close (&seq_frame);
        zlink::recv_tls_view::abort ();
        errno = saved_errno;
        return -1;
    }
    if (zlink_msg_size (&seq_frame) != 8) {
        zlink_msg_close (&peer_frame);
        zlink_msg_close (&seq_frame);
        zlink::recv_tls_view::abort ();
        errno = EPROTO;
        return -1;
    }
    if (zlink::internal_pair_queue::recv_followup_with_retry (
          queue_->rx, first_payload, flags_)
        != 0) {
        const int saved_errno = errno;
        zlink_msg_close (&peer_frame);
        zlink_msg_close (&seq_frame);
        zlink::recv_tls_view::abort ();
        errno = saved_errno;
        return -1;
    }

    g_router_recv_source_rid.size =
      static_cast<uint8_t> (std::min (zlink_msg_size (&peer_frame),
                                      sizeof (g_router_recv_source_rid.data)));
    if (g_router_recv_source_rid.size > 0) {
        memcpy (g_router_recv_source_rid.data, zlink_msg_data (&peer_frame),
                g_router_recv_source_rid.size);
    }
    *peer_rid_out_ = &g_router_recv_source_rid;
    *request_seq_out_ = zlink::request_reply::decode_u64_be (
      static_cast<const unsigned char *> (zlink_msg_data (&seq_frame)));
    zlink_msg_close (&peer_frame);
    zlink_msg_close (&seq_frame);

    if ((reinterpret_cast<const zlink::msg_t *> (first_payload)->flags ()
         & zlink::msg_t::more)
        == 0)
        return zlink::recv_tls_view::commit_reserved_single (parts_out_,
                                                             part_count_out_);
    return zlink::internal_pair_queue::export_followup_sequence_from_reserved_first (
      queue_->rx, parts_out_, part_count_out_);
}

int recv_router_message_direct (socket_handle_t handle_,
                                const zlink_routing_id_t **peer_rid_out_,
                                uint64_t *request_seq_out_,
                                zlink_msg_t **parts_out_,
                                size_t *part_count_out_,
                                int flags_)
{
    if (!handle_.socket || !peer_rid_out_ || !request_seq_out_ || !parts_out_
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

    std::vector<zlink_msg_t> raw_parts;
    while (true) {
        raw_parts.push_back (zlink_msg_t ());
        zlink_msg_init (&raw_parts.back ());
        if (reinterpret_cast<zlink::msg_t *> (&raw_parts.back ())
              ->move (current)
            != 0) {
            close_router_raw_parts (&raw_parts);
            const int close_rc = current.close ();
            errno_assert (close_rc == 0);
            errno = EFAULT;
            return -1;
        }

        if (!router_raw_part_has_more (&raw_parts.back ()))
            break;

        zlink::msg_t next;
        const int next_init_rc = next.init ();
        errno_assert (next_init_rc == 0);
        zlink_routing_id_t next_source_rid;
        memset (&next_source_rid, 0, sizeof (next_source_rid));
        if (handle_.socket->recv_routed (&next, &next_source_rid, ZLINK_DONTWAIT)
            != 0) {
            const int close_rc = next.close ();
            errno_assert (close_rc == 0);
            close_router_raw_parts (&raw_parts);
            return -1;
        }
        if (current.move (next) != 0) {
            const int close_rc = next.close ();
            errno_assert (close_rc == 0);
            close_router_raw_parts (&raw_parts);
            errno = EFAULT;
            return -1;
        }
    }

    if (zlink::recv_tls_view::begin (parts_out_, part_count_out_) != 0) {
        close_router_raw_parts (&raw_parts);
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
    *peer_rid_out_ = &g_router_recv_source_rid;
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

    const int rc =
      routed ? zlink_send_rid (socket_handle_, peer_rid_, &combined[0],
                               total_part_count, flags_)
             : zlink_send (socket_handle_, &combined[0], total_part_count, flags_);
    if (rc != 0)
        return -1;

    errno = 0;
    return 0;
}

int ensure_internal_dispatch_installed (
  const std::shared_ptr<socket_request_reply_state_t> &state_)
{
    if (!state_ || !state_->socket) {
        errno = EFAULT;
        return -1;
    }

    std::lock_guard<std::mutex> lock (state_->mutex);
    if (state_->internal_dispatch_installed)
        return 0;

    if (state_->socket->socket_msg_dispatch_active ()) {
        errno = EBUSY;
        return -1;
    }

    if (state_->socket->socket_set_msg_handler_with_userdata (
          &socket_request_reply_dispatch, NULL, state_.get ())
        != 0)
        return -1;

    state_->internal_dispatch_installed = true;
    return 0;
}

void cleanup_request_reply_socket (socket_handle_t handle_)
{
    if (!handle_.socket)
        return;

    bool stop_dispatch = false;
    std::shared_ptr<socket_request_reply_state_t> state =
      std::static_pointer_cast<socket_request_reply_state_t> (
        handle_.socket->request_reply_state ());
    if (state) {
        std::lock_guard<std::mutex> state_lock (state->mutex);
        stop_dispatch = state->internal_dispatch_installed;
        zlink::internal_pair_queue::close (&state->recv_queue);
    }
    handle_.socket->clear_request_reply_state ();

    if (stop_dispatch && handle_.socket->socket_msg_dispatch_active ())
        (void) handle_.socket->socket_msg_dispatch_stop ();
}
}
}
