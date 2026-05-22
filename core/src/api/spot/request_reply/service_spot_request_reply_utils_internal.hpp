/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_SERVICE_SPOT_REQUEST_REPLY_UTILS_INTERNAL_HPP_INCLUDED__
#define __ZLINK_SERVICE_SPOT_REQUEST_REPLY_UTILS_INTERNAL_HPP_INCLUDED__

#include <string>

#include <zlink.h>

namespace zlink
{
class ctx_t;
struct spot_runtime_t;
}

namespace zlink
{
namespace spot_reqrep_internal
{
struct routing_pair_t
{
    std::string node_rid;
    std::string spot_rid;
};

ctx_t *resolve_spot_ctx (void *spot_);
spot_runtime_t *resolve_spot_runtime (void *spot_);
bool has_valid_routing_id (const zlink_routing_id_t *peer_rid_);
std::string routing_id_key (const zlink_routing_id_t *peer_rid_);
bool routing_id_from_key (const std::string &value_,
                          zlink_routing_id_t *out_);
void optional_routing_id_from_key (const std::string &value_,
                                   zlink_routing_id_t *out_);
bool resolve_spot_identity (void *spot_, routing_pair_t *out_);
}
}

#endif
