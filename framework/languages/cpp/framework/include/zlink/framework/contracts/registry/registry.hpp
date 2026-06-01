/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/framework/contracts/errors/result.hpp>
#include <zlink/framework/contracts/spots/spot.hpp>

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace zlink::framework
{

namespace detail
{
class registry_runtime_state_t;
class registry_runtime_t;
} // namespace detail

enum class registry_state_t
{
  stopped,
  running
};

enum class topology_source_t
{
  embedded,
  remote
};

enum class topology_state_t
{
  unknown,
  active,
  stale
};

enum class service_kind_t
{
  registry,
  channel,
  spot,
  stream
};

enum class service_role_t
{
  server,
  client,
  publisher,
  subscriber,
  spot_node,
  stream_endpoint
};

struct registry_options_snapshot_t
{
  std::string registry_id;
  std::string pub_endpoint;
  std::string router_endpoint;
  std::chrono::milliseconds heartbeat_interval { 1000 };
  std::chrono::milliseconds heartbeat_timeout { 3000 };
  std::chrono::milliseconds broadcast_interval { 1000 };
  std::vector<std::string> peer_pub_endpoints;
};

struct discovery_snapshot_t
{
  std::vector<std::string> registry_endpoints;
};

struct registry_status_t
{
  registry_state_t state = registry_state_t::stopped;
  std::string registry_id;
  std::string pub_endpoint;
  std::string router_endpoint;
  std::size_t peer_count = 0;
};

struct service_summary_entry_t
{
  std::string name;
  service_kind_t kind = service_kind_t::channel;
  service_role_t role = service_role_t::server;
  std::size_t instance_count = 0;
};

struct topology_entry_t
{
  std::string node_name;
  service_kind_t kind = service_kind_t::channel;
  service_role_t role = service_role_t::server;
  std::string name;
  topology_source_t source = topology_source_t::embedded;
  topology_state_t state = topology_state_t::active;
};

struct member_peer_t
{
  std::string channel_name;
  std::string node_name;
  std::string endpoint;
};

struct registry_monitoring_snapshot_t
{
  registry_state_t state = registry_state_t::stopped;
  std::size_t topology_count = 0;
  std::size_t service_count = 0;
  std::size_t spot_lookup_count = 0;
};

class registry_builder_t
{
public:
  registry_builder_t ();
  ~registry_builder_t ();

  registry_builder_t (registry_builder_t &&) noexcept;
  registry_builder_t &operator= (registry_builder_t &&) noexcept;
  registry_builder_t (const registry_builder_t &) = default;
  registry_builder_t &operator= (const registry_builder_t &) = default;

  registry_builder_t &registry_id (std::string registry_id);
  registry_builder_t &bind (std::string pub_endpoint,
                            std::string router_endpoint);
  registry_builder_t &heartbeat_interval (std::chrono::milliseconds value);
  registry_builder_t &heartbeat_timeout (std::chrono::milliseconds value);
  registry_builder_t &broadcast_interval (std::chrono::milliseconds value);
  registry_builder_t &add_peer (std::string pub_endpoint);

  registry_options_snapshot_t snapshot () const;

private:
  friend class zlink_builder_t;
  explicit registry_builder_t (
    std::shared_ptr<detail::registry_runtime_state_t> state);

  std::shared_ptr<detail::registry_runtime_state_t> _state;
};

class discovery_builder_t
{
public:
  discovery_builder_t ();
  ~discovery_builder_t ();

  discovery_builder_t (discovery_builder_t &&) noexcept;
  discovery_builder_t &operator= (discovery_builder_t &&) noexcept;
  discovery_builder_t (const discovery_builder_t &) = default;
  discovery_builder_t &operator= (const discovery_builder_t &) = default;

  discovery_builder_t &connect_registry (std::string endpoint);
  discovery_snapshot_t snapshot () const;

private:
  friend class zlink_builder_t;
  explicit discovery_builder_t (
    std::shared_ptr<detail::registry_runtime_state_t> state);

  std::shared_ptr<detail::registry_runtime_state_t> _state;
};

class registry_query_t
{
public:
  registry_query_t ();
  ~registry_query_t ();

  registry_query_t (registry_query_t &&) noexcept;
  registry_query_t &operator= (registry_query_t &&) noexcept;
  registry_query_t (const registry_query_t &) = default;
  registry_query_t &operator= (const registry_query_t &) = default;

  registry_status_t status () const;
  std::vector<service_summary_entry_t> service_summary () const;
  std::vector<topology_entry_t> topology () const;
  std::vector<member_peer_t> member_peers (std::string channel_name) const;
  registry_monitoring_snapshot_t monitoring_snapshot () const;
  result_t<spot_route_t> resolve_spot_remote_address (spot_rid_t spot_rid);

private:
  friend class zlink_builder_t;
  friend class detail::registry_runtime_t;
  explicit registry_query_t (
    std::shared_ptr<detail::registry_runtime_state_t> state);

  std::shared_ptr<detail::registry_runtime_state_t> _state;
};

} // namespace zlink::framework
