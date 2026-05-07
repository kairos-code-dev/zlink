/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include "api/part_helper_internal.hpp"
#include "api/request_reply_protocol_internal.hpp"
#include "api/request_timeout_scheduler_internal.hpp"
#include "api/service_api_internal.hpp"
#include "api/service_spot_request_reply_internal.hpp"
#include "api/service_spot_request_reply_utils_internal.hpp"
#include "api/service_mode_internal.hpp"
#include "api/socket_api_internal.hpp"
#include "api/socket_message_api_internal.hpp"
#include "api/socket_request_reply_internal.hpp"
#include "api/submit_result_internal.hpp"
#include "core/internal_defs.hpp"
#include "core/multipart_send_txn.hpp"
#include "services/spot/spot_handle.hpp"
#include "services/spot/spot_node.hpp"
#include "services/spot/spot_node_access.hpp"
#include "services/spot/spot_subject_access.hpp"

namespace
{
namespace reqrep = zlink::socket_reqrep_internal;

using zlink::spot_reqrep_internal::bind_router_state_rid;
using zlink::spot_reqrep_internal::build_spot_request_reply_message;
using zlink::spot_reqrep_internal::build_spot_routed_message;
using zlink::spot_reqrep_internal::dispatch_local_built_message;
using zlink::spot_reqrep_internal::erase_spot_pending_request;
using zlink::spot_reqrep_internal::find_or_create_router_state;
using zlink::spot_reqrep_internal::find_or_create_spot_state;
using zlink::spot_reqrep_internal::find_router_state_by_rid;
using zlink::spot_reqrep_internal::find_spot_state_by_identity;
using zlink::spot_reqrep_internal::pending_reply_t;
using zlink::spot_reqrep_internal::pending_spot_key_t;
using zlink::spot_reqrep_internal::register_router_spot_pending_request;
using zlink::spot_reqrep_internal::register_spot_pending_request;
using zlink::spot_reqrep_internal::resolve_spot_identity;
using zlink::spot_reqrep_internal::resolve_spot_node_routing_id;
using zlink::spot_reqrep_internal::router_spot_delivery_direct;
using zlink::spot_reqrep_internal::router_spot_delivery_reply;
using zlink::spot_reqrep_internal::router_spot_delivery_request;
using zlink::spot_reqrep_internal::router_spot_request_reply_state_t;
using zlink::spot_reqrep_internal::routing_pair_t;
using zlink::spot_reqrep_internal::routed_spot_delivery_direct;
using zlink::spot_reqrep_internal::routed_spot_delivery_reply;
using zlink::spot_reqrep_internal::routed_spot_delivery_request;
using zlink::spot_reqrep_internal::spot_request_reply_state_t;
using zlink::spot_reqrep_internal::has_valid_routing_id;
using zlink::spot_reqrep_internal::routing_id_key;
using zlink::spot_reqrep_internal::validate_request_parts;

int resolve_router_send_timeout_ms (void *router_)
{
    socket_handle_t handle = as_socket_handle (router_);
    if (!handle.socket)
        return -1;
    int timeout = -1;
    size_t timeout_size = sizeof (timeout);
    if (handle.socket->getsockopt (ZLINK_INTERNAL_OPT_SNDTIMEO, &timeout,
                                   &timeout_size)
        != 0)
        return -1;
    return timeout;
}

struct channel_reply_bridge_ctx_t
{
    std::weak_ptr<spot_request_reply_state_t> state;
    void *dealer;
    zlink_reply_handler_fn handler;
    void *userdata;
};

int validate_request_send_flags (zlink_send_flags_t flags_)
{
    if (flags_ != 0 && flags_ != ZLINK_DONTWAIT) {
        errno = ENOTSUP;
        return -1;
    }
    return 0;
}

int request_result_to_errno (zlink_request_result_t result_)
{
    switch (result_) {
    case ZLINK_REQUEST_OK:
        return 0;
    case ZLINK_REQUEST_TIMED_OUT:
        return ETIMEDOUT;
    case ZLINK_REQUEST_NOT_FOUND:
        return ENOENT;
    case ZLINK_REQUEST_TERMINATED:
        return ETERM;
    case ZLINK_REQUEST_PROTOCOL_ERROR:
        return EPROTO;
    case ZLINK_REQUEST_INTERNAL_ERROR:
    default:
        return EIO;
    }
}

bool has_local_spot_route_target (uint8_t destination_class_,
                                  const std::string &destination_node_rid_,
                                  const std::string &destination_endpoint_rid_)
{
    return destination_class_ == 0x01
             ? static_cast<bool> (find_spot_state_by_identity (
                 destination_node_rid_, destination_endpoint_rid_))
             : static_cast<bool> (
                 find_router_state_by_rid (destination_endpoint_rid_));
}

bool spot_destination_has_positive_weight (void *spot_,
                                   const zlink_routing_id_t *dest_node_rid_)
{
    spot_handle_t *spot = as_spot_handle (spot_);
    return !spot || !spot->node || spot->node->peer_has_positive_weight (dest_node_rid_);
}

int start_spot_request_common (void *spot_,
                               uint8_t destination_class_,
                               const std::string &destination_node_rid_,
                               const std::string &destination_endpoint_rid_,
                               uint8_t pending_source_class_,
                               const std::string &pending_source_rid_,
                               const std::string &pending_source_spot_rid_,
                               zlink_msg_t *parts_,
                               size_t part_count_,
                               zlink_send_flags_t flags_,
                               uint32_t timeout_ms_,
                               zlink_reply_handler_fn handler_,
                               void *userdata_)
{
    spot_handle_t *spot = as_spot_handle (spot_);
    routing_pair_t source_identity;
    if (!resolve_spot_identity (spot_, &source_identity))
        return -1;

    std::shared_ptr<spot_request_reply_state_t> state =
      find_or_create_spot_state (spot_);
    if (!state)
        return -1;
    pending_spot_key_t key;
    uint64_t request_seq = 0;
    {
        std::lock_guard<std::mutex> lock (state->mutex);
        request_seq =
          zlink::request_reply_runtime::allocate_request_sequence (
            &state->requests.next_request_seq,
            state->requests.pending_sequences);
        if (request_seq == 0)
            return -1;

        key.source_class = pending_source_class_;
        key.source_rid = pending_source_rid_;
        key.source_spot_rid = pending_source_spot_rid_;
        key.request_seq = request_seq;
    }
    if (register_spot_pending_request (state, key, timeout_ms_, handler_,
                                       userdata_)
        != 0) {
        return -1;
    }

    std::vector<zlink_msg_t> combined;
    if (build_spot_request_reply_message (
          0x01, source_identity.node_rid, source_identity.spot_rid,
          destination_class_, destination_node_rid_, destination_endpoint_rid_,
          zlink::request_reply::request_type, key.request_seq, parts_,
          part_count_, &combined)
        != 0) {
        erase_spot_pending_request (state, key);
        return -1;
    }

    const bool local_target = has_local_spot_route_target (
      destination_class_, destination_node_rid_, destination_endpoint_rid_);
    const int rc = zlink::spot_reqrep_internal::dispatch_spot_routed_delivery (
      spot ? spot->node : NULL, routed_spot_delivery_request, local_target,
      destination_class_ == 0x02 ? destination_endpoint_rid_ : std::string (),
      flags_, resolve_spot_send_timeout_ms (spot), &combined);
    if (rc != 0) {
        const int saved_errno = errno;
        zlink::request_reply::close_built_parts (&combined);
        erase_spot_pending_request (state, key);
        errno = saved_errno;
        return -1;
    }

    zlink::request_reply::close_built_parts (&combined);
    return 0;
}

int start_spot_request_to_spot (void *spot_,
                                const zlink_routing_id_t *dest_node_rid_,
                                const zlink_routing_id_t *dest_spot_rid_,
                                zlink_msg_t *parts_,
                                size_t part_count_,
                                zlink_send_flags_t flags_,
                                uint32_t timeout_ms_,
                                zlink_reply_handler_fn handler_,
                                void *userdata_)
{
    if (!has_valid_routing_id (dest_node_rid_)
        || !has_valid_routing_id (dest_spot_rid_) || !handler_) {
        errno = EINVAL;
        return -1;
    }
    if (!spot_destination_has_positive_weight (spot_, dest_node_rid_)) {
        errno = ECONNREFUSED;
        return -1;
    }

    const std::string destination_node_rid = routing_id_key (dest_node_rid_);
    const std::string destination_spot_rid = routing_id_key (dest_spot_rid_);
    return start_spot_request_common (
      spot_, 0x01, destination_node_rid, destination_spot_rid, 0x01,
      destination_node_rid, destination_spot_rid, parts_, part_count_, flags_,
      timeout_ms_, handler_, userdata_);
}

int start_spot_request_to_router (void *spot_,
                                  const zlink_routing_id_t *peer_rid_,
                                  zlink_msg_t *parts_,
                                  size_t part_count_,
                                  zlink_send_flags_t flags_,
                                  uint32_t timeout_ms_,
                                  zlink_reply_handler_fn handler_,
                                  void *userdata_)
{
    if (!handler_ || !has_valid_routing_id (peer_rid_)) {
        errno = EINVAL;
        return -1;
    }

    const std::string peer_rid = routing_id_key (peer_rid_);
    return start_spot_request_common (
      spot_, 0x02, std::string (), peer_rid, 0x02, peer_rid, std::string (),
      parts_, part_count_, flags_, timeout_ms_, handler_, userdata_);
}

int start_router_request_to_spot (void *router_,
                                  const zlink_routing_id_t *dest_node_rid_,
                                  const zlink_routing_id_t *dest_spot_rid_,
                                  zlink_msg_t *parts_,
                                  size_t part_count_,
                                  zlink_send_flags_t flags_,
                                  uint32_t timeout_ms_,
                                  zlink_reply_handler_fn handler_,
                                  void *userdata_)
{
    if (!handler_ || !has_valid_routing_id (dest_node_rid_)
        || !has_valid_routing_id (dest_spot_rid_)) {
        errno = EINVAL;
        return -1;
    }

    const std::string destination_node_rid = routing_id_key (dest_node_rid_);
    const std::string destination_spot_rid = routing_id_key (dest_spot_rid_);

    zlink_routing_id_t router_rid;
    memset (&router_rid, 0, sizeof (router_rid));
    if (zlink_get_routing_id (router_, &router_rid) != 0
        || router_rid.size == 0) {
        return -1;
    }
    const std::string router_rid_key = routing_id_key (&router_rid);

    std::shared_ptr<router_spot_request_reply_state_t> state =
      find_or_create_router_state (router_);
    bind_router_state_rid (router_, router_rid_key, state);

    uint64_t request_seq = 0;
    pending_spot_key_t key;
    {
        std::lock_guard<std::mutex> lock (state->mutex);
        request_seq =
          zlink::request_reply_runtime::allocate_request_sequence (
            &state->requests.next_request_seq,
            state->requests.pending_sequences);
        if (request_seq == 0)
            return -1;

        key.source_class = 0x01;
        key.source_rid = destination_node_rid;
        key.source_spot_rid = destination_spot_rid;
        key.request_seq = request_seq;
    }
    if (register_router_spot_pending_request (state, request_seq, key,
                                              timeout_ms_, handler_, userdata_)
        != 0) {
        return -1;
    }

    std::vector<zlink_msg_t> combined;
    if (build_spot_request_reply_message (
          0x02, std::string (), router_rid_key, 0x01, destination_node_rid,
          destination_spot_rid, zlink::request_reply::request_type,
          request_seq, parts_, part_count_, &combined)
        != 0) {
        pending_reply_t pending;
        {
            std::lock_guard<std::mutex> lock (state->mutex);
            state->requests.pending_sequences.erase (request_seq);
            std::unordered_map<uint64_t, pending_reply_t>::iterator it =
              state->requests.pending_replies.find (request_seq);
            if (it != state->requests.pending_replies.end ()) {
                pending = it->second;
                state->requests.pending_replies.erase (it);
            }
        }
        zlink::request_timeout::cancel (pending.timeout_task);
        return -1;
    }

    const int rc = zlink::spot_reqrep_internal::dispatch_router_spot_delivery (
      destination_node_rid, destination_spot_rid,
      router_spot_delivery_request, flags_,
      resolve_router_send_timeout_ms (router_), &combined);
    if (rc != 0) {
        const int saved_errno = errno;
        zlink::request_reply::close_built_parts (&combined);
        pending_reply_t pending;
        {
            std::lock_guard<std::mutex> lock (state->mutex);
            state->requests.pending_sequences.erase (request_seq);
            std::unordered_map<uint64_t, pending_reply_t>::iterator it =
              state->requests.pending_replies.find (request_seq);
            if (it != state->requests.pending_replies.end ()) {
                pending = it->second;
                state->requests.pending_replies.erase (it);
            }
        }
        zlink::request_timeout::cancel (pending.timeout_task);
        errno = saved_errno;
        return -1;
    }

    zlink::request_reply::close_built_parts (&combined);
    return 0;
}

void channel_reply_bridge_completion (zlink_request_result_t result_,
                                      zlink_msg_t *parts_,
                                      size_t part_count_,
                                      void *userdata_)
{
    std::unique_ptr<channel_reply_bridge_ctx_t> bridge (
      static_cast<channel_reply_bridge_ctx_t *> (userdata_));
    if (!bridge.get ())
        return;

    std::shared_ptr<spot_request_reply_state_t> state = bridge->state.lock ();
    if (!state)
        return;

    {
        std::lock_guard<std::mutex> lock (state->mutex);
        if (state->completion_state.pending_channel_requests > 0)
            --state->completion_state.pending_channel_requests;
    }

    const int errnum = request_result_to_errno (result_);
    (void) zlink::spot_reqrep_internal::queue_spot_channel_reply_completion (
      state, bridge->dealer, bridge->handler, bridge->userdata, errnum, parts_,
      part_count_);
}
}

zlink_submit_result_t spot_send_channel_impl (void *spot_,
                                              const char *channel_name_,
                                              zlink_msg_t *parts_,
                                              size_t part_count_,
                                              zlink_send_flags_t flags_)
{
    if (zlink::part_helper_internal::reject_if_send_sequence_open (spot_) != 0)
        return zlink::submit_result_internal::from_errno (errno);
    if (validate_request_parts (parts_, part_count_) != 0)
        return zlink::submit_result_internal::from_errno (errno);
    if (!channel_name_ || channel_name_[0] == '\0') {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }
    if (validate_recv_flags (flags_) != 0)
        return zlink::submit_result_internal::from_errno (errno);

    spot_handle_t *spot = as_spot_handle (spot_);
    if (!spot || !spot->node) {
        errno = EFAULT;
        return ZLINK_SUBMIT_INVALID_HANDLE;
    }

    zlink::socket_base_t *router =
      zlink::spot_node_access_t::select_service_router (spot->node,
                                                        channel_name_);
    if (!router)
        return zlink::submit_result_internal::from_errno (errno);
    return zlink::submit_result_internal::from_rc (
      zlink_socket_send_internal (router, parts_, part_count_, flags_));
}

zlink_submit_result_t spot_request_channel_impl (
  void *spot_,
  const char *channel_name_,
  zlink_msg_t *parts_,
  size_t part_count_,
  zlink_reply_handler_fn handler_,
  void *userdata_,
  zlink_send_flags_t flags_,
  uint32_t timeout_ms_)
{
    if (zlink::part_helper_internal::reject_if_send_sequence_open (spot_) != 0)
        return zlink::submit_result_internal::from_errno (errno);
    if (!handler_) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }
    if (validate_request_parts (parts_, part_count_) != 0)
        return zlink::submit_result_internal::from_errno (errno);
    if (validate_request_send_flags (flags_) != 0)
        return zlink::submit_result_internal::from_errno (errno);
    if (!channel_name_ || channel_name_[0] == '\0') {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }

    spot_handle_t *spot = as_spot_handle (spot_);
    if (!spot || !spot->node) {
        errno = EFAULT;
        return ZLINK_SUBMIT_INVALID_HANDLE;
    }

    zlink::socket_base_t *router =
      zlink::spot_node_access_t::select_service_router (spot->node,
                                                        channel_name_);
    if (!router)
        return zlink::submit_result_internal::from_errno (errno);

    std::shared_ptr<spot_request_reply_state_t> state =
      find_or_create_spot_state (spot_);
    if (!state)
        return zlink::submit_result_internal::from_errno (errno);
    {
        std::lock_guard<std::mutex> lock (state->mutex);
        if (state->completion_state.channel_reply_sources.count (router) == 0) {
            state->completion_state.channel_reply_sources[router] =
              std::shared_ptr<zlink::spot_reqrep_internal::spot_channel_reply_source_t> (
                new zlink::spot_reqrep_internal::spot_channel_reply_source_t (
                  router));
        }
        ++state->completion_state.pending_channel_requests;
    }

    std::shared_ptr<reqrep::socket_request_reply_state_t> socket_state =
      reqrep::find_or_create_request_reply_state (make_socket_handle (router));
    reqrep::register_spot_channel_dispatch_observer (socket_state, spot_);

    std::unique_ptr<channel_reply_bridge_ctx_t> bridge (
      new (std::nothrow) channel_reply_bridge_ctx_t ());
    if (!bridge.get ()) {
        {
            std::lock_guard<std::mutex> lock (state->mutex);
            if (state->completion_state.pending_channel_requests > 0)
                --state->completion_state.pending_channel_requests;
        }
        errno = ENOMEM;
        return zlink::submit_result_internal::from_errno (errno);
    }
    bridge->state = state;
    bridge->dealer = router;
    bridge->handler = handler_;
    bridge->userdata = userdata_;

    const int rc = reqrep::start_request (
      make_socket_handle (router), NULL, parts_, part_count_, flags_,
      timeout_ms_, &channel_reply_bridge_completion, bridge.release ());
    if (rc != 0) {
        std::lock_guard<std::mutex> lock (state->mutex);
        if (state->completion_state.pending_channel_requests > 0)
            --state->completion_state.pending_channel_requests;
    }
    return zlink::submit_result_internal::from_request_submit_rc (rc);
}

zlink_submit_result_t spot_request_spot_impl (
  void *spot_,
  const zlink_routing_id_t *dest_node_rid_,
  const zlink_routing_id_t *dest_spot_rid_,
  zlink_msg_t *parts_,
  size_t part_count_,
  zlink_reply_handler_fn handler_,
  void *userdata_,
  zlink_send_flags_t flags_,
  uint32_t timeout_ms_)
{
    if (zlink::part_helper_internal::reject_if_send_sequence_open (spot_) != 0)
        return zlink::submit_result_internal::from_errno (errno);
    if (validate_request_parts (parts_, part_count_) != 0)
        return zlink::submit_result_internal::from_errno (errno);
    if (validate_request_send_flags (flags_) != 0)
        return zlink::submit_result_internal::from_errno (errno);

    spot_handle_t *spot = as_spot_handle (spot_);
    if (!spot || !spot->node) {
        errno = EFAULT;
        return ZLINK_SUBMIT_INVALID_HANDLE;
    }

    return zlink::submit_result_internal::from_request_submit_rc (
      start_spot_request_to_spot (spot_, dest_node_rid_, dest_spot_rid_, parts_,
                                  part_count_, flags_, timeout_ms_, handler_,
                                  userdata_));
}

zlink_submit_result_t spot_request_router_impl (
  void *spot_,
  const zlink_routing_id_t *peer_rid_,
  zlink_msg_t *parts_,
  size_t part_count_,
  zlink_reply_handler_fn handler_,
  void *userdata_,
  zlink_send_flags_t flags_,
  uint32_t timeout_ms_)
{
    if (zlink::part_helper_internal::reject_if_send_sequence_open (spot_) != 0)
        return zlink::submit_result_internal::from_errno (errno);
    if (validate_request_parts (parts_, part_count_) != 0)
        return zlink::submit_result_internal::from_errno (errno);
    if (validate_request_send_flags (flags_) != 0)
        return zlink::submit_result_internal::from_errno (errno);

    spot_handle_t *spot = as_spot_handle (spot_);
    if (!spot || !spot->node) {
        errno = EFAULT;
        return ZLINK_SUBMIT_INVALID_HANDLE;
    }

    return zlink::submit_result_internal::from_request_submit_rc (
      start_spot_request_to_router (spot_, peer_rid_, parts_, part_count_,
                                    flags_, timeout_ms_, handler_, userdata_));
}

zlink_submit_result_t spot_reply_spot_impl (void *spot_,
                                            const zlink_routing_id_t *dest_node_rid_,
                                            const zlink_routing_id_t *dest_spot_rid_,
                                            uint64_t request_seq_,
                                            zlink_msg_t *parts_,
                                            size_t part_count_)
{
    if (zlink::part_helper_internal::reject_if_send_sequence_open (spot_) != 0)
        return zlink::submit_result_internal::from_errno (errno);
    if (validate_request_parts (parts_, part_count_) != 0)
        return zlink::submit_result_internal::from_errno (errno);
    if (!has_valid_routing_id (dest_node_rid_)
        || !has_valid_routing_id (dest_spot_rid_) || request_seq_ == 0) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }

    routing_pair_t source_identity;
    if (!resolve_spot_identity (spot_, &source_identity))
        return zlink::submit_result_internal::from_errno (errno);

    spot_handle_t *spot = as_spot_handle (spot_);
    const std::string destination_node_rid = routing_id_key (dest_node_rid_);
    const std::string destination_spot_rid = routing_id_key (dest_spot_rid_);
    std::vector<zlink_msg_t> combined;
    if (build_spot_request_reply_message (
          0x01, source_identity.node_rid, source_identity.spot_rid, 0x01,
          destination_node_rid, destination_spot_rid,
          zlink::request_reply::reply_type, request_seq_, parts_, part_count_,
          &combined)
        != 0) {
        return zlink::submit_result_internal::from_errno (errno);
    }
    const bool local_target = has_local_spot_route_target (
      0x01, destination_node_rid, destination_spot_rid);
    const int rc = zlink::spot_reqrep_internal::dispatch_spot_routed_delivery (
      spot ? spot->node : NULL, routed_spot_delivery_reply, local_target,
      std::string (), ZLINK_DONTWAIT, 0, &combined);
    const int saved_errno = errno;
    zlink::request_reply::close_built_parts (&combined);
    errno = saved_errno;
    return zlink::submit_result_internal::from_rc (rc);
}

zlink_submit_result_t spot_send_spot_impl (void *spot_,
                                           const zlink_routing_id_t *dest_node_rid_,
                                           const zlink_routing_id_t *dest_spot_rid_,
                                           zlink_msg_t *parts_,
                                           size_t part_count_,
                                           zlink_send_flags_t flags_)
{
    if (zlink::part_helper_internal::reject_if_send_sequence_open (spot_) != 0)
        return zlink::submit_result_internal::from_errno (errno);
    if (validate_request_parts (parts_, part_count_) != 0)
        return zlink::submit_result_internal::from_errno (errno);
    if (!has_valid_routing_id (dest_node_rid_)
        || !has_valid_routing_id (dest_spot_rid_)) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }
    if (validate_recv_flags (flags_) != 0)
        return zlink::submit_result_internal::from_errno (errno);

    routing_pair_t source_identity;
    if (!resolve_spot_identity (spot_, &source_identity))
        return zlink::submit_result_internal::from_errno (errno);

    spot_handle_t *spot = as_spot_handle (spot_);
    const std::string destination_node_rid = routing_id_key (dest_node_rid_);
    const std::string destination_spot_rid = routing_id_key (dest_spot_rid_);

    std::vector<zlink_msg_t> combined;
    if (build_spot_routed_message (
          0x01, source_identity.node_rid, source_identity.spot_rid, 0x01,
          destination_node_rid, destination_spot_rid, parts_, part_count_,
          &combined)
        != 0) {
        return zlink::submit_result_internal::from_errno (errno);
    }

    const bool local_target = has_local_spot_route_target (
      0x01, destination_node_rid, destination_spot_rid);
    const int rc = zlink::spot_reqrep_internal::dispatch_spot_routed_delivery (
      spot ? spot->node : NULL, routed_spot_delivery_direct, local_target,
      destination_spot_rid, flags_, resolve_spot_send_timeout_ms (spot),
      &combined);
    const int saved_errno = errno;
    zlink::request_reply::close_built_parts (&combined);
    errno = saved_errno;
    return zlink::submit_result_internal::from_rc (rc);
}

zlink_submit_result_t spot_reply_router_impl (
  void *spot_,
  const zlink_routing_id_t *peer_rid_,
  uint64_t request_seq_,
  zlink_msg_t *parts_,
  size_t part_count_)
{
    if (zlink::part_helper_internal::reject_if_send_sequence_open (spot_) != 0)
        return zlink::submit_result_internal::from_errno (errno);
    if (validate_request_parts (parts_, part_count_) != 0)
        return zlink::submit_result_internal::from_errno (errno);
    if (!has_valid_routing_id (peer_rid_) || request_seq_ == 0) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }

    routing_pair_t source_identity;
    if (!resolve_spot_identity (spot_, &source_identity))
        return zlink::submit_result_internal::from_errno (errno);

    spot_handle_t *spot = as_spot_handle (spot_);
    const std::string peer_rid = routing_id_key (peer_rid_);
    std::vector<zlink_msg_t> combined;
    if (build_spot_request_reply_message (
          0x01, source_identity.node_rid, source_identity.spot_rid, 0x02,
          std::string (), peer_rid, zlink::request_reply::reply_type,
          request_seq_, parts_, part_count_, &combined)
        != 0) {
        return zlink::submit_result_internal::from_errno (errno);
    }
    const bool local_target = has_local_spot_route_target (
      0x02, std::string (), peer_rid);
    const int rc = zlink::spot_reqrep_internal::dispatch_spot_routed_delivery (
      spot ? spot->node : NULL, routed_spot_delivery_reply, local_target,
      peer_rid, ZLINK_DONTWAIT, 0, &combined);
    const int saved_errno = errno;
    zlink::request_reply::close_built_parts (&combined);
    errno = saved_errno;
    return zlink::submit_result_internal::from_rc (rc);
}

zlink_submit_result_t router_request_spot_impl (
  void *router_,
  const zlink_routing_id_t *dest_node_rid_,
  const zlink_routing_id_t *dest_spot_rid_,
  zlink_msg_t *parts_,
  size_t part_count_,
  zlink_reply_handler_fn handler_,
  void *userdata_,
  zlink_send_flags_t flags_,
  uint32_t timeout_ms_)
{
    if (zlink::part_helper_internal::reject_if_send_sequence_open (router_) != 0)
        return zlink::submit_result_internal::from_errno (errno);
    if (validate_request_parts (parts_, part_count_) != 0)
        return zlink::submit_result_internal::from_errno (errno);
    if (validate_request_send_flags (flags_) != 0)
        return zlink::submit_result_internal::from_errno (errno);

    socket_handle_t handle = as_socket_handle (router_);
    if (!handle.socket || handle.socket->socket_type () != ZLINK_CORE_SOCKET_ROUTER) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }

    return zlink::submit_result_internal::from_request_submit_rc (
      start_router_request_to_spot (router_, dest_node_rid_, dest_spot_rid_,
                                    parts_, part_count_, flags_, timeout_ms_,
                                    handler_, userdata_));
}

zlink_submit_result_t router_reply_spot_impl (
  void *router_,
  const zlink_routing_id_t *dest_node_rid_,
  const zlink_routing_id_t *dest_spot_rid_,
  uint64_t request_seq_,
  zlink_msg_t *parts_,
  size_t part_count_)
{
    if (zlink::part_helper_internal::reject_if_send_sequence_open (router_) != 0)
        return zlink::submit_result_internal::from_errno (errno);
    if (validate_request_parts (parts_, part_count_) != 0)
        return zlink::submit_result_internal::from_errno (errno);
    if (!has_valid_routing_id (dest_node_rid_)
        || !has_valid_routing_id (dest_spot_rid_) || request_seq_ == 0) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }

    socket_handle_t handle = as_socket_handle (router_);
    if (!handle.socket || handle.socket->socket_type () != ZLINK_CORE_SOCKET_ROUTER) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }

    const std::string destination_node_rid = routing_id_key (dest_node_rid_);
    const std::string destination_spot_rid = routing_id_key (dest_spot_rid_);

    zlink_routing_id_t router_rid;
    memset (&router_rid, 0, sizeof (router_rid));
    if (zlink_get_routing_id (router_, &router_rid) != 0
        || router_rid.size == 0) {
        return zlink::submit_result_internal::from_errno (errno);
    }
    const std::string router_rid_key = routing_id_key (&router_rid);

    std::vector<zlink_msg_t> combined;
    if (build_spot_request_reply_message (
          0x02, std::string (), router_rid_key, 0x01, destination_node_rid,
          destination_spot_rid, zlink::request_reply::reply_type, request_seq_,
          parts_, part_count_, &combined)
        != 0) {
        return zlink::submit_result_internal::from_errno (errno);
    }
    const int rc = zlink::spot_reqrep_internal::dispatch_router_spot_delivery (
      destination_node_rid, destination_spot_rid, router_spot_delivery_reply,
      ZLINK_DONTWAIT, 0, &combined);
    const int saved_errno = errno;
    zlink::request_reply::close_built_parts (&combined);
    errno = saved_errno;
    return zlink::submit_result_internal::from_rc (rc);
}

zlink_submit_result_t router_send_spot_impl (
  void *router_,
  const zlink_routing_id_t *dest_node_rid_,
  const zlink_routing_id_t *dest_spot_rid_,
  zlink_msg_t *parts_,
  size_t part_count_,
  zlink_send_flags_t flags_)
{
    if (zlink::part_helper_internal::reject_if_send_sequence_open (router_) != 0)
        return zlink::submit_result_internal::from_errno (errno);
    if (validate_request_parts (parts_, part_count_) != 0)
        return zlink::submit_result_internal::from_errno (errno);
    if (!has_valid_routing_id (dest_node_rid_)
        || !has_valid_routing_id (dest_spot_rid_)) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }
    if (validate_recv_flags (flags_) != 0)
        return zlink::submit_result_internal::from_errno (errno);

    socket_handle_t handle = as_socket_handle (router_);
    if (!handle.socket || handle.socket->socket_type () != ZLINK_CORE_SOCKET_ROUTER) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }

    const std::string destination_node_rid = routing_id_key (dest_node_rid_);
    const std::string destination_spot_rid = routing_id_key (dest_spot_rid_);

    zlink_routing_id_t router_rid;
    memset (&router_rid, 0, sizeof (router_rid));
    if (zlink_get_routing_id (router_, &router_rid) != 0
        || router_rid.size == 0) {
        return zlink::submit_result_internal::from_errno (errno);
    }
    const std::string router_rid_key = routing_id_key (&router_rid);

    std::vector<zlink_msg_t> combined;
    if (build_spot_routed_message (
          0x02, std::string (), router_rid_key, 0x01, destination_node_rid,
          destination_spot_rid, parts_, part_count_, &combined)
        != 0) {
        return zlink::submit_result_internal::from_errno (errno);
    }
    const int rc = zlink::spot_reqrep_internal::dispatch_router_spot_delivery (
      destination_node_rid, destination_spot_rid, router_spot_delivery_direct,
      flags_, resolve_router_send_timeout_ms (router_), &combined);
    const int saved_errno = errno;
    zlink::request_reply::close_built_parts (&combined);
    errno = saved_errno;
    return zlink::submit_result_internal::from_rc (rc);
}

extern "C" int zlink_router_enable_spot_receive (void *router_)
{
    socket_handle_t handle = as_socket_handle (router_);
    if (!handle.socket || handle.socket->socket_type () != ZLINK_CORE_SOCKET_ROUTER) {
        errno = EINVAL;
        return -1;
    }

    zlink_routing_id_t router_rid;
    memset (&router_rid, 0, sizeof (router_rid));
    if (zlink_get_routing_id (router_, &router_rid) != 0
        || router_rid.size == 0) {
        errno = 0;
        return 0;
    }

    std::shared_ptr<router_spot_request_reply_state_t> state =
      find_or_create_router_state (router_);
    bind_router_state_rid (router_, routing_id_key (&router_rid), state);
    errno = 0;
    return 0;
}
