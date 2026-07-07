/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_API_SERVICE_SPOT_ACTOR_INTERNAL_HPP_INCLUDED__
#define __ZLINK_API_SERVICE_SPOT_ACTOR_INTERNAL_HPP_INCLUDED__

#include <zlink.h>
#include <stddef.h>

namespace zlink
{
class spot_node_t;

namespace spot_actor_internal
{
int node_has_any_actor (void *node_);
int process_gateway_delivery (void *node_,
                              const zlink_routing_id_t *source_node_rid_,
                              zlink_msg_t *parts_,
                              size_t part_count_);
zlink_submit_result_t send_actor_gateway_multipart_from_source (
  zlink::spot_node_t *origin_node_,
  const zlink_routing_id_t &source_node_rid_,
  const zlink_routing_id_t &target_node_rid_,
  uint8_t kind_,
  const zlink_routing_id_t &session_rid_,
  const char *actor_id_,
  uint64_t generation_,
  uint64_t request_id_,
  int32_t result_code_,
  zlink_msg_t *parts_,
  size_t part_count_,
  zlink_send_flags_t flags_);
}
}

#endif
