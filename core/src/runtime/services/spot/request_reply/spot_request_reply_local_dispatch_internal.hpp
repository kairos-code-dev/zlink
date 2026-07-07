/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_RUNTIME_SPOT_REQUEST_REPLY_LOCAL_DISPATCH_INTERNAL_HPP_INCLUDED__
#define __ZLINK_RUNTIME_SPOT_REQUEST_REPLY_LOCAL_DISPATCH_INTERNAL_HPP_INCLUDED__

#include <string>

#include "api/socket/request_reply_protocol_internal.hpp"
#include "api/spot/request_reply/service_spot_request_reply_internal.hpp"

namespace zlink
{
namespace spot_reqrep_internal
{
int deliver_reply_to_spot (const parsed_spot_envelope_t &spot_envelope_,
                           const zlink::request_reply::parsed_envelope_t &rr_envelope_);
int deliver_reply_to_router (const std::string &router_rid_,
                             const zlink::request_reply::parsed_envelope_t &rr_envelope_);
int dispatch_spot_request_to_spot (
  const parsed_spot_envelope_t &spot_envelope_,
  const zlink::request_reply::parsed_envelope_t &rr_envelope_);
int dispatch_spot_request_to_router (
  const std::string &router_rid_,
  const parsed_spot_envelope_t &spot_envelope_,
  const zlink::request_reply::parsed_envelope_t &rr_envelope_);
int dispatch_local_direct_to_spot (uint8_t source_class_,
                                   const std::string &source_node_rid_,
                                   const std::string &source_endpoint_rid_,
                                   const zlink_routing_id_t *source_node_rid_value_,
                                   const zlink_routing_id_t *source_endpoint_rid_value_,
                                   const std::string &dest_node_rid_,
                                   const std::string &dest_spot_rid_,
                                   zlink_msg_t *parts_,
                                   size_t part_count_);
int dispatch_local_direct_to_router (const std::string &router_rid_,
                                     const std::string &source_node_rid_,
                                     const std::string &source_spot_rid_,
                                     const zlink_routing_id_t *source_node_rid_value_,
                                     const zlink_routing_id_t *source_spot_rid_value_,
                                     zlink_msg_t *parts_,
                                     size_t part_count_);
}
}

#endif
