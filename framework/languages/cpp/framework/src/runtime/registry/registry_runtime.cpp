/* SPDX-License-Identifier: MPL-2.0 */

#include "registry_runtime.hpp"

#include <zlink/framework/contracts/configuration/zlink_builder.hpp>

#include "runtime/channels/channel_runtime.hpp"
#include "runtime/diagnostics/monitoring_runtime.hpp"
#include "runtime/spots/spot_runtime.hpp"

#include <zlink/Contracts/Core/context.hpp>
#include <zlink/Contracts/Errors/errors.hpp>
#include <zlink/Contracts/Service/registry.hpp>

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace zlink::framework
{

namespace
{

zlink::service_kind_t to_native_service_kind (service_kind_t kind)
{
    switch (kind) {
        case service_kind_t::registry:
            return zlink::service_kind_t::discovery;
        case service_kind_t::spot:
            return zlink::service_kind_t::spot_pub;
        case service_kind_t::channel:
        case service_kind_t::stream:
            return zlink::service_kind_t::socket;
    }
    return zlink::service_kind_t::socket;
}

zlink::service_role_t to_native_service_role (service_role_t role)
{
    switch (role) {
        case service_role_t::server:
            return zlink::service_role_t::router;
        case service_role_t::client:
            return zlink::service_role_t::dealer;
        case service_role_t::publisher:
            return zlink::service_role_t::pub;
        case service_role_t::subscriber:
            return zlink::service_role_t::sub;
        case service_role_t::spot_node:
            return zlink::service_role_t::spot;
        case service_role_t::stream_endpoint:
            return zlink::service_role_t::invalid;
    }
    return zlink::service_role_t::invalid;
}

zlink::topology_source_t to_native_topology_source (topology_source_t source)
{
    switch (source) {
        case topology_source_t::embedded:
            return zlink::topology_source_t::manual;
        case topology_source_t::remote:
            return zlink::topology_source_t::registry;
    }
    return zlink::topology_source_t::manual;
}

zlink::topology_state_t to_native_topology_state (topology_state_t state)
{
    switch (state) {
        case topology_state_t::unknown:
            return zlink::topology_state_t::discovered;
        case topology_state_t::active:
            return zlink::topology_state_t::ready;
        case topology_state_t::stale:
            return zlink::topology_state_t::lost;
    }
    return zlink::topology_state_t::discovered;
}

service_kind_t from_native_service_kind (zlink::service_kind_t kind)
{
    switch (kind) {
        case zlink::service_kind_t::discovery:
            return service_kind_t::registry;
        case zlink::service_kind_t::spot_pub:
        case zlink::service_kind_t::spot_sub:
            return service_kind_t::spot;
        case zlink::service_kind_t::socket:
            return service_kind_t::channel;
    }
    return service_kind_t::channel;
}

service_role_t from_native_service_role (zlink::service_role_t role)
{
    switch (role) {
        case zlink::service_role_t::spot:
            return service_role_t::spot_node;
        case zlink::service_role_t::router:
            return service_role_t::server;
        case zlink::service_role_t::dealer:
            return service_role_t::client;
        case zlink::service_role_t::pub:
            return service_role_t::publisher;
        case zlink::service_role_t::sub:
            return service_role_t::subscriber;
        case zlink::service_role_t::invalid:
            return service_role_t::server;
    }
    return service_role_t::server;
}

topology_source_t from_native_topology_source (zlink::topology_source_t source)
{
    switch (source) {
        case zlink::topology_source_t::manual:
            return topology_source_t::embedded;
        case zlink::topology_source_t::discovery:
        case zlink::topology_source_t::registry:
            return topology_source_t::remote;
    }
    return topology_source_t::embedded;
}

topology_state_t from_native_topology_state (zlink::topology_state_t state)
{
    switch (state) {
        case zlink::topology_state_t::ready:
        case zlink::topology_state_t::connecting:
            return topology_state_t::active;
        case zlink::topology_state_t::lost:
        case zlink::topology_state_t::error:
        case zlink::topology_state_t::stopped:
            return topology_state_t::stale;
        case zlink::topology_state_t::discovered:
            return topology_state_t::unknown;
    }
    return topology_state_t::unknown;
}

registry_state_t from_native_registry_state (zlink::registry_state_t state)
{
    switch (state) {
        case zlink::registry_state_t::active:
        case zlink::registry_state_t::degraded:
            return registry_state_t::running;
        case zlink::registry_state_t::idle:
        case zlink::registry_state_t::error:
            return registry_state_t::stopped;
    }
    return registry_state_t::stopped;
}

zlink::registry_topology_filter_t to_native_filter (const topology_filter_t &filter)
{
    zlink::registry_topology_filter_t native;
    if (filter.name) {
        native.channel_name (*filter.name);
    }
    if (filter.kind) {
        native.service_kind (to_native_service_kind (*filter.kind));
    }
    if (filter.role) {
        native.service_role (to_native_service_role (*filter.role));
    }
    if (filter.source) {
        native.source (to_native_topology_source (*filter.source));
    }
    if (filter.state) {
        native.state (to_native_topology_state (*filter.state));
    }
    if (filter.routing_id) {
        native.routing_id (*filter.routing_id);
    }
    return native;
}

topology_entry_t from_native_topology_entry (const zlink::registry_topology_entry_t &entry)
{
    return topology_entry_t{{},
                            from_native_service_kind (entry.service_kind ()),
                            from_native_service_role (entry.service_role ()),
                            entry.channel_name (),
                            from_native_topology_source (entry.source ()),
                            from_native_topology_state (entry.state ()),
                            entry.endpoint (),
                            entry.routing_id ()};
}

member_peer_t from_native_member_peer_entry (const zlink::member_peer_entry_t &entry)
{
    return member_peer_t{entry.channel_name (),
                         entry.routing_id () ? entry.routing_id ()->to_string () : std::string (),
                         entry.endpoint ()};
}

registry_status_t from_native_registry_status (const zlink::registry_status_t &status)
{
    return registry_status_t{from_native_registry_state (status.state ()),
                             std::to_string (status.registry_id ()),
                             std::string (),
                             status.bind_endpoint (),
                             status.peer_registry_count (),
                             status.connected_peer_registry_count ()};
}

framework_error_kind_t map_registry_client_error (const std::exception &ex)
{
    if (dynamic_cast<const zlink::connect_error_t *> (&ex) != nullptr) {
        return framework_error_kind_t::disconnected;
    }
    if (dynamic_cast<const zlink::config_error_t *> (&ex) != nullptr) {
        return framework_error_kind_t::request_failed;
    }
    if (dynamic_cast<const std::invalid_argument *> (&ex) != nullptr) {
        return framework_error_kind_t::request_protocol_error;
    }
    return framework_error_kind_t::request_failed;
}

bool matches_service_summary_filter (const service_summary_entry_t &entry,
                                     const service_summary_filter_t &filter)
{
    if (filter.name && entry.name != *filter.name) {
        return false;
    }
    if (filter.kind && entry.kind != *filter.kind) {
        return false;
    }
    if (filter.role && entry.role != *filter.role) {
        return false;
    }
    return true;
}

bool matches_topology_filter (const topology_entry_t &entry, const topology_filter_t &filter)
{
    if (filter.node_name && entry.node_name != *filter.node_name) {
        return false;
    }
    if (filter.name && entry.name != *filter.name) {
        return false;
    }
    if (filter.kind && entry.kind != *filter.kind) {
        return false;
    }
    if (filter.role && entry.role != *filter.role) {
        return false;
    }
    if (filter.source && entry.source != *filter.source) {
        return false;
    }
    if (filter.state && entry.state != *filter.state) {
        return false;
    }
    if (filter.routing_id && (!entry.routing_id || *entry.routing_id != *filter.routing_id)) {
        return false;
    }
    return true;
}

} // namespace

registry_builder_t::registry_builder_t () :
    _state (std::make_shared<detail::registry_runtime_state_t> ())
{
}

registry_builder_t::registry_builder_t (std::shared_ptr<detail::registry_runtime_state_t> state) :
    _state (std::move (state))
{
}

registry_builder_t::~registry_builder_t () = default;
registry_builder_t::registry_builder_t (registry_builder_t &&) noexcept = default;
registry_builder_t &registry_builder_t::operator= (registry_builder_t &&) noexcept = default;

registry_builder_t &registry_builder_t::registry_id (std::string registry_id)
{
    _state->options.registry_id = std::move (registry_id);
    return *this;
}

registry_builder_t &registry_builder_t::bind (std::string pub_endpoint, std::string router_endpoint)
{
    _state->embedded_registry_enabled = true;
    _state->options.pub_endpoint = std::move (pub_endpoint);
    _state->options.router_endpoint = std::move (router_endpoint);
    return *this;
}

registry_builder_t &registry_builder_t::heartbeat_interval (std::chrono::milliseconds value)
{
    _state->options.heartbeat_interval = value;
    return *this;
}

registry_builder_t &registry_builder_t::heartbeat_timeout (std::chrono::milliseconds value)
{
    _state->options.heartbeat_timeout = value;
    return *this;
}

registry_builder_t &registry_builder_t::broadcast_interval (std::chrono::milliseconds value)
{
    _state->options.broadcast_interval = value;
    return *this;
}

registry_builder_t &registry_builder_t::add_peer (std::string pub_endpoint)
{
    _state->options.peer_pub_endpoints.push_back (std::move (pub_endpoint));
    return *this;
}

registry_options_snapshot_t registry_builder_t::snapshot () const
{
    return _state->options;
}

discovery_builder_t::discovery_builder_t () :
    _state (std::make_shared<detail::registry_runtime_state_t> ())
{
}

discovery_builder_t::discovery_builder_t (std::shared_ptr<detail::registry_runtime_state_t> state) :
    _state (std::move (state))
{
}

discovery_builder_t::~discovery_builder_t () = default;
discovery_builder_t::discovery_builder_t (discovery_builder_t &&) noexcept = default;
discovery_builder_t &discovery_builder_t::operator= (discovery_builder_t &&) noexcept = default;

discovery_builder_t &discovery_builder_t::connect_registry (std::string endpoint)
{
    if (endpoint.empty ()) {
        throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                     "registry discovery endpoint is required");
    }
    _state->discovery.registry_endpoints.push_back (std::move (endpoint));
    return *this;
}

discovery_snapshot_t discovery_builder_t::snapshot () const
{
    return _state->discovery;
}

registry_query_t::registry_query_t () :
    _state (std::make_shared<detail::registry_runtime_state_t> ())
{
}

registry_query_t::registry_query_t (std::shared_ptr<detail::registry_runtime_state_t> state) :
    _state (std::move (state))
{
}

registry_query_t::~registry_query_t () = default;
registry_query_t::registry_query_t (registry_query_t &&) noexcept = default;
registry_query_t &registry_query_t::operator= (registry_query_t &&) noexcept = default;

registry_status_t registry_query_t::status () const
{
    return registry_status_t{_state->embedded_registry_enabled ? registry_state_t::running
                                                               : registry_state_t::stopped,
                             _state->options.registry_id,
                             _state->options.pub_endpoint,
                             _state->options.router_endpoint,
                             _state->options.peer_pub_endpoints.size (),
                             _state->options.peer_pub_endpoints.size ()};
}

std::vector<service_summary_entry_t> registry_query_t::service_summary () const
{
    return _state->services;
}

std::vector<service_summary_entry_t>
registry_query_t::service_summary (const service_summary_filter_t &filter) const
{
    std::vector<service_summary_entry_t> result;
    for (const auto &entry : _state->services) {
        if (matches_service_summary_filter (entry, filter)) {
            result.push_back (entry);
        }
    }
    return result;
}

std::vector<topology_entry_t> registry_query_t::topology () const
{
    return _state->topology;
}

std::vector<topology_entry_t> registry_query_t::topology (const topology_filter_t &filter) const
{
    std::vector<topology_entry_t> result;
    for (const auto &entry : _state->topology) {
        if (matches_topology_filter (entry, filter)) {
            result.push_back (entry);
        }
    }
    return result;
}

std::vector<member_peer_t> registry_query_t::member_peers (std::string channel_name) const
{
    std::vector<member_peer_t> result;
    for (const auto &peer : _state->member_peers) {
        if (peer.channel_name == channel_name) {
            result.push_back (peer);
        }
    }
    return result;
}

registry_monitoring_snapshot_t registry_query_t::monitoring_snapshot () const
{
    return registry_monitoring_snapshot_t{
      _state->embedded_registry_enabled ? registry_state_t::running : registry_state_t::stopped,
      _state->topology.size (), _state->services.size (), _state->spot_lookup_count};
}

result_t<spot_route_t> registry_query_t::resolve_spot_remote_address (spot_rid_t spot_rid)
{
    return detail::registry_runtime_t (_state).resolve_spot_remote_address (std::move (spot_rid));
}

class registry_query_client_t::impl
{
  public:
    std::unique_ptr<zlink::context_t> context;
    std::unique_ptr<zlink::service::registry_query_client_t> client;
};

registry_query_client_t::registry_query_client_t () : _impl (std::make_unique<impl> ())
{
}

registry_query_client_t::registry_query_client_t (registry_query_client_options_t options) :
    registry_query_client_t ()
{
    auto connected = connect (std::move (options));
    if (!connected) {
        connected.value ();
    }
}

registry_query_client_t::~registry_query_client_t () = default;
registry_query_client_t::registry_query_client_t (registry_query_client_t &&) noexcept = default;
registry_query_client_t &
registry_query_client_t::operator= (registry_query_client_t &&) noexcept = default;

result_t<void> registry_query_client_t::connect (registry_query_client_options_t options)
{
    if (options.endpoint.empty ()) {
        return result_t<void>::failure (framework_error_kind_t::request_protocol_error,
                                        "registry query client endpoint is required");
    }
    try {
        auto context = std::make_unique<zlink::context_t> ();
        auto client = std::make_unique<zlink::service::registry_query_client_t> (*context);
        client->connect (options.endpoint);
        _impl->client = std::move (client);
        _impl->context = std::move (context);
        return result_t<void>::success ();
    }
    catch (const std::exception &ex) {
        return result_t<void>::failure (map_registry_client_error (ex), ex.what ());
    }
}

result_t<void> registry_query_client_t::connect (std::string endpoint)
{
    return connect (registry_query_client_options_t{std::move (endpoint)});
}

result_t<std::vector<topology_entry_t>> registry_query_client_t::topology () const
{
    if (!_impl->client) {
        return result_t<std::vector<topology_entry_t>>::failure (
          framework_error_kind_t::disconnected, "registry query client is not connected");
    }
    try {
        std::vector<topology_entry_t> mapped;
        const auto native = _impl->client->topology ();
        mapped.reserve (native.size ());
        for (const auto &entry : native) {
            mapped.push_back (from_native_topology_entry (entry));
        }
        return result_t<std::vector<topology_entry_t>>::success (std::move (mapped));
    }
    catch (const std::exception &ex) {
        return result_t<std::vector<topology_entry_t>>::failure (map_registry_client_error (ex),
                                                                 ex.what ());
    }
}

result_t<std::vector<topology_entry_t>>
registry_query_client_t::topology (const topology_filter_t &filter) const
{
    if (!_impl->client) {
        return result_t<std::vector<topology_entry_t>>::failure (
          framework_error_kind_t::disconnected, "registry query client is not connected");
    }
    try {
        const auto native_filter = to_native_filter (filter);
        const auto native = _impl->client->topology (&native_filter);
        std::vector<topology_entry_t> mapped;
        mapped.reserve (native.size ());
        for (const auto &entry : native) {
            mapped.push_back (from_native_topology_entry (entry));
        }
        return result_t<std::vector<topology_entry_t>>::success (std::move (mapped));
    }
    catch (const std::exception &ex) {
        return result_t<std::vector<topology_entry_t>>::failure (map_registry_client_error (ex),
                                                                 ex.what ());
    }
}

result_t<std::vector<member_peer_t>>
registry_query_client_t::member_peers (std::string channel_name) const
{
    if (!_impl->client) {
        return result_t<std::vector<member_peer_t>>::failure (
          framework_error_kind_t::disconnected, "registry query client is not connected");
    }
    try {
        const auto native = _impl->client->member_peers (channel_name);
        std::vector<member_peer_t> mapped;
        mapped.reserve (native.size ());
        for (const auto &entry : native) {
            mapped.push_back (from_native_member_peer_entry (entry));
        }
        return result_t<std::vector<member_peer_t>>::success (std::move (mapped));
    }
    catch (const std::exception &ex) {
        return result_t<std::vector<member_peer_t>>::failure (map_registry_client_error (ex),
                                                              ex.what ());
    }
}

result_t<registry_status_t> registry_query_client_t::status () const
{
    if (!_impl->client) {
        return result_t<registry_status_t>::failure (framework_error_kind_t::disconnected,
                                                     "registry query client is not connected");
    }
    try {
        return result_t<registry_status_t>::success (
          from_native_registry_status (_impl->client->status ()));
    }
    catch (const std::exception &ex) {
        return result_t<registry_status_t>::failure (map_registry_client_error (ex), ex.what ());
    }
}

void registry_query_client_t::close () noexcept
{
    try {
        if (_impl->client) {
            _impl->client->close ();
        }
        if (_impl->context) {
            _impl->context->term ();
        }
    }
    catch (...) {
    }
    _impl->client.reset ();
    _impl->context.reset ();
}

registry_builder_t zlink_builder_t::enable_registry ()
{
    return registry_builder_t (_state->registry_runtime);
}

discovery_builder_t zlink_builder_t::discovery ()
{
    return discovery_builder_t (_state->registry_runtime);
}

route_channel_builder_t zlink_builder_t::route_channel (std::string route_channel_name)
{
    if (route_channel_name.empty ()) {
        throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                     "route channel name is required");
    }
    if (auto found = _state->route_channels.find (route_channel_name);
        found != _state->route_channels.end ()) {
        return route_channel_builder_t (found->second);
    }
    auto state = std::make_shared<detail::route_channel_builder_state_t> (route_channel_name);
    _state->route_channels[route_channel_name] = state;
    auto &route_channels = _state->registry_runtime->route_channels;
    if (std::find (route_channels.begin (), route_channels.end (), route_channel_name)
        == route_channels.end ()) {
        route_channels.push_back (std::move (route_channel_name));
    }
    return route_channel_builder_t (state);
}

registry_options_snapshot_t zlink_builder_t::registry_options () const
{
    return _state->registry_runtime->options;
}

discovery_snapshot_t zlink_builder_t::discovery_options () const
{
    return _state->registry_runtime->discovery;
}

std::vector<std::string> zlink_builder_t::route_channels () const
{
    return _state->registry_runtime->route_channels;
}

result_t<void> zlink_builder_t::validate_registry () const
{
    return detail::registry_runtime_t (_state->registry_runtime).validate (*_state);
}

registry_query_t zlink_builder_t::registry_query () const
{
    detail::registry_runtime_t runtime (_state->registry_runtime);
    runtime.project_topology (*_state);
    return registry_query_t (_state->registry_runtime);
}

} // namespace zlink::framework

namespace zlink::framework::detail
{

registry_runtime_t::registry_runtime_t (std::shared_ptr<registry_runtime_state_t> state) :
    _state (std::move (state))
{
}

registry_runtime_t registry_runtime_t::from (const registry_query_t &query)
{
    return registry_runtime_t (query._state);
}

result_t<void> registry_runtime_t::validate (const zlink_builder_state_t &builder) const
{
    if (auto registry = validate_embedded_registry (); !registry) {
        return registry;
    }
    return validate_spot_remote_lookup (builder);
}

result_t<void> registry_runtime_t::validate_embedded_registry () const
{
    if (!_state->embedded_registry_enabled) {
        return result_t<void>::success ();
    }
    if (_state->options.pub_endpoint.empty () || _state->options.router_endpoint.empty ()) {
        return result_t<void>::failure (framework_error_kind_t::request_protocol_error,
                                        "embedded registry requires pub and router endpoints");
    }
    if (_state->options.heartbeat_interval.count () <= 0
        || _state->options.heartbeat_timeout.count () <= 0
        || _state->options.broadcast_interval.count () <= 0) {
        return result_t<void>::failure (framework_error_kind_t::request_protocol_error,
                                        "embedded registry intervals must be positive");
    }
    return result_t<void>::success ();
}

result_t<void>
registry_runtime_t::validate_spot_remote_lookup (const zlink_builder_state_t &builder) const
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
        const auto route_channel = resolve_registry_route_channel (builder, spot_node->snapshot);
        if (!route_channel) {
            return result_t<void>::failure (
              framework_error_kind_t::request_protocol_error,
              "registry spot remote address resolver requires an explicit route channel "
              "when there is not exactly one route channel");
        }
    }

    return result_t<void>::success ();
}

std::optional<std::string>
registry_runtime_t::resolve_registry_route_channel (const zlink_builder_state_t &builder,
                                                    const spot_node_snapshot_t &spot_node) const
{
    (void) builder;
    if (spot_node.registry_spot_route_channel) {
        const auto found =
          std::find (_state->route_channels.begin (), _state->route_channels.end (),
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

void registry_runtime_t::project_topology (const zlink_builder_state_t &builder)
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
    if (_state->monitoring) {
        monitoring_runtime_t (_state->monitoring)
          .publish_registry_snapshot ("registry",
                                      registry_status_t{registry_state_t::running, "registry",
                                                        _state->options.pub_endpoint,
                                                        _state->options.router_endpoint, 0},
                                      _state->topology, _state->services);
    }
}

void registry_runtime_t::project_channel (const zlink_builder_state_t &builder,
                                          const std::string &name,
                                          const channel_snapshot_t &channel)
{
    auto add_capability = [&] (const channel_capability_snapshot_t &capability,
                               service_role_t role) {
        if (!capability.enabled) {
            return;
        }
        _state->services.push_back (
          service_summary_entry_t{name, service_kind_t::channel, role, 1});
        _state->topology.push_back (topology_entry_t{builder.node_name, service_kind_t::channel,
                                                     role, name, topology_source_t::embedded,
                                                     topology_state_t::active});
        for (const auto &endpoint : capability.connect_endpoints) {
            _state->member_peers.push_back (member_peer_t{name, builder.node_name, endpoint});
        }
        for (const auto &endpoint : capability.bind_endpoints) {
            _state->member_peers.push_back (member_peer_t{name, builder.node_name, endpoint});
        }
    };
    add_capability (channel.server, service_role_t::server);
    add_capability (channel.client, service_role_t::client);
    add_capability (channel.publisher, service_role_t::publisher);
    add_capability (channel.subscriber, service_role_t::subscriber);
}

void registry_runtime_t::project_spot_node (const zlink_builder_state_t &builder,
                                            const spot_node_snapshot_t &spot_node)
{
    _state->services.push_back (service_summary_entry_t{spot_node.name, service_kind_t::spot,
                                                        service_role_t::spot_node,
                                                        spot_node.spot_names.size ()});
    _state->topology.push_back (
      topology_entry_t{builder.node_name, service_kind_t::spot, service_role_t::spot_node,
                       spot_node.name, topology_source_t::embedded, topology_state_t::active});
}

void registry_runtime_t::add_spot_route (spot_route_t route)
{
    const auto key = std::string (route.spot_rid.value ());
    _state->spot_routes[key] = std::move (route);
    const auto &stored = _state->spot_routes.at (key);
    for (const auto &[_, discovery] : _state->spot_route_discoveries) {
        if (discovery) {
            (void) discovery->bind_spot_route (stored);
        }
    }
}

void registry_runtime_t::attach_spot_route_discovery (
  std::string route_channel_name, std::shared_ptr<spot_route_discovery_bridge_t> discovery)
{
    if (route_channel_name.empty () || !discovery) {
        return;
    }
    auto bridge = discovery;
    _state->spot_route_discoveries[std::move (route_channel_name)] = std::move (discovery);
    for (const auto &[_, route] : _state->spot_routes) {
        (void) bridge->bind_spot_route (route);
    }
}

void registry_runtime_t::detach_spot_route_discovery (
  const std::string &route_channel_name) noexcept
{
    if (!_state || route_channel_name.empty ()) {
        return;
    }
    _state->spot_route_discoveries.erase (route_channel_name);
}

void registry_runtime_t::cleanup_stale_spot_routes (const std::set<std::string> &active_spot_rids)
{
    for (auto it = _state->spot_routes.begin (); it != _state->spot_routes.end ();) {
        if (active_spot_rids.find (it->first) == active_spot_rids.end ()) {
            it = _state->spot_routes.erase (it);
            continue;
        }
        ++it;
    }
}

result_t<spot_route_t> registry_runtime_t::resolve_spot_remote_address (spot_rid_t spot_rid)
{
    ++_state->spot_lookup_count;
    const auto found = _state->spot_routes.find (std::string (spot_rid.value ()));
    if (found == _state->spot_routes.end ()) {
        for (const auto &[_, discovery] : _state->spot_route_discoveries) {
            if (!discovery) {
                continue;
            }
            auto route = discovery->resolve_spot_route (spot_rid);
            if (route) {
                _state->spot_routes[std::string (spot_rid.value ())] = route.value ();
                return route;
            }
        }
        return result_t<spot_route_t>::failure (framework_error_kind_t::spot_route_not_found,
                                                "registry spot route was not found");
    }
    return result_t<spot_route_t>::success (found->second);
}

} // namespace zlink::framework::detail
