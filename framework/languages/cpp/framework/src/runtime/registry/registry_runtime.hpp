/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/framework/contracts/registry/registry.hpp>

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace zlink::framework::detail
{

class zlink_builder_state_t;

class registry_runtime_state_t
{
  public:
    registry_options_snapshot_t options;
    discovery_snapshot_t discovery;
    std::vector<std::string> route_channels;
    std::vector<service_summary_entry_t> services;
    std::vector<topology_entry_t> topology;
    std::vector<member_peer_t> member_peers;
    std::map<std::string, spot_route_t> spot_routes;
    std::size_t spot_lookup_count = 0;
    bool embedded_registry_enabled = false;
};

class registry_runtime_t
{
  public:
    explicit registry_runtime_t (std::shared_ptr<registry_runtime_state_t> state);

    static registry_runtime_t from (const registry_query_t &query);

    result_t<void> validate (const zlink_builder_state_t &builder) const;
    void project_topology (const zlink_builder_state_t &builder);
    void add_spot_route (spot_route_t route);
    void cleanup_stale_spot_routes (const std::set<std::string> &active_spot_rids);
    result_t<spot_route_t> resolve_spot_remote_address (spot_rid_t spot_rid);

  private:
    result_t<void> validate_embedded_registry () const;
    result_t<void> validate_spot_remote_lookup (const zlink_builder_state_t &builder) const;
    std::optional<std::string>
    resolve_registry_route_channel (const zlink_builder_state_t &builder,
                                    const spot_node_snapshot_t &spot_node) const;
    void project_channel (const zlink_builder_state_t &builder,
                          const std::string &name,
                          const channel_snapshot_t &channel);
    void project_spot_node (const zlink_builder_state_t &builder,
                            const spot_node_snapshot_t &spot_node);

    std::shared_ptr<registry_runtime_state_t> _state;
};

} // namespace zlink::framework::detail
