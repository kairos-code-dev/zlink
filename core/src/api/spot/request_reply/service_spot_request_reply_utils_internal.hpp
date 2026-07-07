/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_SERVICE_SPOT_REQUEST_REPLY_UTILS_INTERNAL_HPP_INCLUDED__
#define __ZLINK_SERVICE_SPOT_REQUEST_REPLY_UTILS_INTERNAL_HPP_INCLUDED__

#include <memory>
#include <string>

#include <zlink.h>

#include "api/spot/request_reply/service_spot_routed_protocol_internal.hpp"

namespace zlink
{
class ctx_t;
struct spot_runtime_t;
}

namespace zlink
{
namespace spot_reqrep_internal
{
struct router_spot_request_reply_state_t;
struct spot_request_reply_state_t;

struct routing_pair_t
{
    std::string node_rid;
    std::string spot_rid;
};

ctx_t *resolve_spot_ctx (void *spot_);
spot_runtime_t *resolve_spot_runtime (void *spot_);
bool has_valid_routing_id (const zlink_routing_id_t *peer_rid_);
std::string routing_id_key (const zlink_routing_id_t *peer_rid_);
bool routing_id_from_key (const std::string &value_, zlink_routing_id_t *out_);
void optional_routing_id_from_key (const std::string &value_, zlink_routing_id_t *out_);
bool resolve_spot_identity (void *spot_, routing_pair_t *out_);
std::shared_ptr<spot_request_reply_state_t>
find_spot_state_by_identity (const std::string &node_rid_, const std::string &spot_rid_);
std::shared_ptr<router_spot_request_reply_state_t>
find_router_state_by_rid (const std::string &router_rid_);

inline bool has_local_spot_route_target (uint8_t destination_class_,
                                         const std::string &destination_node_rid_,
                                         const std::string &destination_endpoint_rid_,
                                         const std::string &local_node_rid_)
{
    if (destination_class_ == zlink::spot_routed_protocol::spot_endpoint_class) {
        if (!local_node_rid_.empty () && destination_node_rid_ != local_node_rid_)
            return false;
        return static_cast<bool> (
          find_spot_state_by_identity (destination_node_rid_, destination_endpoint_rid_));
    }
    return static_cast<bool> (find_router_state_by_rid (destination_endpoint_rid_));
}
}
}

#endif
