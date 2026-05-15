/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include <memory>

#include "api/socket/request_reply_protocol_internal.hpp"
#include "api/socket/part_helper_internal.hpp"
#include "api/service/service_mode_internal.hpp"
#include "api/socket/socket_request_reply_internal.hpp"
#include "api/socket/socket_request_reply_submit_internal.hpp"
#include "api/message/submit_result_internal.hpp"
#include "core/msg.hpp"

namespace reqrep = zlink::socket_reqrep_internal;

namespace
{
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
    if (reqrep::validate_request_send_flags (flags_) != 0) {
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
    bool helper_state_matches_family = false;
    if (helper_state) {
        std::lock_guard<std::mutex> lock (helper_state->mutex);
        if (helper_state->send.active
            && helper_state->send.spec.family == family_) {
            spec.request_seq = helper_state->send.spec.request_seq;
            helper_state_matches_family = true;
        }
    }

    std::shared_ptr<reqrep::socket_request_reply_state_t> request_state;
    reqrep::pending_key_t pending_key;
    if (spec.request_seq == 0) {
        if (reqrep::ensure_socket_pending_request (
              handle, peer_rid_, timeout_ms_, handler_, userdata_,
              &spec.request_seq, &request_state, &pending_key)
            != 0) {
            zlink::part_helper_internal::consume_send_part (part_);
            return zlink::submit_result_internal::from_errno (errno);
        }
    } else {
        request_state = reqrep::find_or_create_request_reply_state (handle);
        if (!request_state
            || reqrep::lookup_socket_pending_request_by_seq (
                 request_state, spec.request_seq, &pending_key)
                 != 0) {
            zlink::part_helper_internal::consume_send_part (part_);
            return zlink::submit_result_internal::from_errno (errno);
        }
    }

    zlink::msg_t *core_part = reinterpret_cast<zlink::msg_t *> (part_);
    if (!part_ || !core_part->check ()) {
        if (helper_state_matches_family)
            zlink::part_helper_internal::abort_send_step (helper_state);
        reqrep::erase_socket_pending_request (request_state, pending_key);
        zlink::part_helper_internal::consume_send_part (part_);
        errno = EFAULT;
        return zlink::submit_result_internal::from_errno (errno);
    }

    std::shared_ptr<zlink::part_helper_internal::handle_state_t> state;
    bool first_part = false;
    if (zlink::part_helper_internal::prepare_send_step (
          handle_, spec, handle.socket, &state, &first_part)
        != 0) {
        if (spec.request_seq != 0)
            reqrep::erase_socket_pending_request (request_state, pending_key);
        zlink::part_helper_internal::consume_send_part (part_);
        return zlink::submit_result_internal::from_errno (errno);
    }

    if (part_flag_ == ZLINK_PART_MORE) {
        if (reqrep::stage_request_payload_part (state.get (), part_) != 0) {
            const int saved_errno = errno;
            zlink::part_helper_internal::abort_send_step (state);
            reqrep::erase_socket_pending_request (request_state, pending_key);
            zlink::part_helper_internal::consume_send_part (part_);
            errno = saved_errno;
            return zlink::submit_result_internal::from_errno (saved_errno);
        }

        return ZLINK_SUBMIT_OK;
    }

    if (first_part || !state->send.buffered_parts.empty ()) {
        const unsigned char protocol_id = zlink::request_reply::protocol_id;
        const unsigned char version = zlink::request_reply::version;
        const unsigned char type =
          family_ == zlink::part_helper_internal::send_family_router_reply
            ? zlink::request_reply::reply_type
            : zlink::request_reply::request_type;
        unsigned char seq_buf[8];
        zlink::request_reply::encode_u64_be (spec.request_seq, seq_buf);
        if (reqrep::send_request_frame (handle.socket, state.get (), peer_rid_,
                                        &protocol_id, 1,
                                        ZLINK_SNDMORE
                                          | (flags_ & ZLINK_DONTWAIT))
              != 0
            || reqrep::send_request_frame (handle.socket, state.get (), NULL,
                                           &version, 1,
                                           ZLINK_SNDMORE
                                             | (flags_ & ZLINK_DONTWAIT))
                 != 0
            || reqrep::send_request_frame (handle.socket, state.get (), NULL,
                                           &type, 1,
                                           ZLINK_SNDMORE
                                             | (flags_ & ZLINK_DONTWAIT))
                 != 0
            || reqrep::send_request_frame (handle.socket, state.get (), NULL,
                                           seq_buf, sizeof (seq_buf),
                                           ZLINK_SNDMORE
                                             | (flags_ & ZLINK_DONTWAIT))
                 != 0) {
            const int saved_errno = errno;
            zlink::part_helper_internal::abort_send_step (state);
            reqrep::erase_socket_pending_request (request_state, pending_key);
            zlink::part_helper_internal::consume_send_part (part_);
            errno = saved_errno;
            return zlink::submit_result_internal::from_errno (saved_errno);
        }
    }

    for (size_t i = 0; i < state->send.buffered_parts.size (); ++i) {
        if (reqrep::send_request_payload_part (
              handle.socket, state.get (), peer_rid_,
              &state->send.buffered_parts[i], flags_, ZLINK_PART_MORE)
            != 0) {
            const int saved_errno = errno;
            zlink::part_helper_internal::abort_send_step (state);
            reqrep::erase_socket_pending_request (request_state, pending_key);
            zlink::part_helper_internal::consume_send_part (part_);
            errno = saved_errno;
            return zlink::submit_result_internal::from_errno (saved_errno);
        }
    }

    if (reqrep::send_request_payload_part (handle.socket, state.get (), peer_rid_,
                                           part_, flags_, part_flag_)
        != 0) {
        const int saved_errno = errno;
        zlink::part_helper_internal::abort_send_step (state);
        zlink::part_helper_internal::consume_send_part (part_);
        reqrep::erase_socket_pending_request (request_state, pending_key);
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
    if (reqrep::validate_socket_type (dealer_, ZLINK_CORE_SOCKET_DEALER) != 0) {
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
    if (reqrep::validate_socket_type (router_, ZLINK_CORE_SOCKET_ROUTER) != 0) {
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
    if (reqrep::validate_socket_type (router_, ZLINK_CORE_SOCKET_ROUTER) != 0) {
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
        if (reqrep::send_request_frame (handle.socket, state.get (), peer_rid_,
                                        &protocol_id, 1, ZLINK_SNDMORE)
              != 0
            || reqrep::send_request_frame (handle.socket, state.get (), NULL,
                                           &version, 1, ZLINK_SNDMORE)
                 != 0
            || reqrep::send_request_frame (handle.socket, state.get (), NULL,
                                           &type, 1, ZLINK_SNDMORE)
                 != 0
            || reqrep::send_request_frame (handle.socket, state.get (), NULL,
                                           seq_buf, sizeof (seq_buf),
                                           ZLINK_SNDMORE)
                 != 0) {
            const int saved_errno = errno;
            zlink::part_helper_internal::abort_send_step (state);
            zlink::part_helper_internal::consume_send_part (part_);
            errno = saved_errno;
            return zlink::submit_result_internal::from_errno (saved_errno);
        }
    }

    if (reqrep::send_request_payload_part (handle.socket, state.get (), peer_rid_,
                                           part_, ZLINK_SEND_FLAGS_NONE,
                                           part_flag_)
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
