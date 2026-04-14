/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include <memory>

#include "api/request_reply_protocol_internal.hpp"
#include "api/service_api_internal.hpp"
#include "api/socket_request_reply_internal.hpp"
#include "api/handler_result_internal.hpp"
#include "api/recv_result_internal.hpp"
#include "api/submit_result_internal.hpp"

namespace reqrep = zlink::socket_reqrep_internal;

extern "C" int zlink_router_enable_spot_receive (void *router_);

namespace
{
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

}

zlink_submit_result_t zlink_dealer_request (void *dealer_,
                                            zlink_msg_t *parts_,
                                            size_t part_count_,
                                            zlink_reply_handler_fn handler_,
                                            void *userdata_,
                                            zlink_send_flags_t flags_,
                                            uint32_t timeout_ms_)
{
    if (!handler_) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }
    if (reqrep::validate_request_parts (parts_, part_count_) != 0)
        return zlink::submit_result_internal::from_errno (errno);
    if (validate_request_send_flags (flags_) != 0)
        return zlink::submit_result_internal::from_errno (errno);
    if (validate_socket_type (dealer_, ZLINK_CORE_SOCKET_DEALER) != 0)
        return zlink::submit_result_internal::from_errno (errno);

    return zlink::submit_result_internal::from_request_submit_rc (
      reqrep::start_request (as_socket_handle (dealer_), NULL, parts_,
                             part_count_, flags_, timeout_ms_, handler_,
                             userdata_));
}

zlink_submit_result_t zlink_router_request (void *router_,
                                            const zlink_routing_id_t *peer_rid_,
                                            zlink_msg_t *parts_,
                                            size_t part_count_,
                                            zlink_reply_handler_fn handler_,
                                            void *userdata_,
                                            zlink_send_flags_t flags_,
                                            uint32_t timeout_ms_)
{
    if (!handler_ || !reqrep::has_valid_routing_id (peer_rid_)) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }
    if (reqrep::validate_request_parts (parts_, part_count_) != 0)
        return zlink::submit_result_internal::from_errno (errno);
    if (validate_request_send_flags (flags_) != 0)
        return zlink::submit_result_internal::from_errno (errno);
    if (validate_socket_type (router_, ZLINK_CORE_SOCKET_ROUTER) != 0)
        return zlink::submit_result_internal::from_errno (errno);

    return zlink::submit_result_internal::from_request_submit_rc (
      reqrep::start_request (as_socket_handle (router_), peer_rid_, parts_,
                             part_count_, flags_, timeout_ms_, handler_,
                             userdata_));
}

zlink_submit_result_t zlink_router_reply (void *router_,
                                          const zlink_routing_id_t *peer_rid_,
                                          uint64_t request_seq_,
                                          zlink_msg_t *parts_,
                                          size_t part_count_)
{
    if (!reqrep::has_valid_routing_id (peer_rid_) || request_seq_ == 0) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }
    if (reqrep::validate_request_parts (parts_, part_count_) != 0)
        return zlink::submit_result_internal::from_errno (errno);
    if (validate_socket_type (router_, ZLINK_CORE_SOCKET_ROUTER) != 0)
        return zlink::submit_result_internal::from_errno (errno);

    socket_handle_t handle = as_socket_handle (router_);
    if (!handle.socket)
        return zlink::submit_result_internal::from_errno (errno);

    return zlink::submit_result_internal::from_rc (
      reqrep::send_request_reply_message (router_, peer_rid_, parts_,
                                         part_count_, ZLINK_SEND_FLAGS_NONE,
                                         zlink::request_reply::reply_type,
                                         request_seq_));
}

zlink_handler_result_t zlink_router_handler (void *router_,
                                            zlink_router_handler_fn handler_,
                                            void *userdata_)
{
    if (!handler_) {
        errno = EINVAL;
        return ZLINK_HANDLER_INVALID_ARGUMENT;
    }
    if (validate_socket_type (router_, ZLINK_CORE_SOCKET_ROUTER) != 0)
        return zlink::handler_result_internal::from_errno (errno);

    socket_handle_t handle = as_socket_handle (router_);
    if (!handle.socket)
        return zlink::handler_result_internal::from_errno (EFAULT);
    if (zlink_router_enable_spot_receive (router_) != 0)
        return zlink::handler_result_internal::from_errno (errno);

    std::shared_ptr<reqrep::socket_request_reply_state_t> state =
      reqrep::find_or_create_request_reply_state (handle);
    if (reqrep::ensure_recv_queue_ready (state) != 0)
        return zlink::handler_result_internal::from_errno (errno);
    if (reqrep::ensure_internal_dispatch_installed (state) != 0)
        return zlink::handler_result_internal::from_errno (errno);

    std::lock_guard<std::mutex> lock (state->mutex);
    if (state->router_handler) {
        errno = EBUSY;
        return ZLINK_HANDLER_BUSY;
    }

    state->router_handler = handler_;
    state->router_handler_userdata = userdata_;
    errno = 0;
    return ZLINK_HANDLER_OK;
}

zlink_recv_result_t zlink_router_recv (void *router_,
                                      const zlink_routing_id_t **source_node_rid_out_,
                                      const zlink_routing_id_t **source_spot_rid_out_,
                                      uint64_t *request_seq_out_,
                                      zlink_msg_t **parts_out_,
                                      size_t *part_count_out_,
                                      zlink_recv_flags_t flags_)
{
    if (!source_node_rid_out_ || !source_spot_rid_out_ || !request_seq_out_
        || !parts_out_ || !part_count_out_) {
        errno = EFAULT;
        return ZLINK_RECV_INVALID_HANDLE;
    }
    if (validate_recv_flags (flags_) != 0)
        return zlink::recv_result_internal::from_errno (errno);
    if (validate_socket_type (router_, ZLINK_CORE_SOCKET_ROUTER) != 0)
        return zlink::recv_result_internal::from_errno (errno);

    socket_handle_t handle = as_socket_handle (router_);
    if (!handle.socket)
        return zlink::recv_result_internal::from_errno (EFAULT);
    if (zlink_router_enable_spot_receive (router_) != 0)
        return zlink::recv_result_internal::from_errno (errno);

    std::shared_ptr<reqrep::socket_request_reply_state_t> state =
      reqrep::find_request_reply_state (handle);

    if (!state) {
        const int direct_rc = reqrep::recv_router_message_direct (
          handle, source_node_rid_out_, source_spot_rid_out_,
          request_seq_out_, parts_out_, part_count_out_, flags_);
        if (direct_rc == 0)
            return ZLINK_RECV_OK;
        return zlink::recv_result_internal::from_errno (errno);
    }

    std::unique_lock<std::mutex> lock (state->mutex);
    if (state->router_handler) {
        errno = EBUSY;
        return ZLINK_RECV_BUSY;
    }
    const bool can_drain_direct =
      !state->internal_dispatch_installed && state->pending_requests.empty ()
      && state->pending_sequences.empty ();
    lock.unlock ();

    if (can_drain_direct) {
        const int direct_rc = reqrep::recv_router_message_direct (
          handle, source_node_rid_out_, source_spot_rid_out_,
          request_seq_out_, parts_out_, part_count_out_, flags_);
        if (direct_rc == 0)
            return ZLINK_RECV_OK;
        return zlink::recv_result_internal::from_errno (errno);
    }

    if (reqrep::ensure_recv_queue_ready (state) != 0)
        return zlink::recv_result_internal::from_errno (errno);
    if (reqrep::ensure_internal_dispatch_installed (state) != 0)
        return zlink::recv_result_internal::from_errno (errno);
    int timeout_ms = -1;
    size_t timeout_size = sizeof (timeout_ms);
    if (handle.socket->getsockopt (ZLINK_INTERNAL_OPT_RCVTIMEO, &timeout_ms,
                                   &timeout_size)
        != 0)
        return zlink::recv_result_internal::from_errno (errno);
    return zlink::recv_result_internal::from_rc (
      reqrep::recv_internal_router_queue (&state->recv_queue,
                                          source_node_rid_out_,
                                          source_spot_rid_out_,
                                          request_seq_out_, parts_out_,
                                          part_count_out_, flags_,
                                          timeout_ms));
}

extern "C" void zlink_socket_request_reply_cleanup (void *socket_)
{
    reqrep::cleanup_request_reply_socket (as_socket_handle (socket_));
}

extern "C" int zlink_router_enable_request_reply_receive (void *router_)
{
    if (validate_socket_type (router_, ZLINK_CORE_SOCKET_ROUTER) != 0)
        return -1;

    socket_handle_t handle = as_socket_handle (router_);
    if (!handle.socket) {
        errno = EFAULT;
        return -1;
    }

    std::shared_ptr<reqrep::socket_request_reply_state_t> state =
      reqrep::find_or_create_request_reply_state (handle);
    if (reqrep::ensure_recv_queue_ready (state) != 0)
        return -1;
    return reqrep::ensure_internal_dispatch_installed (state);
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

    std::shared_ptr<reqrep::socket_request_reply_state_t> state =
      reqrep::find_or_create_request_reply_state (handle);
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

    std::shared_ptr<reqrep::socket_request_reply_state_t> state =
      reqrep::find_or_create_request_reply_state (handle);
    int timeout_ms = 0;
    {
        std::lock_guard<std::mutex> lock (state->mutex);
        timeout_ms = static_cast<int> (state->default_timeout_ms);
    }

    memcpy (optval_, &timeout_ms, sizeof (timeout_ms));
    *optvallen_ = sizeof (timeout_ms);
    return 0;
}
