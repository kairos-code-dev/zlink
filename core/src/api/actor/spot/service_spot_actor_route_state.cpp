/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/actor/validation/service_spot_actor_validation_internal.hpp"
#include "services/spot/node/spot_node.hpp"
#include "services/spot/runtime/spot_handle.hpp"
#include "utils/routing_id.hpp"
#include "api/actor/spot/service_spot_actor_state_internal.hpp"

namespace
{

void fill_ref (const zlink::spot_actor_api_internal::actor_handle_t *actor_,
               zlink_actor_ref_t *out_)
{
    memset (out_, 0, sizeof (*out_));
    out_->node_rid = actor_->node_rid;
    strncpy (out_->actor_id, actor_->actor_id.c_str (), ZLINK_ACTOR_ID_MAX - 1);
    out_->generation = actor_->generation;
}

zlink_spot_kind_t spot_kind_for_state (
  const std::shared_ptr<spot_logical_state_t> &state_)
{
    if (!state_)
        return ZLINK_SPOT_KIND_INVALID;
    return state_->entry ? ZLINK_SPOT_KIND_ENTRY : ZLINK_SPOT_KIND_USER;
}

}

namespace zlink
{
namespace spot_actor_api_internal
{

using zlink::spot_actor_internal::same_routing_id;

bool actor_route_state_t::is_disconnected (
  zlink::spot_node_t *source_node_, const zlink_routing_id_t &target_rid_) const
{
    if (!source_node_)
        return true;
    zlink_routing_id_t source_rid;
    memset (&source_rid, 0, sizeof (source_rid));
    if (source_node_->node_routing_id (&source_rid) == 0
        && same_routing_id (source_rid, target_rid_))
        return false;
    return disconnected.count (
             std::make_pair (source_node_, routing_id_key (target_rid_)))
           != 0;
}

bool actor_route_state_t::active_matches (
  const actor_handle_t *actor_) const
{
    if (!actor_)
        return false;
    std::map<std::string, zlink_actor_route_t>::const_iterator it =
      active.find (actor_->actor_id);
    return it != active.end ()
           && same_routing_id (it->second.actor.node_rid, actor_->node_rid)
           && it->second.actor.generation == actor_->generation;
}

bool actor_route_state_t::active_exists (const actor_handle_t *actor_) const
{
    return actor_ && active.count (actor_->actor_id) != 0;
}

bool actor_route_state_t::find_active (
  const char *actor_id_, zlink_actor_route_t *route_out_) const
{
    std::map<std::string, zlink_actor_route_t>::const_iterator it =
      active.find (actor_id_);
    if (it == active.end ())
        return false;
    if (route_out_)
        *route_out_ = it->second;
    return true;
}

void actor_route_state_t::publish_active (actor_handle_t *actor_,
                                          bool create_)
{
    if (!actor_ || !actor_->node->actor_route_sync_enabled ())
        return;
    if (!create_ && !active_matches (actor_))
        return;

    zlink_actor_route_t route;
    memset (&route, 0, sizeof (route));
    fill_ref (actor_, &route.actor);
    if (actor_->joined_spot_state) {
        route.current_spot_rid = actor_->joined_spot_state->routing_id;
        route.current_spot_kind =
          spot_kind_for_state (actor_->joined_spot_state);
    }
    active[actor_->actor_id] = route;
    (void) actor_->node->bind_actor_route (actor_->actor_id.c_str (),
                                           &route, sizeof (route));
}

void actor_route_state_t::remove_matching_active (actor_handle_t *actor_)
{
    if (!actor_)
        return;
    std::map<std::string, zlink_actor_route_t>::iterator it =
      active.find (actor_->actor_id);
    if (it == active.end ())
        return;
    if (same_routing_id (it->second.actor.node_rid, actor_->node_rid)
        && it->second.actor.generation == actor_->generation) {
        active.erase (it);
        if (actor_->node->actor_route_sync_enabled ())
            (void) actor_->node->unbind_actor_route (actor_->actor_id.c_str ());
    }
}

void actor_route_state_t::erase_disconnected_for_node (
  zlink::spot_node_t *node_)
{
    for (std::set<std::pair<zlink::spot_node_t *, std::string> >::iterator it =
           disconnected.begin ();
         it != disconnected.end ();) {
        if (it->first == node_)
            it = disconnected.erase (it);
        else
            ++it;
    }
}

void actor_route_state_t::mark_disconnected (
  zlink::spot_node_t *node_, const zlink_routing_id_t &target_node_rid_)
{
    disconnected.insert (
      std::make_pair (node_, routing_id_key (target_node_rid_)));
}

}
}
