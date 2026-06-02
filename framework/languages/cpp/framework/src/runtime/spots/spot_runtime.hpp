/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/framework/contracts/spots/spot.hpp>

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace zlink::framework::detail
{

class spot_node_builder_state_t
{
public:
  explicit spot_node_builder_state_t (std::string name)
    : snapshot { .name = std::move (name) }
  {
  }

  spot_node_snapshot_t snapshot;
  std::map<std::string, std::type_index> spot_factories;
  std::map<std::string, spot_rid_t> spot_rids_by_name;
  std::map<std::string, std::string> spot_names_by_rid;
  std::map<std::string, std::type_index> actor_factories;
  std::map<std::string, std::function<std::optional<spot_route_t> (spot_rid_t)>>
    resolvers;
  std::uint64_t next_spot_id = 1;
};

class spot_context_state_t
{
public:
  std::shared_ptr<spot_node_builder_state_t> node;
  node_rid_t node_rid;
  spot_rid_t spot_rid;
  std::string spot_name;
  std::vector<spot_packet_descriptor_t> packets;
  std::vector<spot_handler_descriptor_t> handlers;
  std::vector<spot_handler_registry_t::invoker_t> handler_invokers;
  std::vector<std::string> ordering_log;
  std::vector<std::shared_ptr<timer_state_t>> timers;
};

class spot_node_runtime_t
{
public:
  explicit spot_node_runtime_t (
    std::shared_ptr<spot_node_builder_state_t> state);

  static spot_node_runtime_t from (const spot_node_builder_t &builder);

  spot_context_t create_spot (std::string spot_name);
  std::optional<std::string> spot_name_for (spot_rid_t spot_rid) const;
  std::optional<spot_route_t> resolve_spot (spot_rid_t spot_rid) const;
  const std::vector<std::string> &ordering_log (const spot_context_t &context)
    const;

private:
  std::shared_ptr<spot_node_builder_state_t> _state;
};

} // namespace zlink::framework::detail
