/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include <memory>

#include "api/request_reply_protocol_internal.hpp"
#include "api/part_helper_internal.hpp"
#include "api/service_mode_internal.hpp"
#include "api/service_spot_request_reply_internal.hpp"
#include "api/socket_request_reply_internal.hpp"
#include "api/handler_result_internal.hpp"
#include "api/recv_result_internal.hpp"
#include "api/submit_result_internal.hpp"
#include "core/recv_internal.hpp"
#include "core/recv_tls_view.hpp"

namespace reqrep = zlink::socket_reqrep_internal;

extern "C" int zlink_router_enable_spot_receive (void *router_);

namespace
{
thread_local zlink_routing_id_t g_router_recv_part_source_node_rid;
thread_local zlink_routing_id_t g_router_recv_part_source_spot_rid;

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

int validate_request_send_flags (zlink_send_flags_t flags_)
{
    if (flags_ != 0 && flags_ != ZLINK_DONTWAIT) {
        errno = ENOTSUP;
        return -1;
    }
    return 0;
}

void drain_router_completion_queues (
  void *router_,
  const std::shared_ptr<reqrep::socket_request_reply_state_t> &socket_state_,
  const std::shared_ptr<zlink::spot_reqrep_internal::router_spot_request_reply_state_t>
    &router_spot_state_)
{
    if (socket_state_)
        (void) reqrep::drain_reply_completions (socket_state_, router_);
    if (router_spot_state_)
        (void) zlink::spot_reqrep_internal::drain_router_reply_completions (
          router_spot_state_, router_);
}

int wait_router_input_or_completion (
  zlink::socket_base_t *input_,
  zlink::socket_base_t *socket_signal_,
  zlink::socket_base_t *router_spot_signal_,
  long timeout_ms_,
  bool *input_ready_out_,
  bool *socket_signal_ready_out_,
  bool *router_spot_signal_ready_out_)
{
    if (!input_ || !input_ready_out_ || !socket_signal_ready_out_
        || !router_spot_signal_ready_out_) {
        errno = EFAULT;
        return -1;
    }

    *input_ready_out_ = false;
    *socket_signal_ready_out_ = false;
    *router_spot_signal_ready_out_ = false;

    zlink::socket_poller_t poller;
    if (poller.add (input_, NULL, ZLINK_POLLIN) != 0)
        return -1;
    if (socket_signal_ && poller.add (socket_signal_, NULL, ZLINK_POLLIN) != 0)
        return -1;
    if (router_spot_signal_
        && poller.add (router_spot_signal_, NULL, ZLINK_POLLIN) != 0)
        return -1;

    zlink::socket_poller_t::event_t events[3];
    const int rc = poller.wait (events, 3, timeout_ms_);
    if (rc <= 0)
        return rc;

    for (int i = 0; i < rc; ++i) {
        if (events[i].socket == input_)
            *input_ready_out_ = true;
        else if (events[i].socket == socket_signal_)
            *socket_signal_ready_out_ = true;
        else if (events[i].socket == router_spot_signal_)
            *router_spot_signal_ready_out_ = true;
    }
    return rc;
}

void consume_send_frames_local (zlink_msg_t *parts_,
                                size_t start_index_,
                                size_t part_count_)
{
    for (size_t i = start_index_; i < part_count_; ++i)
        zlink::part_helper_internal::consume_send_part (&parts_[i]);
}

void export_router_recv_part_metadata_view (
  const zlink_routing_id_t *source_node_rid_,
  const zlink_routing_id_t *source_spot_rid_,
  uint64_t request_seq_,
  const zlink_routing_id_t **source_node_rid_out_,
  const zlink_routing_id_t **source_spot_rid_out_,
  uint64_t *request_seq_out_)
{
    zlink::part_helper_internal::copy_routing_id (
      source_node_rid_, &g_router_recv_part_source_node_rid);
    zlink::part_helper_internal::copy_routing_id (
      source_spot_rid_, &g_router_recv_part_source_spot_rid);

    if (source_node_rid_out_) {
        *source_node_rid_out_ =
          source_node_rid_ ? &g_router_recv_part_source_node_rid : NULL;
    }
    if (source_spot_rid_out_) {
        *source_spot_rid_out_ =
          source_spot_rid_ ? &g_router_recv_part_source_spot_rid : NULL;
    }
    if (request_seq_out_)
        *request_seq_out_ = request_seq_;
}

int lookup_socket_pending_request_by_seq (
  const std::shared_ptr<reqrep::socket_request_reply_state_t> &state_,
  uint64_t request_seq_,
  reqrep::pending_key_t *key_out_)
{
    if (!state_ || !key_out_ || request_seq_ == 0) {
        errno = EFAULT;
        return -1;
    }

    std::lock_guard<std::mutex> lock (state_->mutex);
    std::unordered_map<uint64_t, reqrep::pending_key_t>::const_iterator it =
      state_->pending_request_keys_by_seq.find (request_seq_);
    if (it == state_->pending_request_keys_by_seq.end ()) {
        errno = EINVAL;
        return -1;
    }

    *key_out_ = it->second;
    return 0;
}

int send_request_frame (zlink::socket_base_t *socket_,
                        zlink::part_helper_internal::handle_state_t *helper_state_,
                        const zlink_routing_id_t *peer_rid_,
                        const void *data_,
                        size_t size_,
                        int flags_)
{
    zlink::msg_t msg;
    if (msg.init_size (size_) != 0)
        return -1;
    if (size_ > 0 && data_)
        memcpy (msg.data (), data_, size_);

    const int rc = peer_rid_
                     ? socket_->send_routed_scoped (
                         peer_rid_, &msg, flags_,
                         *helper_state_->send.send_scope)
                     : socket_->send_scoped (
                         &msg, flags_, *helper_state_->send.send_scope);
    const int saved_errno = errno;
    (void) msg.close ();
    errno = saved_errno;
    return rc;
}

int send_request_payload_part (zlink::socket_base_t *socket_,
                               zlink::part_helper_internal::handle_state_t *helper_state_,
                               const zlink_routing_id_t *peer_rid_,
                               zlink_msg_t *part_,
                               zlink_send_flags_t flags_,
                               zlink_part_flag_t part_flag_)
{
    return socket_->send_scoped (
      reinterpret_cast<zlink::msg_t *> (part_),
      static_cast<int> (flags_ & ZLINK_DONTWAIT)
        | (part_flag_ == ZLINK_PART_MORE ? ZLINK_SNDMORE : 0),
      *helper_state_->send.send_scope);
}

int stage_request_payload_part (
  zlink::part_helper_internal::handle_state_t *helper_state_,
  zlink_msg_t *part_)
{
    if (!helper_state_ || !part_) {
        errno = EFAULT;
        return -1;
    }

    helper_state_->send.buffered_parts.resize (
      helper_state_->send.buffered_parts.size () + 1);
    zlink_msg_t &slot = helper_state_->send.buffered_parts.back ();
    zlink_msg_init (&slot);
    if (zlink_msg_move (&slot, part_) != 0) {
        zlink_msg_close (&slot);
        helper_state_->send.buffered_parts.pop_back ();
        errno = EFAULT;
        return -1;
    }

    return 0;
}

void erase_socket_pending_request (
  const std::shared_ptr<reqrep::socket_request_reply_state_t> &state_,
  const reqrep::pending_key_t &key_)
{
    if (!state_)
        return;

    reqrep::pending_request_t pending;
    bool found = false;
    {
        std::lock_guard<std::mutex> lock (state_->mutex);
        std::unordered_map<reqrep::pending_key_t,
                           reqrep::pending_request_t,
                           reqrep::pending_key_hash_t>::iterator it =
          state_->pending_requests.find (key_);
        if (it != state_->pending_requests.end ()) {
            pending = it->second;
            state_->pending_sequences.erase (key_.request_seq);
            state_->pending_request_keys_by_seq.erase (key_.request_seq);
            state_->pending_requests.erase (it);
            found = true;
        }
    }

    if (found)
        zlink::request_timeout::cancel (pending.timeout_task);
}

int ensure_socket_pending_request (
  socket_handle_t handle_,
  const zlink_routing_id_t *peer_rid_,
  uint32_t timeout_ms_,
  zlink_reply_handler_fn handler_,
  void *userdata_,
  uint64_t *request_seq_out_,
  std::shared_ptr<reqrep::socket_request_reply_state_t> *state_out_,
  reqrep::pending_key_t *key_out_)
{
    if (!request_seq_out_ || !state_out_ || !key_out_) {
        errno = EFAULT;
        return -1;
    }

    std::shared_ptr<reqrep::socket_request_reply_state_t> state =
      reqrep::find_or_create_request_reply_state (handle_);
    if (reqrep::ensure_internal_dispatch_installed (state) != 0)
        return -1;

    reqrep::pending_key_t key;
    reqrep::pending_request_t pending;
    {
        std::lock_guard<std::mutex> lock (state->mutex);
        uint64_t request_seq = state->next_request_seq == 0 ? 1 : state->next_request_seq;
        while (state->pending_sequences.count (request_seq) != 0) {
            ++request_seq;
            if (request_seq == 0)
                request_seq = 1;
            if (request_seq == state->next_request_seq) {
                errno = EBUSY;
                return -1;
            }
        }
        state->next_request_seq = request_seq + 1 == 0 ? 1 : request_seq + 1;

        key.request_seq = request_seq;
        if (handle_.socket->socket_type () == ZLINK_CORE_SOCKET_ROUTER
            && reqrep::has_valid_routing_id (peer_rid_)) {
            key.peer_rid = reqrep::routing_id_key (peer_rid_);
        }

        pending.key = key;
        pending.handler = handler_;
        pending.userdata = userdata_;
        const uint32_t resolved_timeout_ms = zlink::request_reply::resolve_timeout_ms (
          timeout_ms_, state->default_timeout_ms);
        struct socket_timeout_callback_ctx_t
        {
            std::shared_ptr<reqrep::socket_request_reply_state_t> state;
            reqrep::pending_key_t key;
        };
        auto destroy_ctx = [] (void *userdata_) {
            delete static_cast<socket_timeout_callback_ctx_t *> (userdata_);
        };
        auto on_timeout = [] (void *userdata_) {
            std::unique_ptr<socket_timeout_callback_ctx_t> ctx (
              static_cast<socket_timeout_callback_ctx_t *> (userdata_));
            if (!ctx || !ctx->state)
                return;

            reqrep::pending_request_t pending;
            bool found = false;
            {
                std::lock_guard<std::mutex> lock (ctx->state->mutex);
                std::unordered_map<reqrep::pending_key_t,
                                   reqrep::pending_request_t,
                                   reqrep::pending_key_hash_t>::iterator it =
                  ctx->state->pending_requests.find (ctx->key);
                if (it == ctx->state->pending_requests.end ())
                    return;
                pending = it->second;
                ctx->state->pending_sequences.erase (ctx->key.request_seq);
                ctx->state->pending_request_keys_by_seq.erase (
                  ctx->key.request_seq);
                ctx->state->pending_requests.erase (it);
                found = true;
            }
            if (found) {
                (void) reqrep::queue_reply_completion (
                  ctx->state, pending.handler, pending.userdata, ETIMEDOUT,
                  NULL, 0);
            }
        };
        std::unique_ptr<socket_timeout_callback_ctx_t> timeout_ctx (
          new (std::nothrow) socket_timeout_callback_ctx_t ());
        if (!timeout_ctx) {
            errno = ENOMEM;
            return -1;
        }
        timeout_ctx->state = state;
        timeout_ctx->key = key;
        pending.timeout_task = zlink::request_timeout::schedule (
          resolved_timeout_ms, on_timeout, timeout_ctx.release (), destroy_ctx);
        if (!pending.timeout_task) {
            errno = ENOMEM;
            return -1;
        }

        state->pending_sequences.insert (request_seq);
        state->pending_requests[key] = pending;
        state->pending_request_keys_by_seq[request_seq] = key;
        *request_seq_out_ = request_seq;
    }

    *state_out_ = state;
    *key_out_ = key;
    return 0;
}

zlink_submit_result_t request_part_common (
  void *handle_,
  const zlink_routing_id_t *peer_rid_,
  zlink_msg_t *part_,
  zlink_send_flags_t flags_,
  zlink_part_flag_t part_flag_,
  uint32_t timeout_ms_,
  zlink_reply_handler_fn handler_,
  void *userdata_,
  zlink::part_helper_internal::send_family_t family_)
{
    if (zlink::part_helper_internal::validate_part_flag (part_flag_) != 0) {
        zlink::part_helper_internal::consume_send_part (part_);
        return zlink::submit_result_internal::from_errno (errno);
    }
    if (validate_request_send_flags (flags_) != 0) {
        zlink::part_helper_internal::consume_send_part (part_);
        return zlink::submit_result_internal::from_errno (errno);
    }

    socket_handle_t handle = as_socket_handle (handle_);
    if (!handle.socket) {
        zlink::part_helper_internal::consume_send_part (part_);
        errno = EFAULT;
        return zlink::submit_result_internal::from_errno (errno);
    }

    zlink::part_helper_internal::send_sequence_spec_t spec;
    spec.family = family_;
    spec.flags = flags_;
    spec.timeout_ms = timeout_ms_;
    spec.handler = handler_;
    spec.userdata = userdata_;
    spec.request_like = true;
    if (peer_rid_) {
        spec.has_rid1 = true;
        zlink::part_helper_internal::copy_routing_id (peer_rid_, &spec.rid1);
    }

    std::shared_ptr<zlink::part_helper_internal::handle_state_t> helper_state =
      zlink::part_helper_internal::find_handle_state (handle_);
    if (helper_state) {
        std::lock_guard<std::mutex> lock (helper_state->mutex);
        if (helper_state->send.active
            && helper_state->send.spec.family == family_) {
            spec.request_seq = helper_state->send.spec.request_seq;
        }
    }

    std::shared_ptr<reqrep::socket_request_reply_state_t> request_state;
    reqrep::pending_key_t pending_key;
    if (spec.request_seq == 0) {
        if (ensure_socket_pending_request (handle, peer_rid_, timeout_ms_, handler_,
                                           userdata_, &spec.request_seq,
                                           &request_state, &pending_key)
            != 0) {
            zlink::part_helper_internal::consume_send_part (part_);
            return zlink::submit_result_internal::from_errno (errno);
        }
    } else {
        request_state = reqrep::find_or_create_request_reply_state (handle);
        if (!request_state
            || lookup_socket_pending_request_by_seq (request_state,
                                                     spec.request_seq,
                                                     &pending_key)
                 != 0) {
            zlink::part_helper_internal::consume_send_part (part_);
            return zlink::submit_result_internal::from_errno (errno);
        }
    }

    std::shared_ptr<zlink::part_helper_internal::handle_state_t> state;
    bool first_part = false;
    if (zlink::part_helper_internal::prepare_send_step (
          handle_, spec, handle.socket, &state, &first_part)
        != 0) {
        if (spec.request_seq != 0)
            erase_socket_pending_request (request_state, pending_key);
        zlink::part_helper_internal::consume_send_part (part_);
        return zlink::submit_result_internal::from_errno (errno);
    }

    if (part_flag_ == ZLINK_PART_MORE) {
        if (stage_request_payload_part (state.get (), part_) != 0) {
            const int saved_errno = errno;
            zlink::part_helper_internal::abort_send_step (state);
            erase_socket_pending_request (request_state, pending_key);
            zlink::part_helper_internal::consume_send_part (part_);
            errno = saved_errno;
            return zlink::submit_result_internal::from_errno (saved_errno);
        }

        return ZLINK_SUBMIT_OK;
    }

    if (first_part) {
        const unsigned char protocol_id = zlink::request_reply::protocol_id;
        const unsigned char version = zlink::request_reply::version;
        const unsigned char type =
          family_ == zlink::part_helper_internal::send_family_router_reply
            ? zlink::request_reply::reply_type
            : zlink::request_reply::request_type;
        unsigned char seq_buf[8];
        zlink::request_reply::encode_u64_be (spec.request_seq, seq_buf);
        if (send_request_frame (handle.socket, state.get (), peer_rid_,
                                &protocol_id, 1,
                                ZLINK_SNDMORE | (flags_ & ZLINK_DONTWAIT))
              != 0
            || send_request_frame (handle.socket, state.get (), NULL,
                                   &version, 1,
                                   ZLINK_SNDMORE | (flags_ & ZLINK_DONTWAIT))
                 != 0
            || send_request_frame (handle.socket, state.get (), NULL,
                                   &type, 1,
                                   ZLINK_SNDMORE | (flags_ & ZLINK_DONTWAIT))
                 != 0
            || send_request_frame (handle.socket, state.get (), NULL,
                                   seq_buf,
                                   sizeof (seq_buf),
                                   ZLINK_SNDMORE | (flags_ & ZLINK_DONTWAIT))
                 != 0) {
            const int saved_errno = errno;
            zlink::part_helper_internal::abort_send_step (state);
            erase_socket_pending_request (request_state, pending_key);
            zlink::part_helper_internal::consume_send_part (part_);
            errno = saved_errno;
            return zlink::submit_result_internal::from_errno (saved_errno);
        }
    }

    for (size_t i = 0; i < state->send.buffered_parts.size (); ++i) {
        if (send_request_payload_part (handle.socket, state.get (), peer_rid_,
                                       &state->send.buffered_parts[i], flags_,
                                       ZLINK_PART_MORE)
            != 0) {
            const int saved_errno = errno;
            zlink::part_helper_internal::abort_send_step (state);
            erase_socket_pending_request (request_state, pending_key);
            zlink::part_helper_internal::consume_send_part (part_);
            errno = saved_errno;
            return zlink::submit_result_internal::from_errno (saved_errno);
        }
    }

    if (send_request_payload_part (handle.socket, state.get (), peer_rid_, part_, flags_,
                                   part_flag_)
        != 0) {
        const int saved_errno = errno;
        zlink::part_helper_internal::abort_send_step (state);
        zlink::part_helper_internal::consume_send_part (part_);
        erase_socket_pending_request (request_state, pending_key);
        errno = saved_errno;
        return zlink::submit_result_internal::from_errno (saved_errno);
    }

    zlink::part_helper_internal::complete_send_step (state, part_flag_);
    return ZLINK_SUBMIT_OK;
}

}

zlink_submit_result_t zlink_dealer_request_part (
  void *dealer_,
  zlink_msg_t *part_,
  zlink_send_flags_t flags_,
  zlink_part_flag_t part_flag_,
  uint32_t timeout_ms_,
  zlink_reply_handler_fn handler_,
  void *userdata_)
{
    if (!handler_) {
        zlink::part_helper_internal::consume_send_part (part_);
        errno = EINVAL;
        return zlink::submit_result_internal::from_errno (errno);
    }
    if (validate_socket_type (dealer_, ZLINK_CORE_SOCKET_DEALER) != 0) {
        zlink::part_helper_internal::consume_send_part (part_);
        return zlink::submit_result_internal::from_errno (errno);
    }
    return request_part_common (
      dealer_, NULL, part_, flags_, part_flag_, timeout_ms_, handler_,
      userdata_, zlink::part_helper_internal::send_family_dealer_request);
}

zlink_submit_result_t zlink_router_request_part (
  void *router_,
  const zlink_routing_id_t *peer_rid_,
  zlink_msg_t *part_,
  zlink_send_flags_t flags_,
  zlink_part_flag_t part_flag_,
  uint32_t timeout_ms_,
  zlink_reply_handler_fn handler_,
  void *userdata_)
{
    if (!handler_ || !reqrep::has_valid_routing_id (peer_rid_)) {
        zlink::part_helper_internal::consume_send_part (part_);
        errno = EINVAL;
        return zlink::submit_result_internal::from_errno (errno);
    }
    if (validate_socket_type (router_, ZLINK_CORE_SOCKET_ROUTER) != 0) {
        zlink::part_helper_internal::consume_send_part (part_);
        return zlink::submit_result_internal::from_errno (errno);
    }
    return request_part_common (
      router_, peer_rid_, part_, flags_, part_flag_, timeout_ms_, handler_,
      userdata_, zlink::part_helper_internal::send_family_router_request);
}

zlink_submit_result_t zlink_router_reply_part (
  void *router_,
  const zlink_routing_id_t *peer_rid_,
  uint64_t request_seq_,
  zlink_msg_t *part_,
  zlink_part_flag_t part_flag_)
{
    if (!reqrep::has_valid_routing_id (peer_rid_) || request_seq_ == 0) {
        zlink::part_helper_internal::consume_send_part (part_);
        errno = EINVAL;
        return zlink::submit_result_internal::from_errno (errno);
    }
    if (validate_socket_type (router_, ZLINK_CORE_SOCKET_ROUTER) != 0) {
        zlink::part_helper_internal::consume_send_part (part_);
        return zlink::submit_result_internal::from_errno (errno);
    }

    socket_handle_t handle = as_socket_handle (router_);
    zlink::part_helper_internal::send_sequence_spec_t spec;
    spec.family = zlink::part_helper_internal::send_family_router_reply;
    spec.request_like = true;
    spec.request_seq = request_seq_;
    spec.has_rid1 = true;
    zlink::part_helper_internal::copy_routing_id (peer_rid_, &spec.rid1);

    std::shared_ptr<zlink::part_helper_internal::handle_state_t> state;
    bool first_part = false;
    if (zlink::part_helper_internal::prepare_send_step (
          router_, spec, handle.socket, &state, &first_part)
        != 0) {
        zlink::part_helper_internal::consume_send_part (part_);
        return zlink::submit_result_internal::from_errno (errno);
    }

    if (first_part) {
        const unsigned char protocol_id = zlink::request_reply::protocol_id;
        const unsigned char version = zlink::request_reply::version;
        const unsigned char type = zlink::request_reply::reply_type;
        unsigned char seq_buf[8];
        zlink::request_reply::encode_u64_be (request_seq_, seq_buf);
        if (send_request_frame (handle.socket, state.get (), peer_rid_,
                                &protocol_id, 1,
                                ZLINK_SNDMORE) != 0
            || send_request_frame (handle.socket, state.get (), NULL,
                                   &version, 1,
                                   ZLINK_SNDMORE)
                 != 0
            || send_request_frame (handle.socket, state.get (), NULL,
                                   &type, 1,
                                   ZLINK_SNDMORE)
                 != 0
            || send_request_frame (handle.socket, state.get (), NULL,
                                   seq_buf,
                                   sizeof (seq_buf), ZLINK_SNDMORE)
                 != 0) {
            const int saved_errno = errno;
            zlink::part_helper_internal::abort_send_step (state);
            zlink::part_helper_internal::consume_send_part (part_);
            errno = saved_errno;
            return zlink::submit_result_internal::from_errno (saved_errno);
        }
    }

    if (send_request_payload_part (handle.socket, state.get (), peer_rid_, part_,
                                   ZLINK_SEND_FLAGS_NONE, part_flag_)
        != 0) {
        const int saved_errno = errno;
        zlink::part_helper_internal::abort_send_step (state);
        zlink::part_helper_internal::consume_send_part (part_);
        errno = saved_errno;
        return zlink::submit_result_internal::from_errno (saved_errno);
    }

    zlink::part_helper_internal::complete_send_step (state, part_flag_);
    return ZLINK_SUBMIT_OK;
}
