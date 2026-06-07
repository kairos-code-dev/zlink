/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/spot/node/spot_node.hpp"
#include "services/spot/node/spot_node_access.hpp"
#include "services/spot/runtime/spot_handle.hpp"
#include "utils/routing_id.hpp"
#include "api/actor/spot/service_spot_actor_state_internal.hpp"

namespace
{

bool same_logical_spot (const spot_handle_t *lhs_, const spot_handle_t *rhs_)
{
    if (!lhs_ || !rhs_)
        return false;
    if (lhs_ == rhs_)
        return true;
    return lhs_->logical_state && lhs_->logical_state == rhs_->logical_state;
}

}

namespace zlink
{
namespace spot_actor_api_internal
{

void actor_node_registry_t::register_spot (spot_handle_t *spot_)
{
    if (spot_)
        known_spots.insert (spot_);
}

void actor_node_registry_t::erase_spot (spot_handle_t *spot_)
{
    known_spots.erase (spot_);
}

spot_handle_t *actor_node_registry_t::find_spot_for_state (
  zlink::spot_node_t *node_, const std::shared_ptr<spot_logical_state_t> &state_) const
{
    if (!node_ || !state_)
        return NULL;
    for (std::set<spot_handle_t *>::const_iterator it = known_spots.begin ();
         it != known_spots.end (); ++it) {
        if ((*it)->node == node_ && (*it)->logical_state == state_)
            return *it;
    }
    return NULL;
}

spot_handle_t *actor_node_registry_t::find_replacement_spot (
  spot_handle_t *spot_, const std::shared_ptr<spot_logical_state_t> &state_) const
{
    if (!spot_ || !state_)
        return NULL;
    for (std::set<spot_handle_t *>::const_iterator it = known_spots.begin ();
         it != known_spots.end (); ++it) {
        if (*it != spot_ && (*it)->logical_state == state_)
            return *it;
    }
    return NULL;
}

bool actor_node_registry_t::has_peer_spot_facade (spot_handle_t *spot_) const
{
    if (!spot_)
        return false;
    for (std::set<spot_handle_t *>::const_iterator it = known_spots.begin ();
         it != known_spots.end (); ++it) {
        if (*it != spot_ && same_logical_spot (*it, spot_))
            return true;
    }
    return false;
}

bool actor_node_registry_t::collect_spots_for_node (
  zlink::spot_node_t *node_,
  const std::shared_ptr<spot_logical_state_t> &entry_state_,
  std::vector<actor_spot_snapshot_t> *out_) const
{
    if (!node_ || !out_)
        return false;
    out_->clear ();
    bool has_entry_facade = false;
    for (std::set<spot_handle_t *>::const_iterator it = known_spots.begin ();
         it != known_spots.end (); ++it) {
        if ((*it)->node != node_)
            continue;
        actor_spot_snapshot_t row;
        row.facade = *it;
        row.state = (*it)->logical_state;
        out_->push_back (row);
        if (entry_state_ && (*it)->logical_state == entry_state_)
            has_entry_facade = true;
    }
    if (entry_state_ && !has_entry_facade) {
        actor_spot_snapshot_t row;
        row.state = entry_state_;
        out_->push_back (row);
    }
    return true;
}

void actor_node_registry_t::register_node (zlink::spot_node_t *node_,
                                           const zlink_routing_id_t &node_rid_)
{
    if (!node_)
        return;
    known_nodes.insert (node_);
    nodes_by_rid[routing_id_key (node_rid_)] = node_;
}

void actor_node_registry_t::erase_node (zlink::spot_node_t *node_)
{
    erase_node_routes (node_);
    erase_known_node (node_);
}

void actor_node_registry_t::erase_node_routes (zlink::spot_node_t *node_)
{
    if (!node_)
        return;
    for (std::map<std::string, zlink::spot_node_t *>::iterator it = nodes_by_rid.begin ();
         it != nodes_by_rid.end ();) {
        if (it->second == node_)
            it = nodes_by_rid.erase (it);
        else
            ++it;
    }
}

void actor_node_registry_t::erase_known_node (zlink::spot_node_t *node_)
{
    known_nodes.erase (node_);
}

zlink::spot_node_t *
actor_node_registry_t::resolve_node_by_rid (const zlink_routing_id_t &rid_) const
{
    std::map<std::string, zlink::spot_node_t *>::const_iterator it =
      nodes_by_rid.find (routing_id_key (rid_));
    return it == nodes_by_rid.end () ? NULL : it->second;
}

bool actor_node_registry_t::known_node (zlink::spot_node_t *node_) const
{
    return node_ && known_nodes.count (node_) != 0;
}

zlink::spot_node_t *actor_node_registry_t::find_socket_owner (zlink::socket_base_t *socket_) const
{
    if (!socket_)
        return NULL;
    for (std::set<zlink::spot_node_t *>::const_iterator node_it = known_nodes.begin ();
         node_it != known_nodes.end (); ++node_it) {
        if (zlink::spot_node_access_t::owns_socket (*node_it, socket_))
            return *node_it;
    }
    return NULL;
}

void actor_node_registry_t::collect_actor_handles (std::vector<actor_handle_t *> *out_) const
{
    if (!out_)
        return;
    out_->clear ();
    for (std::set<zlink::spot_node_t *>::const_iterator node_it = known_nodes.begin ();
         node_it != known_nodes.end (); ++node_it) {
        const std::set<actor_handle_t *> &node_actors =
          zlink::spot_node_access_t::actor_handles (*node_it);
        out_->insert (out_->end (), node_actors.begin (), node_actors.end ());
    }
}

actor_handle_t *actor_node_registry_t::find_unique_actor_by_id (const char *actor_id_,
                                                                bool include_pending_) const
{
    actor_handle_t *match = NULL;
    for (std::set<zlink::spot_node_t *>::const_iterator node_it = known_nodes.begin ();
         node_it != known_nodes.end (); ++node_it) {
        std::map<std::string, actor_handle_t *> &actors =
          zlink::spot_node_access_t::actors_by_id (*node_it);
        std::map<std::string, actor_handle_t *>::iterator actor_it = actors.find (actor_id_);
        if (actor_it == actors.end ()
            || (!include_pending_ && actor_it->second->pending_remote_join))
            continue;

        if (match && match != actor_it->second) {
            errno = EBUSY;
            return NULL;
        }
        match = actor_it->second;
    }

    if (!match)
        errno = ENOENT;
    return match;
}

}
}
