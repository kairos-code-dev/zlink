/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include <limits>
#include <memory>
#include <set>
#include <string>

#include "api/request_timeout_scheduler_internal.hpp"
#include "api/internal_pair_queue_internal.hpp"
#include "api/request_reply_protocol_internal.hpp"
#include "api/socket_api_internal.hpp"
#include "api/service_api_internal.hpp"
#include "core/multipart_send_txn.hpp"
#include "core/recv_internal.hpp"
#include "core/recv_tls_view.hpp"
#include "sockets/socket_base.hpp"
#include "utils/random.hpp"

namespace
{
struct pending_key_t
{
    std::string peer_rid;
    uint64_t request_seq;

    bool operator< (const pending_key_t &other_) const
    {
        if (request_seq != other_.request_seq)
            return request_seq < other_.request_seq;
        return peer_rid < other_.peer_rid;
    }
};

struct pending_request_t
{
    pending_key_t key;
    zlink_reply_handler_fn handler;
    void *userdata;
    std::shared_ptr<zlink::request_timeout::task_t> timeout_task;
};

struct socket_request_reply_state_t
{
    explicit socket_request_reply_state_t (zlink::socket_base_t *socket_,
                                           int socket_type_) :
        socket (socket_),
        socket_type (socket_type_),
        default_timeout_ms (zlink::request_reply::default_timeout_ms),
        next_request_seq (1),
        internal_dispatch_installed (false),
        router_handler (NULL),
        router_handler_userdata (NULL)
    {
    }

    zlink::socket_base_t *socket;
    int socket_type;
    std::mutex mutex;
    uint32_t default_timeout_ms;
    uint64_t next_request_seq;
    std::set<uint64_t> pending_sequences;
    std::map<pending_key_t, pending_request_t> pending_requests;
    bool internal_dispatch_installed;
    zlink::internal_pair_queue::queue_t recv_queue;
    zlink_router_handler_fn router_handler;
    void *router_handler_userdata;
};

thread_local zlink_routing_id_t g_router_recv_source_rid;

bool has_valid_routing_id (const zlink_routing_id_t *peer_rid_);

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

bool has_valid_routing_id (const zlink_routing_id_t *peer_rid_)
{
    return peer_rid_ && peer_rid_->size > 0
           && peer_rid_->size <= sizeof (peer_rid_->data);
}

int validate_socket_type (void *socket_, int expected_type_)
{
    const socket_handle_t handle = as_socket_handle (socket_);
    if (!handle.socket)
        return -1;

    if (socket_type (handle) != expected_type_) {
        errno = EINVAL;
        return -1;
    }

    return 0;
}

std::string routing_id_key (const zlink_routing_id_t *peer_rid_)
{
    if (!has_valid_routing_id (peer_rid_))
        return std::string ();

    return std::string (reinterpret_cast<const char *> (peer_rid_->data),
                        peer_rid_->size);
}

int send_request_reply_message (void *socket_handle_,
                                const zlink_routing_id_t *peer_rid_,
                                zlink_msg_t *parts_,
                                size_t part_count_,
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
                               total_part_count, 0)
             : zlink_send (socket_handle_, &combined[0], total_part_count, 0);
    if (rc != 0)
        return -1;

    errno = 0;
    return 0;
}

std::shared_ptr<socket_request_reply_state_t>
find_or_create_request_reply_state (socket_handle_t handle_)
{
    std::shared_ptr<socket_request_reply_state_t> state =
      handle_.socket ? std::static_pointer_cast<socket_request_reply_state_t> (
                        handle_.socket->request_reply_state ())
                     : std::shared_ptr<socket_request_reply_state_t> ();
    if (state)
        return state;

    state.reset (
      new socket_request_reply_state_t (handle_.socket, socket_type (handle_)));
    handle_.socket->set_request_reply_state (state);
    return state;
}

std::shared_ptr<socket_request_reply_state_t>
find_request_reply_state (socket_handle_t handle_)
{
    return handle_.socket
             ? std::static_pointer_cast<socket_request_reply_state_t> (
                 handle_.socket->request_reply_state ())
                          : std::shared_ptr<socket_request_reply_state_t> ();
}

uint64_t allocate_request_seq (socket_request_reply_state_t *state_)
{
    if (!state_) {
        errno = EFAULT;
        return 0;
    }

    const uint64_t start = state_->next_request_seq == 0 ? 1 : state_->next_request_seq;
    uint64_t candidate = start;

    do {
        if (candidate == 0)
            candidate = 1;

        if (state_->pending_sequences.count (candidate) == 0) {
            uint64_t next = candidate + 1;
            if (next == 0)
                next = 1;
            state_->next_request_seq = next;
            return candidate;
        }

        ++candidate;
        if (candidate == 0)
            candidate = 1;
    } while (candidate != start);

    errno = EBUSY;
    return 0;
}

struct socket_timeout_callback_ctx_t
{
    std::shared_ptr<socket_request_reply_state_t> state;
    pending_key_t key;
};

void on_socket_request_timeout (void *userdata_)
{
    std::unique_ptr<socket_timeout_callback_ctx_t> ctx (
      static_cast<socket_timeout_callback_ctx_t *> (userdata_));
    if (!ctx.get () || !ctx->state)
        return;

    pending_request_t pending;
    bool found = false;
    {
        std::lock_guard<std::mutex> lock (ctx->state->mutex);
        std::map<pending_key_t, pending_request_t>::iterator it =
          ctx->state->pending_requests.find (ctx->key);
        if (it == ctx->state->pending_requests.end ())
            return;
        pending = it->second;
        ctx->state->pending_sequences.erase (ctx->key.request_seq);
        ctx->state->pending_requests.erase (it);
        found = true;
    }

    if (found)
        zlink::request_reply::complete_reply_callback (
          pending.handler, ETIMEDOUT, NULL, 0, pending.userdata);
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

int start_request (socket_handle_t handle_,
                   const zlink_routing_id_t *peer_rid_,
                   zlink_msg_t *parts_,
                   size_t part_count_,
                   uint32_t timeout_ms_,
                   zlink_reply_handler_fn handler_,
                   void *userdata_)
{
    std::shared_ptr<socket_request_reply_state_t> state =
      find_or_create_request_reply_state (handle_);
    if (ensure_internal_dispatch_installed (state) != 0)
        return -1;

    pending_key_t key;
    pending_request_t pending;
    uint32_t resolved_timeout_ms = zlink::request_reply::default_timeout_ms;
    {
        std::lock_guard<std::mutex> lock (state->mutex);
        const uint64_t request_seq = allocate_request_seq (state.get ());
        if (request_seq == 0)
            return -1;

        key.request_seq = request_seq;
        if (handle_.socket->socket_type () == ZLINK_CORE_SOCKET_ROUTER
            && has_valid_routing_id (peer_rid_)) {
            key.peer_rid = routing_id_key (peer_rid_);
        }

        pending.key = key;
        pending.handler = handler_;
        pending.userdata = userdata_;
        resolved_timeout_ms = zlink::request_reply::resolve_timeout_ms (
          timeout_ms_, state->default_timeout_ms);
        std::unique_ptr<socket_timeout_callback_ctx_t> timeout_ctx (
          new (std::nothrow) socket_timeout_callback_ctx_t ());
        if (!timeout_ctx.get ()) {
            errno = ENOMEM;
            return -1;
        }
        timeout_ctx->state = state;
        timeout_ctx->key = key;
        pending.timeout_task =
          zlink::request_timeout::schedule (resolved_timeout_ms,
                                            &on_socket_request_timeout,
                                            timeout_ctx.release ());
        if (!pending.timeout_task) {
            errno = ENOMEM;
            return -1;
        }
        state->pending_sequences.insert (request_seq);
        state->pending_requests[key] = pending;
    }

    const uint8_t message_type = zlink::request_reply::request_type;
    const int rc =
      send_request_reply_message (handle_.socket, peer_rid_, parts_, part_count_,
                                  message_type, key.request_seq);
    if (rc != 0) {
        std::lock_guard<std::mutex> lock (state->mutex);
        zlink::request_timeout::cancel (pending.timeout_task);
        state->pending_sequences.erase (key.request_seq);
        state->pending_requests.erase (key);
        return -1;
    }
    return 0;
}
}

int zlink_dealer_request (void *dealer_,
                          zlink_msg_t *parts_,
                          size_t part_count_,
                          uint32_t timeout_ms_,
                          zlink_reply_handler_fn handler_,
                          void *userdata_)
{
    if (!handler_) {
        errno = EINVAL;
        return -1;
    }
    if (validate_request_parts (parts_, part_count_) != 0)
        return -1;
    if (validate_socket_type (dealer_, ZLINK_CORE_SOCKET_DEALER) != 0)
        return -1;

    return start_request (as_socket_handle (dealer_), NULL, parts_, part_count_,
                          timeout_ms_, handler_, userdata_);
}

int zlink_router_request (void *router_,
                          const zlink_routing_id_t *peer_rid_,
                          zlink_msg_t *parts_,
                          size_t part_count_,
                          uint32_t timeout_ms_,
                          zlink_reply_handler_fn handler_,
                          void *userdata_)
{
    if (!handler_ || !has_valid_routing_id (peer_rid_)) {
        errno = EINVAL;
        return -1;
    }
    if (validate_request_parts (parts_, part_count_) != 0)
        return -1;
    if (validate_socket_type (router_, ZLINK_CORE_SOCKET_ROUTER) != 0)
        return -1;

    return start_request (as_socket_handle (router_), peer_rid_, parts_,
                          part_count_, timeout_ms_, handler_, userdata_);
}

int zlink_router_reply (void *router_,
                        const zlink_routing_id_t *peer_rid_,
                        uint64_t request_seq_,
                        zlink_msg_t *parts_,
                        size_t part_count_)
{
    if (!has_valid_routing_id (peer_rid_) || request_seq_ == 0) {
        errno = EINVAL;
        return -1;
    }
    if (validate_request_parts (parts_, part_count_) != 0)
        return -1;
    if (validate_socket_type (router_, ZLINK_CORE_SOCKET_ROUTER) != 0)
        return -1;

    socket_handle_t handle = as_socket_handle (router_);
    if (!handle.socket)
        return -1;

    return send_request_reply_message (router_, peer_rid_, parts_,
                                       part_count_,
                                       zlink::request_reply::reply_type,
                                       request_seq_);
}

int zlink_router_handler (void *router_,
                          zlink_router_handler_fn handler_,
                          void *userdata_)
{
    if (!handler_) {
        errno = EINVAL;
        return -1;
    }
    if (validate_socket_type (router_, ZLINK_CORE_SOCKET_ROUTER) != 0)
        return -1;

    socket_handle_t handle = as_socket_handle (router_);
    if (!handle.socket)
        return -1;

    std::shared_ptr<socket_request_reply_state_t> state =
      find_or_create_request_reply_state (handle);
    if (ensure_internal_dispatch_installed (state) != 0)
        return -1;

    std::lock_guard<std::mutex> lock (state->mutex);
    if (state->router_handler) {
        errno = EBUSY;
        return -1;
    }

    state->router_handler = handler_;
    state->router_handler_userdata = userdata_;
    errno = 0;
    return 0;
}

int zlink_router_recv (void *router_,
                       const zlink_routing_id_t **peer_rid_out_,
                       uint64_t *request_seq_out_,
                       zlink_msg_t **parts_out_,
                       size_t *part_count_out_,
                       int flags_)
{
    if (!peer_rid_out_ || !request_seq_out_ || !parts_out_ || !part_count_out_) {
        errno = EFAULT;
        return -1;
    }
    if (validate_recv_flags (flags_) != 0)
        return -1;
    if (validate_socket_type (router_, ZLINK_CORE_SOCKET_ROUTER) != 0)
        return -1;

    socket_handle_t handle = as_socket_handle (router_);
    if (!handle.socket)
        return -1;

    std::shared_ptr<socket_request_reply_state_t> state =
      find_request_reply_state (handle);
    if (!state)
        return recv_router_message_direct (handle, peer_rid_out_,
                                           request_seq_out_, parts_out_,
                                           part_count_out_, flags_);

    {
        std::unique_lock<std::mutex> lock (state->mutex);
        if (state->router_handler) {
            errno = EBUSY;
            return -1;
        }

        if (!state->internal_dispatch_installed
            && state->pending_requests.empty ()) {
            lock.unlock ();
            return recv_router_message_direct (handle, peer_rid_out_,
                                               request_seq_out_, parts_out_,
                                               part_count_out_, flags_);
        }

        if (zlink::internal_pair_queue::ensure (handle.socket->get_ctx (),
                                        "zlink.router.reqrep.recv",
                                        &state->recv_queue)
            != 0)
            return -1;
        lock.unlock ();
        return recv_internal_router_queue (&state->recv_queue, peer_rid_out_,
                                           request_seq_out_, parts_out_,
                                           part_count_out_, flags_);
    }
}

extern "C" void zlink_socket_request_reply_cleanup (void *socket_)
{
    const socket_handle_t handle = as_socket_handle (socket_);
    if (!handle.socket)
        return;

    bool stop_dispatch = false;
    std::shared_ptr<socket_request_reply_state_t> state =
      std::static_pointer_cast<socket_request_reply_state_t> (
        handle.socket->request_reply_state ());
    if (state) {
        std::lock_guard<std::mutex> state_lock (state->mutex);
        stop_dispatch = state->internal_dispatch_installed;
        zlink::internal_pair_queue::close (&state->recv_queue);
    }
    handle.socket->clear_request_reply_state ();

    if (stop_dispatch && handle.socket->socket_msg_dispatch_active ())
        (void) handle.socket->socket_msg_dispatch_stop ();
}

extern "C" int zlink_socket_request_reply_set_default_timeout (
  void *socket_,
  const void *optval_,
  size_t optvallen_)
{
    const socket_handle_t handle = as_socket_handle (socket_);
    if (!handle.socket) {
        errno = EINVAL;
        return -1;
    }

    const int type = socket_type (handle);
    if (type != ZLINK_CORE_SOCKET_ROUTER && type != ZLINK_CORE_SOCKET_DEALER) {
        errno = EINVAL;
        return -1;
    }
    if (!optval_ || optvallen_ != sizeof (int)) {
        errno = EINVAL;
        return -1;
    }

    int timeout_ms = 0;
    memcpy (&timeout_ms, optval_, sizeof (timeout_ms));
    if (timeout_ms < 0) {
        errno = EINVAL;
        return -1;
    }

    std::shared_ptr<socket_request_reply_state_t> state =
      find_or_create_request_reply_state (handle);
    std::lock_guard<std::mutex> lock (state->mutex);
    state->default_timeout_ms = static_cast<uint32_t> (timeout_ms);
    return 0;
}

extern "C" int zlink_socket_request_reply_get_default_timeout (
  void *socket_,
  void *optval_,
  size_t *optvallen_)
{
    const socket_handle_t handle = as_socket_handle (socket_);
    if (!handle.socket) {
        errno = EINVAL;
        return -1;
    }

    const int type = socket_type (handle);
    if (type != ZLINK_CORE_SOCKET_ROUTER && type != ZLINK_CORE_SOCKET_DEALER) {
        errno = EINVAL;
        return -1;
    }
    if (!optval_ || !optvallen_ || *optvallen_ < sizeof (int)) {
        errno = EINVAL;
        return -1;
    }

    std::shared_ptr<socket_request_reply_state_t> state =
      find_or_create_request_reply_state (handle);
    int timeout_ms = 0;
    {
        std::lock_guard<std::mutex> lock (state->mutex);
        timeout_ms = static_cast<int> (state->default_timeout_ms);
    }

    memcpy (optval_, &timeout_ms, sizeof (timeout_ms));
    *optvallen_ = sizeof (timeout_ms);
    return 0;
}
