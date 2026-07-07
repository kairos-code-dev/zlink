/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "runtime/services/spot/request_reply/spot_request_reply_local_dispatch_internal.hpp"

#include "api/spot/request_reply/service_spot_request_reply_utils_internal.hpp"
#include "api/spot/request_reply/service_spot_routed_protocol_internal.hpp"

namespace
{
namespace routed_protocol = zlink::spot_routed_protocol;

using zlink::spot_reqrep_internal::find_router_state_by_rid;
using zlink::spot_reqrep_internal::find_spot_state_by_identity;
using zlink::spot_reqrep_internal::optional_routing_id_from_key;
using zlink::spot_reqrep_internal::router_spot_request_reply_state_t;
using zlink::spot_reqrep_internal::spot_request_reply_state_t;
}

int zlink::spot_reqrep_internal::deliver_direct_to_spot (
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

    if (queue_local_spot_message (state.get (), source_rid, spot_rid, 0, parts_, part_count_) != 0)
        return -1;
    errno = 0;
    return 0;
}

int zlink::spot_reqrep_internal::deliver_direct_to_router (
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
    return queue_local_router_message (state.get (), source_node_rid, source_spot_rid, 0, parts_,
                                       part_count_);
}
