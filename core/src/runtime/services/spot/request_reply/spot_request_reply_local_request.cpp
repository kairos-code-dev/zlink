/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "runtime/services/spot/request_reply/spot_request_reply_local_dispatch_internal.hpp"

#include "api/spot/request_reply/service_spot_routed_protocol_internal.hpp"

namespace
{
namespace routed_protocol = zlink::spot_routed_protocol;

using zlink::spot_reqrep_internal::find_router_state_by_rid;
using zlink::spot_reqrep_internal::find_spot_state_by_identity;
using zlink::spot_reqrep_internal::parsed_spot_envelope_t;
using zlink::spot_reqrep_internal::router_spot_request_reply_state_t;
using zlink::spot_reqrep_internal::spot_request_reply_state_t;

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

int zlink::spot_reqrep_internal::deliver_request_to_spot (
  const parsed_spot_envelope_t &spot_envelope_,
  const zlink::request_reply::parsed_envelope_t &rr_envelope_)
{
    std::shared_ptr<spot_request_reply_state_t> state = find_spot_state_by_identity (
      spot_envelope_.destination_node_rid, spot_envelope_.destination_endpoint_rid);
    if (!state)
        return synthesize_local_error_reply (spot_envelope_, rr_envelope_.request_seq, ENOENT);

    zlink_routing_id_t empty_spot_rid;
    memset (&empty_spot_rid, 0, sizeof (empty_spot_rid));
    const int rc = queue_local_spot_message (
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

int zlink::spot_reqrep_internal::deliver_request_to_router (
  const std::string &router_rid_,
  const parsed_spot_envelope_t &spot_envelope_,
  const zlink::request_reply::parsed_envelope_t &rr_envelope_)
{
    std::shared_ptr<router_spot_request_reply_state_t> state =
      find_router_state_by_rid (router_rid_);
    if (!state)
        return synthesize_local_error_reply (spot_envelope_, rr_envelope_.request_seq, ENOENT);

    return queue_local_router_message (state.get (), &spot_envelope_.source_node_rid_value,
                                       &spot_envelope_.source_endpoint_rid_value,
                                       rr_envelope_.request_seq, rr_envelope_.payload_parts,
                                       rr_envelope_.payload_part_count);
}
