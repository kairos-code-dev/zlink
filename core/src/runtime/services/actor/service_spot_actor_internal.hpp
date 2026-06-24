/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_API_SERVICE_SPOT_ACTOR_INTERNAL_HPP_INCLUDED__
#define __ZLINK_API_SERVICE_SPOT_ACTOR_INTERNAL_HPP_INCLUDED__

#include <zlink.h>
#include <stddef.h>

namespace zlink
{
namespace spot_actor_internal
{
int node_has_any_actor (void *node_);
int process_gateway_delivery (void *node_,
                              const zlink_routing_id_t *source_node_rid_,
                              zlink_msg_t *parts_,
                              size_t part_count_);
}
}

#endif
