/* SPDX-License-Identifier: MPL-2.0 */

#include "registry_runtime.hpp"

#include <zlink/framework/contracts/configuration/zlink_builder.hpp>

#include "runtime/channels/channel_runtime.hpp"
#include "runtime/spots/spot_runtime.hpp"

#include <algorithm>
#include <utility>

namespace zlink::framework
{

registry_builder_t::registry_builder_t ()
  : _state (std::make_shared<detail::registry_runtime_state_t> ())
{
}

registry_builder_t::registry_builder_t (
  std::shared_ptr<detail::registry_runtime_state_t> state)
  : _state (std::move (state))
{
}

registry_builder_t::~registry_builder_t () = default;
registry_builder_t::registry_builder_t (registry_builder_t &&) noexcept =
  default;
registry_builder_t &registry_builder_t::operator= (
  registry_builder_t &&) noexcept = default;

registry_builder_t &
registry_builder_t::registry_id (std::string registry_id)
{
  _state->options.registry_id = std::move (registry_id);
  return *this;
}

registry_builder_t &
registry_builder_t::bind (std::string pub_endpoint,
                          std::string router_endpoint)
{
  _state->embedded_registry_enabled = true;
  _state->options.pub_endpoint = std::move (pub_endpoint);
  _state->options.router_endpoint = std::move (router_endpoint);
  return *this;
}

registry_builder_t &
registry_builder_t::heartbeat_interval (std::chrono::milliseconds value)
{
  _state->options.heartbeat_interval = value;
  return *this;
}

registry_builder_t &
registry_builder_t::heartbeat_timeout (std::chrono::milliseconds value)
{
  _state->options.heartbeat_timeout = value;
  return *this;
}

registry_builder_t &
registry_builder_t::broadcast_interval (std::chrono::milliseconds value)
{
  _state->options.broadcast_interval = value;
  return *this;
}

registry_builder_t &
registry_builder_t::add_peer (std::string pub_endpoint)
{
  _state->options.peer_pub_endpoints.push_back (std::move (pub_endpoint));
  return *this;
}

registry_options_snapshot_t
registry_builder_t::snapshot () const
{
  return _state->options;
}

discovery_builder_t::discovery_builder_t ()
  : _state (std::make_shared<detail::registry_runtime_state_t> ())
{
}

discovery_builder_t::discovery_builder_t (
  std::shared_ptr<detail::registry_runtime_state_t> state)
  : _state (std::move (state))
{
}

discovery_builder_t::~discovery_builder_t () = default;
discovery_builder_t::discovery_builder_t (discovery_builder_t &&) noexcept =
  default;
discovery_builder_t &discovery_builder_t::operator= (
  discovery_builder_t &&) noexcept = default;

discovery_builder_t &
discovery_builder_t::connect_registry (std::string endpoint)
{
  if (endpoint.empty ()) {
    throw framework_exception_t (
      framework_error_kind_t::request_protocol_error,
      "registry discovery endpoint is required");
  }
  _state->discovery.registry_endpoints.push_back (std::move (endpoint));
  return *this;
}

discovery_snapshot_t
discovery_builder_t::snapshot () const
{
  return _state->discovery;
}

registry_query_t::registry_query_t ()
  : _state (std::make_shared<detail::registry_runtime_state_t> ())
{
}

registry_query_t::registry_query_t (
  std::shared_ptr<detail::registry_runtime_state_t> state)
  : _state (std::move (state))
{
}

registry_query_t::~registry_query_t () = default;
registry_query_t::registry_query_t (registry_query_t &&) noexcept = default;
registry_query_t &registry_query_t::operator= (registry_query_t &&) noexcept =
  default;

registry_status_t
registry_query_t::status () const
{
  return registry_status_t {
    _state->embedded_registry_enabled ? registry_state_t::running
                                      : registry_state_t::stopped,
    _state->options.registry_id,
    _state->options.pub_endpoint,
    _state->options.router_endpoint,
    _state->options.peer_pub_endpoints.size () };
}

std::vector<service_summary_entry_t>
registry_query_t::service_summary () const
{
  return _state->services;
}

std::vector<topology_entry_t>
registry_query_t::topology () const
{
  return _state->topology;
}

std::vector<member_peer_t>
registry_query_t::member_peers (std::string channel_name) const
{
  std::vector<member_peer_t> result;
  for (const auto &peer : _state->member_peers) {
    if (peer.channel_name == channel_name) {
      result.push_back (peer);
    }
  }
  return result;
}

registry_monitoring_snapshot_t
registry_query_t::monitoring_snapshot () const
{
  return registry_monitoring_snapshot_t {
    _state->embedded_registry_enabled ? registry_state_t::running
                                      : registry_state_t::stopped,
    _state->topology.size (),
    _state->services.size (),
    _state->spot_lookup_count };
}

result_t<spot_route_t>
registry_query_t::resolve_spot_remote_address (spot_rid_t spot_rid)
{
  return detail::registry_runtime_t (_state).resolve_spot_remote_address (
    std::move (spot_rid));
}

zlink_builder_t &
zlink_builder_t::registry (
  std::function<void (registry_builder_t &)> configure)
{
  registry_builder_t builder (_state->registry_runtime);
  if (configure) {
    configure (builder);
  }
  return *this;
}

zlink_builder_t &
zlink_builder_t::discovery (
  std::function<void (discovery_builder_t &)> configure)
{
  discovery_builder_t builder (_state->registry_runtime);
  if (configure) {
    configure (builder);
  }
  return *this;
}

zlink_builder_t &
zlink_builder_t::route_channel (std::string route_channel_name)
{
  if (route_channel_name.empty ()) {
    throw framework_exception_t (
      framework_error_kind_t::request_protocol_error,
      "route channel name is required");
  }
  _state->registry_runtime->route_channels.push_back (
    std::move (route_channel_name));
  return *this;
}

registry_options_snapshot_t
zlink_builder_t::registry_options () const
{
  return _state->registry_runtime->options;
}

discovery_snapshot_t
zlink_builder_t::discovery_options () const
{
  return _state->registry_runtime->discovery;
}

std::vector<std::string>
zlink_builder_t::route_channels () const
{
  return _state->registry_runtime->route_channels;
}

result_t<void>
zlink_builder_t::validate_registry () const
{
  return detail::registry_runtime_t (_state->registry_runtime).validate (
    *_state);
}

registry_query_t
zlink_builder_t::registry_query () const
{
  detail::registry_runtime_t runtime (_state->registry_runtime);
  runtime.project_topology (*_state);
  return registry_query_t (_state->registry_runtime);
}

} // namespace zlink::framework

namespace zlink::framework::detail
{

registry_runtime_t::registry_runtime_t (
  std::shared_ptr<registry_runtime_state_t> state)
  : _state (std::move (state))
{
}

registry_runtime_t
registry_runtime_t::from (const registry_query_t &query)
{
  return registry_runtime_t (query._state);
}

result_t<void>
registry_runtime_t::validate (const zlink_builder_state_t &builder) const
{
  if (auto registry = validate_embedded_registry (); !registry) {
    return registry;
  }
  return validate_spot_remote_lookup (builder);
}

result_t<void>
registry_runtime_t::validate_embedded_registry () const
{
  if (!_state->embedded_registry_enabled) {
    return result_t<void>::success ();
  }
  if (_state->options.pub_endpoint.empty () ||
      _state->options.router_endpoint.empty ()) {
    return result_t<void>::failure (
      framework_error_kind_t::request_protocol_error,
      "embedded registry requires pub and router endpoints");
  }
  if (_state->options.heartbeat_interval.count () <= 0 ||
      _state->options.heartbeat_timeout.count () <= 0 ||
      _state->options.broadcast_interval.count () <= 0) {
    return result_t<void>::failure (
      framework_error_kind_t::request_protocol_error,
      "embedded registry intervals must be positive");
  }
  return result_t<void>::success ();
}

result_t<void>
registry_runtime_t::validate_spot_remote_lookup (
  const zlink_builder_state_t &builder) const
{
  for (const auto &[_, spot_node] : builder.spot_nodes) {
    if (!spot_node->snapshot.registry_spot_remote_addresses_enabled) {
      continue;
    }
    if (_state->discovery.registry_endpoints.empty ()) {
      return result_t<void>::failure (
        framework_error_kind_t::request_protocol_error,
        "registry spot remote address resolver requires discovery endpoints");
    }
    if (_state->route_channels.empty ()) {
      return result_t<void>::failure (
        framework_error_kind_t::request_protocol_error,
        "registry spot remote address resolver requires a route channel");
    }
    const auto route_channel =
      resolve_registry_route_channel (builder, spot_node->snapshot);
    if (!route_channel) {
      return result_t<void>::failure (
        framework_error_kind_t::request_protocol_error,
        "registry spot remote address resolver requires an explicit route channel when there is not exactly one route channel");
    }
  }

  return result_t<void>::success ();
}

std::optional<std::string>
registry_runtime_t::resolve_registry_route_channel (
  const zlink_builder_state_t &builder,
  const spot_node_snapshot_t &spot_node) const
{
  (void) builder;
  if (spot_node.registry_spot_route_channel) {
    const auto found = std::find (_state->route_channels.begin (),
                                  _state->route_channels.end (),
                                  *spot_node.registry_spot_route_channel);
    if (found == _state->route_channels.end ()) {
      return std::nullopt;
    }
    return *spot_node.registry_spot_route_channel;
  }
  if (_state->route_channels.size () != 1) {
    return std::nullopt;
  }
  return _state->route_channels.front ();
}

void
registry_runtime_t::project_topology (const zlink_builder_state_t &builder)
{
  _state->services.clear ();
  _state->topology.clear ();
  _state->member_peers.clear ();

  for (const auto &[name, channel] : builder.runtime->channels) {
    project_channel (builder, name, channel);
  }

  for (const auto &[_, spot_node] : builder.spot_nodes) {
    project_spot_node (builder, spot_node->snapshot);
  }
}

void
registry_runtime_t::project_channel (const zlink_builder_state_t &builder,
                                     const std::string &name,
                                     const channel_snapshot_t &channel)
{
  auto add_capability = [&](const channel_capability_snapshot_t &capability,
                            service_role_t role) {
    if (!capability.enabled) {
      return;
    }
    _state->services.push_back (
      service_summary_entry_t { name, service_kind_t::channel, role, 1 });
    _state->topology.push_back (topology_entry_t {
      builder.node_name,
      service_kind_t::channel,
      role,
      name,
      topology_source_t::embedded,
      topology_state_t::active });
    for (const auto &endpoint : capability.connect_endpoints) {
      _state->member_peers.push_back (
        member_peer_t { name, builder.node_name, endpoint });
    }
    for (const auto &endpoint : capability.bind_endpoints) {
      _state->member_peers.push_back (
        member_peer_t { name, builder.node_name, endpoint });
    }
  };
  add_capability (channel.server, service_role_t::server);
  add_capability (channel.client, service_role_t::client);
  add_capability (channel.publisher, service_role_t::publisher);
  add_capability (channel.subscriber, service_role_t::subscriber);
}

void
registry_runtime_t::project_spot_node (const zlink_builder_state_t &builder,
                                       const spot_node_snapshot_t &spot_node)
{
  _state->services.push_back (service_summary_entry_t {
    spot_node.name,
    service_kind_t::spot,
    service_role_t::spot_node,
    spot_node.spot_names.size () });
  _state->topology.push_back (topology_entry_t {
    builder.node_name,
    service_kind_t::spot,
    service_role_t::spot_node,
    spot_node.name,
    topology_source_t::embedded,
    topology_state_t::active });
}

void
registry_runtime_t::add_spot_route (spot_route_t route)
{
  _state->spot_routes[std::string (route.spot_rid.value ())] =
    std::move (route);
}

result_t<spot_route_t>
registry_runtime_t::resolve_spot_remote_address (spot_rid_t spot_rid)
{
  ++_state->spot_lookup_count;
  const auto found = _state->spot_routes.find (std::string (spot_rid.value ()));
  if (found == _state->spot_routes.end ()) {
    return result_t<spot_route_t>::failure (
      framework_error_kind_t::spot_route_not_found,
      "registry spot route was not found");
  }
  return result_t<spot_route_t>::success (found->second);
}

} // namespace zlink::framework::detail
