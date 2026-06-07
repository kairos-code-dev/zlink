/* SPDX-License-Identifier: MPL-2.0 */

#ifndef ZLINK_SERVICE_SPOT_ACTOR_VALIDATION_INTERNAL_HPP_INCLUDED
#define ZLINK_SERVICE_SPOT_ACTOR_VALIDATION_INTERNAL_HPP_INCLUDED

#include "zlink.h"

namespace zlink
{
namespace spot_actor_internal
{

bool same_routing_id (const zlink_routing_id_t &lhs_, const zlink_routing_id_t &rhs_);
bool valid_routing_id (const zlink_routing_id_t *rid_);
bool valid_actor_id (const char *actor_id_);
bool same_actor_ref_identity (const zlink_actor_ref_t &lhs_, const zlink_actor_ref_t &rhs_);

}
}

#endif
