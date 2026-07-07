/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "runtime/services/spot/request_reply/spot_request_reply_local_dispatch_internal.hpp"

#include "api/socket/socket_request_reply_internal.hpp"
#include "api/spot/request_reply/service_spot_request_reply_utils_internal.hpp"
#include "api/spot/request_reply/service_spot_routed_protocol_internal.hpp"

namespace
{
namespace reqrep = zlink::socket_reqrep_internal;
namespace routed_protocol = zlink::spot_routed_protocol;

using zlink::spot_reqrep_internal::find_router_state_by_rid;
using zlink::spot_reqrep_internal::find_spot_state_by_identity;
using zlink::spot_reqrep_internal::optional_routing_id_from_key;
using zlink::spot_reqrep_internal::parsed_spot_envelope_t;
using zlink::spot_reqrep_internal::router_spot_request_reply_state_t;
using zlink::spot_reqrep_internal::spot_request_reply_state_t;

int dispatch_spot_message_local (spot_request_reply_state_t *state_,
                                 const zlink_routing_id_t *source_rid_,
                                 const zlink_routing_id_t *spot_rid_,
                                 uint64_t request_seq_,
                                 zlink_msg_t *parts_,
                                 size_t part_count_)
{
    if (zlink::spot_reqrep_internal::queue_spot_message (state_, source_rid_, spot_rid_,
                                                         request_seq_, parts_, part_count_)
        != 0) {
        zlink::request_reply::close_request_reply_parts (parts_, part_count_);
        return -1;
    }
    return 0;
}

int dispatch_router_spot_message_local (router_spot_request_reply_state_t *state_,
                                        const zlink_routing_id_t *source_node_rid_,
                                        const zlink_routing_id_t *source_spot_rid_,
                                        uint64_t request_seq_,
                                        zlink_msg_t *parts_,
                                        size_t part_count_)
{
    if (!state_ || !state_->owner) {
        zlink::request_reply::close_request_reply_parts (parts_, part_count_);
        errno = EFAULT;
        return -1;
    }

    socket_handle_t handle = as_socket_handle (state_->owner);
    if (!handle.socket) {
        zlink::request_reply::close_request_reply_parts (parts_, part_count_);
        errno = EFAULT;
        return -1;
    }

    std::shared_ptr<reqrep::socket_request_reply_state_t> router_state =
      reqrep::find_or_create_request_reply_state (handle);
    if (reqrep::dispatch_router_message (router_state.get (), source_node_rid_, source_spot_rid_,
                                         request_seq_, parts_, part_count_)
        != 0) {
        zlink::request_reply::close_request_reply_parts (parts_, part_count_);
        return -1;
    }

    return 0;
}

int synthesize_local_error_reply (const parsed_spot_envelope_t &request_envelope_,
                                  uint64_t request_seq_,
                                  int errnum_)
{
    zlink_msg_t errno_part;
    zlink_msg_init (&errno_part);

    if (zlink::request_reply::init_error_reply_errno_part (&errno_part, errnum_) != 0)
        return -1;

    std::vector<zlink_msg_t> combined;
    if (zlink::spot_reqrep_internal::build_spot_request_reply_message (
          request_envelope_.destination_class, request_envelope_.destination_node_rid,
          request_envelope_.destination_endpoint_rid, request_envelope_.source_class,
          request_envelope_.source_node_rid, request_envelope_.source_endpoint_rid,
          zlink::request_reply::error_reply_type, request_seq_, &errno_part, 1, &combined)
        != 0) {
        const int saved_errno = errno;
        zlink::request_reply::consume_send_frame (&errno_part);
        errno = saved_errno;
        return -1;
    }

    const int rc = zlink::spot_reqrep_internal::dispatch_local_reply (&combined);
    const int saved_errno = errno;
    zlink::request_reply::close_built_parts (&combined);
    errno = saved_errno;
    return rc;
}
}

int zlink::spot_reqrep_internal::dispatch_spot_request_to_spot (
  const parsed_spot_envelope_t &spot_envelope_,
  const zlink::request_reply::parsed_envelope_t &rr_envelope_)
{
    std::shared_ptr<spot_request_reply_state_t> state = find_spot_state_by_identity (
      spot_envelope_.destination_node_rid, spot_envelope_.destination_endpoint_rid);
    if (!state)
        return synthesize_local_error_reply (spot_envelope_, rr_envelope_.request_seq, ENOENT);

    zlink_routing_id_t empty_spot_rid;
    memset (&empty_spot_rid, 0, sizeof (empty_spot_rid));
    const int rc = dispatch_spot_message_local (
      state.get (),
      spot_envelope_.source_class == routed_protocol::router_endpoint_class
        ? &spot_envelope_.source_endpoint_rid_value
        : &spot_envelope_.source_node_rid_value,
      spot_envelope_.source_class == routed_protocol::spot_endpoint_class
        ? &spot_envelope_.source_endpoint_rid_value
        : &empty_spot_rid,
      rr_envelope_.request_seq, rr_envelope_.payload_parts, rr_envelope_.payload_part_count);
    return rc;
}

int zlink::spot_reqrep_internal::dispatch_spot_request_to_router (
  const std::string &router_rid_,
  const parsed_spot_envelope_t &spot_envelope_,
  const zlink::request_reply::parsed_envelope_t &rr_envelope_)
{
    std::shared_ptr<router_spot_request_reply_state_t> state =
      find_router_state_by_rid (router_rid_);
    if (!state)
        return synthesize_local_error_reply (spot_envelope_, rr_envelope_.request_seq, ENOENT);

    return dispatch_router_spot_message_local (state.get (), &spot_envelope_.source_node_rid_value,
                                               &spot_envelope_.source_endpoint_rid_value,
                                               rr_envelope_.request_seq, rr_envelope_.payload_parts,
                                               rr_envelope_.payload_part_count);
}

int zlink::spot_reqrep_internal::dispatch_local_direct_to_spot (
  uint8_t source_class_,
  const std::string &source_node_rid_,
  const std::string &source_endpoint_rid_,
  const zlink_routing_id_t *source_node_rid_value_,
  const zlink_routing_id_t *source_endpoint_rid_value_,
  const std::string &dest_node_rid_,
  const std::string &dest_spot_rid_,
  zlink_msg_t *parts_,
  size_t part_count_)
{
    std::shared_ptr<spot_request_reply_state_t> state =
      find_spot_state_by_identity (dest_node_rid_, dest_spot_rid_);
    if (!state) {
        zlink::request_reply::consume_send_frames_from (parts_, 0, part_count_);
        errno = 0;
        return 0;
    }

    zlink_routing_id_t source_rid_fallback;
    zlink_routing_id_t spot_rid_fallback;
    memset (&spot_rid_fallback, 0, sizeof (spot_rid_fallback));
    const zlink_routing_id_t *source_rid = source_class_ == routed_protocol::router_endpoint_class
                                             ? source_endpoint_rid_value_
                                             : source_node_rid_value_;
    const zlink_routing_id_t *spot_rid = source_class_ == routed_protocol::spot_endpoint_class
                                           ? source_endpoint_rid_value_
                                           : &spot_rid_fallback;
    if (!source_rid) {
        optional_routing_id_from_key (source_class_ == routed_protocol::router_endpoint_class
                                        ? source_endpoint_rid_
                                        : source_node_rid_,
                                      &source_rid_fallback);
        source_rid = &source_rid_fallback;
    }
    if (source_class_ == routed_protocol::spot_endpoint_class && !source_endpoint_rid_value_) {
        optional_routing_id_from_key (source_endpoint_rid_, &spot_rid_fallback);
        spot_rid = &spot_rid_fallback;
    }

    if (dispatch_spot_message_local (state.get (), source_rid, spot_rid, 0, parts_, part_count_)
        != 0)
        return -1;
    errno = 0;
    return 0;
}

int zlink::spot_reqrep_internal::dispatch_local_direct_to_router (
  const std::string &router_rid_,
  const std::string &source_node_rid_,
  const std::string &source_spot_rid_,
  const zlink_routing_id_t *source_node_rid_value_,
  const zlink_routing_id_t *source_spot_rid_value_,
  zlink_msg_t *parts_,
  size_t part_count_)
{
    std::shared_ptr<router_spot_request_reply_state_t> state =
      find_router_state_by_rid (router_rid_);
    if (!state) {
        zlink::request_reply::consume_send_frames_from (parts_, 0, part_count_);
        errno = ENOENT;
        return -1;
    }

    zlink_routing_id_t source_node_rid_fallback;
    zlink_routing_id_t source_spot_rid_fallback;
    const zlink_routing_id_t *source_node_rid = source_node_rid_value_;
    const zlink_routing_id_t *source_spot_rid = source_spot_rid_value_;
    if (!source_node_rid) {
        optional_routing_id_from_key (source_node_rid_, &source_node_rid_fallback);
        source_node_rid = &source_node_rid_fallback;
    }
    if (!source_spot_rid) {
        optional_routing_id_from_key (source_spot_rid_, &source_spot_rid_fallback);
        source_spot_rid = &source_spot_rid_fallback;
    }
    return dispatch_router_spot_message_local (state.get (), source_node_rid, source_spot_rid, 0,
                                               parts_, part_count_);
}
