/* SPDX-License-Identifier: MPL-2.0 */

#pragma once

struct actor_route_state_t
{
    bool is_disconnected (zlink::spot_node_t *source_node_,
                          const zlink_routing_id_t &target_rid_) const;
    bool active_matches (const actor_handle_t *actor_) const;
    bool active_exists (const actor_handle_t *actor_) const;
    bool find_active (const char *actor_id_, zlink_actor_route_t *route_out_)
      const;
    void publish_active (actor_handle_t *actor_, bool create_);
    void remove_matching_active (actor_handle_t *actor_);
    void erase_disconnected_for_node (zlink::spot_node_t *node_);
    void mark_disconnected (zlink::spot_node_t *node_,
                            const zlink_routing_id_t &target_node_rid_);

    std::map<std::string, zlink_actor_route_t> active;
    std::set<std::pair<zlink::spot_node_t *, std::string> > disconnected;
};
