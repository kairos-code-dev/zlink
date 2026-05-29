/* SPDX-License-Identifier: MPL-2.0 */

#pragma once

namespace zlink
{
namespace spot_actor_api_internal
{

struct actor_spot_snapshot_t
{
    actor_spot_snapshot_t () : facade (NULL) {}

    spot_handle_t *facade;
    std::shared_ptr<spot_logical_state_t> state;
};

struct actor_node_registry_t
{
    void register_spot (spot_handle_t *spot_);
    void erase_spot (spot_handle_t *spot_);
    spot_handle_t *find_spot_for_state (
      zlink::spot_node_t *node_,
      const std::shared_ptr<spot_logical_state_t> &state_) const;
    spot_handle_t *find_replacement_spot (
      spot_handle_t *spot_,
      const std::shared_ptr<spot_logical_state_t> &state_) const;
    bool has_peer_spot_facade (spot_handle_t *spot_) const;
    bool collect_spots_for_node (
      zlink::spot_node_t *node_,
      const std::shared_ptr<spot_logical_state_t> &entry_state_,
      std::vector<actor_spot_snapshot_t> *out_) const;
    void register_node (zlink::spot_node_t *node_,
                        const zlink_routing_id_t &node_rid_);
    void erase_node (zlink::spot_node_t *node_);
    void erase_node_routes (zlink::spot_node_t *node_);
    void erase_known_node (zlink::spot_node_t *node_);
    zlink::spot_node_t *resolve_node_by_rid (
      const zlink_routing_id_t &rid_) const;
    bool known_node (zlink::spot_node_t *node_) const;
    zlink::spot_node_t *find_socket_owner (zlink::socket_base_t *socket_) const;
    void collect_actor_handles (std::vector<actor_handle_t *> *out_) const;
    actor_handle_t *find_unique_actor_by_id (const char *actor_id_,
                                             bool include_pending_) const;

    std::map<std::string, zlink::spot_node_t *> nodes_by_rid;
    std::set<spot_handle_t *> known_spots;
    std::set<zlink::spot_node_t *> known_nodes;
};

}
}
