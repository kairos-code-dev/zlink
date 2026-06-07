/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/actor/validation/service_spot_actor_validation_internal.hpp"

#include <string.h>

namespace zlink
{
namespace spot_actor_internal
{

bool same_routing_id (const zlink_routing_id_t &lhs_, const zlink_routing_id_t &rhs_)
{
    return lhs_.size == rhs_.size && memcmp (lhs_.data, rhs_.data, lhs_.size) == 0;
}

bool valid_routing_id (const zlink_routing_id_t *rid_)
{
    return rid_ && rid_->size > 0 && rid_->size <= sizeof (rid_->data);
}

bool valid_actor_id (const char *actor_id_)
{
    if (!actor_id_ || actor_id_[0] == '\0')
        return false;
    return strnlen (actor_id_, ZLINK_ACTOR_ID_MAX) < ZLINK_ACTOR_ID_MAX;
}

bool same_actor_ref_identity (const zlink_actor_ref_t &lhs_, const zlink_actor_ref_t &rhs_)
{
    return valid_actor_id (lhs_.actor_id) && valid_actor_id (rhs_.actor_id)
           && valid_routing_id (&lhs_.node_rid) && valid_routing_id (&rhs_.node_rid)
           && strcmp (lhs_.actor_id, rhs_.actor_id) == 0
           && same_routing_id (lhs_.node_rid, rhs_.node_rid);
}

}
}
