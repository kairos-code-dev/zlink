/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#include "runtime/mesh/service_topology_registry.hpp"
#include "runtime/mesh/route_mesh_connection_policy.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace zlink::framework::runtime::mesh
{

bool route_mesh_connection_not_required (
  const service_node_descriptor_t &local,
  const service_node_descriptor_t &remote) noexcept
{
    const auto public_role = [] (service_object_role_t role) {
        return role == service_object_role_t::client
          ? object_role_t::client
          : role == service_object_role_t::server
              ? object_role_t::server
              : object_role_t::none;
    };
    return route_mesh_connection_not_required (
      public_role (local.object_role), !local.channels.empty (),
      public_role (remote.object_role), !remote.channels.empty ());
}

std::uint64_t
sum_service_weights (std::span<const int> weights)
{
    std::uint64_t total = 0;
    for (const auto weight : weights) {
        if (weight < 0 || weight > 10000)
            throw std::invalid_argument (
              "service weight must be in range 0..10000");
        const auto value =
          static_cast<std::uint64_t> (weight);
        if (total
              > std::numeric_limits<std::uint64_t>::max ()
                  - value)
            throw std::overflow_error (
              "service weight sum is exhausted");
        total += value;
    }
    return total;
}

namespace
{

bool valid_channels (const std::vector<service_channel_descriptor_t> &channels)
{
    std::string previous;
    bool first = true;
    for (const auto &channel : channels) {
        if (channel.name.empty ()) {
            return false;
        }
        if (channel.weight < 0 || channel.weight > 10000) {
            return false;
        }
        if (!first && previous >= channel.name) {
            return false;
        }
        previous = channel.name;
        first = false;
    }
    return true;
}

bool immutable_fields_match (
  const service_node_descriptor_t &current,
  const service_node_descriptor_t &incoming,
  bool allow_initial_endpoint_resolution = false)
{
    if (current.mesh_name != incoming.mesh_name
        || current.node_routing_id != incoming.node_routing_id
        || current.lifecycle_generation
             != incoming.lifecycle_generation
        || (!allow_initial_endpoint_resolution
            && current.advertised_endpoint
                 != incoming.advertised_endpoint)
        || current.security_identity != incoming.security_identity
        || current.effective_max_message_bytes
             != incoming.effective_max_message_bytes
        || current.application_version
             != incoming.application_version
        || current.protocol_capabilities
             != incoming.protocol_capabilities
        || current.object_role != incoming.object_role
        || current.active_capacity_limit
             != incoming.active_capacity_limit
        || current.pending_capacity_limit
             != incoming.pending_capacity_limit
        || current.channels.size () != incoming.channels.size ()) {
        return false;
    }
    return std::equal (
      current.channels.begin (), current.channels.end (),
      incoming.channels.begin (),
      [] (const auto &left, const auto &right) {
          return left.name == right.name;
      });
}

bool discovery_expectation_matches (
  const service_node_descriptor_t &expected,
  const service_node_descriptor_t &incoming)
{
    return expected.mesh_name == incoming.mesh_name
           && expected.node_routing_id == incoming.node_routing_id
           && expected.advertised_endpoint
                == incoming.advertised_endpoint
           && expected.security_identity
                == incoming.security_identity
           && expected.lifecycle_generation
                == incoming.lifecycle_generation;
}

} // namespace

service_topology_registry_t::service_topology_registry_t (
  service_node_descriptor_t local) :
    _local (std::move (local))
{
    if (!valid_descriptor (_local)) {
        throw std::invalid_argument ("local service descriptor is invalid");
    }
}

bool service_topology_registry_t::byte_vector_less_t::operator() (
  const std::vector<std::uint8_t> &left,
  const std::vector<std::uint8_t> &right) const noexcept
{
    return std::lexicographical_compare (
      left.begin (), left.end (), right.begin (), right.end ());
}

bool service_topology_registry_t::valid_descriptor (
  const service_node_descriptor_t &descriptor)
{
    return !descriptor.mesh_name.empty () && !descriptor.node_routing_id.empty ()
           && descriptor.lifecycle_generation != 0
           && descriptor.descriptor_revision != 0
           && !descriptor.advertised_endpoint.empty ()
           && valid_channels (descriptor.channels)
           && !descriptor.security_identity.empty ()
           && descriptor.effective_max_message_bytes != 0
           && descriptor.application_version >= 0
           && descriptor.placement_weight >= 0
           && descriptor.placement_weight <= 10000
           && descriptor.active_capacity_limit != 0
           && descriptor.active_capacity_limit <= 2147483647u
           && descriptor.pending_capacity_limit <= 2147483647u
           && descriptor.active_capacity_used <= descriptor.active_capacity_limit
           && descriptor.pending_capacity_used <= descriptor.pending_capacity_limit
           && std::is_sorted (descriptor.protocol_capabilities.begin (),
                              descriptor.protocol_capabilities.end ())
           && std::adjacent_find (descriptor.protocol_capabilities.begin (),
                                  descriptor.protocol_capabilities.end ())
                == descriptor.protocol_capabilities.end ()
           && std::find (descriptor.protocol_capabilities.begin (),
                         descriptor.protocol_capabilities.end (),
                         "framework-service-v11")
                != descriptor.protocol_capabilities.end ();
}

bool service_topology_registry_t::selectable (
  const service_node_descriptor_t &descriptor,
  const std::string &channel_name)
{
    if (descriptor.state != service_node_state_t::serving) {
        return false;
    }
    const auto found = std::lower_bound (
      descriptor.channels.begin (), descriptor.channels.end (), channel_name,
      [] (const service_channel_descriptor_t &channel, const std::string &name) {
          return channel.name < name;
      });
    return found != descriptor.channels.end () && found->name == channel_name
           && found->weight != 0;
}

void service_topology_registry_t::publish_local (
  service_node_descriptor_t descriptor)
{
    if (!valid_descriptor (descriptor)) {
        throw std::invalid_argument ("published service descriptor is invalid");
    }
    std::function<void ()> changed;
    {
        std::lock_guard lock (_mutex);
        if (descriptor.mesh_name != _local.mesh_name
            || descriptor.node_routing_id != _local.node_routing_id
            || descriptor.lifecycle_generation != _local.lifecycle_generation) {
            throw std::invalid_argument (
              "published service descriptor changes the local identity");
        }
        if (descriptor.descriptor_revision <= _local.descriptor_revision) {
            throw std::invalid_argument (
              "published service descriptor revision is not increasing");
        }
        const bool resolving_bound_endpoint =
          _local.state == service_node_state_t::preparing
          && descriptor.state == service_node_state_t::serving;
        if (!immutable_fields_match (
              _local, descriptor, resolving_bound_endpoint)) {
            throw std::invalid_argument (
              "published service descriptor changes immutable fields");
        }
        _local = std::move (descriptor);
        for (auto it = _not_required_peers.begin ();
             it != _not_required_peers.end ();) {
            if (!route_mesh_connection_not_required (_local, it->second))
                it = _not_required_peers.erase (it);
            else
                ++it;
        }
        changed = _change_handler;
    }
    if (changed)
        changed ();
}

void service_topology_registry_t::set_change_handler (
  std::function<void ()> handler)
{
    std::lock_guard lock (_mutex);
    _change_handler = std::move (handler);
}

service_node_descriptor_t
service_topology_registry_t::local_descriptor () const
{
    std::lock_guard lock (_mutex);
    return _local;
}

peer_admission_result_t service_topology_registry_t::admit (
  service_node_descriptor_t descriptor,
  std::vector<std::uint8_t> connection_id)
{
    return admit_impl (
      std::move (descriptor), std::move (connection_id), std::nullopt,
      nullptr);
}

peer_admission_result_t service_topology_registry_t::admit (
  service_node_descriptor_t descriptor,
  std::vector<std::uint8_t> connection_id,
  service_connection_direction_t direction)
{
    return admit_impl (
      std::move (descriptor), std::move (connection_id), direction,
      nullptr);
}

peer_admission_result_t service_topology_registry_t::admit (
  service_node_descriptor_t descriptor,
  std::vector<std::uint8_t> connection_id,
  service_connection_direction_t direction,
  const service_node_descriptor_t &expected_descriptor)
{
    return admit_impl (
      std::move (descriptor), std::move (connection_id), direction,
      &expected_descriptor);
}

peer_admission_result_t service_topology_registry_t::admit_impl (
  service_node_descriptor_t descriptor,
  std::vector<std::uint8_t> connection_id,
  std::optional<service_connection_direction_t> direction,
  const service_node_descriptor_t *expected_descriptor)
{
    if (!valid_descriptor (descriptor) || connection_id.empty ()) {
        return peer_admission_result_t::invalid_descriptor;
    }
    std::unique_lock lock (_mutex);
    if (descriptor.mesh_name != _local.mesh_name) {
        return peer_admission_result_t::mesh_mismatch;
    }
    if (descriptor.node_routing_id == _local.node_routing_id) {
        return peer_admission_result_t::invalid_descriptor;
    }
    if (expected_descriptor != nullptr
        && !discovery_expectation_matches (
          *expected_descriptor, descriptor)) {
        return peer_admission_result_t::stale_descriptor;
    }
    const auto admitted = _peers.find (descriptor.node_routing_id);
    const auto not_required =
      _not_required_peers.find (descriptor.node_routing_id);
    const auto *current =
      admitted != _peers.end ()
        ? &admitted->second.descriptor
        : not_required != _not_required_peers.end ()
            ? &not_required->second
            : nullptr;
    if (current != nullptr
        && descriptor.lifecycle_generation
             != current->lifecycle_generation
        && expected_descriptor == nullptr) {
        return peer_admission_result_t::stale_descriptor;
    }
    if (current != nullptr
        && descriptor.lifecycle_generation
             == current->lifecycle_generation
        && (descriptor.descriptor_revision
              < current->descriptor_revision
            || (descriptor.descriptor_revision
                  == current->descriptor_revision
                && *current != descriptor))) {
        return peer_admission_result_t::stale_descriptor;
    }
    if (current != nullptr
        && descriptor.lifecycle_generation
             == current->lifecycle_generation
        && descriptor.descriptor_revision
             > current->descriptor_revision
        && !immutable_fields_match (*current, descriptor)) {
        return peer_admission_result_t::stale_descriptor;
    }
    if (route_mesh_connection_not_required (_local, descriptor)) {
        auto key = descriptor.node_routing_id;
        _peers.erase (key);
        _not_required_peers.insert_or_assign (
          std::move (key), std::move (descriptor));
        auto changed = _change_handler;
        lock.unlock ();
        if (changed)
            changed ();
        return peer_admission_result_t::not_required;
    }

    if (direction.has_value () && admitted != _peers.end ()
        && admitted->second.descriptor.lifecycle_generation
             == descriptor.lifecycle_generation
        && admitted->second.connection_id != connection_id) {
        const auto preferred_direction =
          byte_vector_less_t{} (_local.node_routing_id,
                                descriptor.node_routing_id)
            ? service_connection_direction_t::outbound
            : service_connection_direction_t::inbound;
        const auto keep_current =
          admitted->second.direction != direction.value ()
            ? admitted->second.direction == preferred_direction
            : !byte_vector_less_t{} (
                connection_id, admitted->second.connection_id);
        if (keep_current)
            return peer_admission_result_t::duplicate_connection;
    }

    _not_required_peers.erase (descriptor.node_routing_id);
    auto key = descriptor.node_routing_id;
    _peers.insert_or_assign (
      std::move (key),
      admitted_peer_t{std::move (descriptor), std::move (connection_id),
                      direction.value_or (
                        service_connection_direction_t::inbound)});
    auto changed = _change_handler;
    lock.unlock ();
    if (changed)
        changed ();
    return peer_admission_result_t::admitted;
}

bool service_topology_registry_t::disconnect (
  const std::vector<std::uint8_t> &node_routing_id,
  const std::vector<std::uint8_t> &connection_id)
{
    std::function<void ()> changed;
    {
        std::lock_guard lock (_mutex);
        const auto found = _peers.find (node_routing_id);
        if (found == _peers.end ()
            || found->second.connection_id != connection_id) {
            return false;
        }
        _peers.erase (found);
        changed = _change_handler;
    }
    if (changed)
        changed ();
    return true;
}

std::vector<admitted_peer_t> service_topology_registry_t::peers () const
{
    std::lock_guard lock (_mutex);
    std::vector<admitted_peer_t> result;
    result.reserve (_peers.size ());
    for (const auto &[_, peer] : _peers) {
        result.push_back (peer);
    }
    return result;
}

std::vector<service_node_descriptor_t>
service_topology_registry_t::not_required_peers () const
{
    std::lock_guard lock (_mutex);
    std::vector<service_node_descriptor_t> result;
    result.reserve (_not_required_peers.size ());
    for (const auto &[_, descriptor] : _not_required_peers)
        result.push_back (descriptor);
    return result;
}

std::optional<admitted_peer_t> service_topology_registry_t::peer (
  const std::vector<std::uint8_t> &node_routing_id) const
{
    std::lock_guard lock (_mutex);
    const auto found = _peers.find (node_routing_id);
    if (found == _peers.end ()) {
        return std::nullopt;
    }
    return found->second;
}

std::optional<admitted_peer_t>
service_topology_registry_t::select (const std::string &channel_name)
{
    if (channel_name.empty ()) {
        return std::nullopt;
    }
    std::lock_guard lock (_mutex);
    std::vector<const admitted_peer_t *> eligible;
    std::vector<int> weights;
    for (const auto &[_, peer] : _peers) {
        if (!selectable (peer.descriptor, channel_name)) {
            continue;
        }
        eligible.push_back (&peer);
        const auto channel = std::lower_bound (
          peer.descriptor.channels.begin (), peer.descriptor.channels.end (),
          channel_name,
          [] (const service_channel_descriptor_t &entry, const std::string &name) {
              return entry.name < name;
          });
        weights.push_back (channel->weight);
    }
    const auto total_weight =
      sum_service_weights (weights);
    if (eligible.empty () || total_weight == 0) {
        return std::nullopt;
    }

    auto &cursor = _selection_cursor[channel_name];
    const auto selected_weight = cursor++ % total_weight;
    std::uint64_t offset = 0;
    for (const auto *peer : eligible) {
        const auto channel = std::lower_bound (
          peer->descriptor.channels.begin (), peer->descriptor.channels.end (),
          channel_name,
          [] (const service_channel_descriptor_t &entry, const std::string &name) {
              return entry.name < name;
          });
        offset += channel->weight;
        if (selected_weight < offset) {
            return *peer;
        }
    }
    return *eligible.back ();
}

std::vector<admitted_peer_t>
service_topology_registry_t::multicast_targets (
  const std::string &channel_name) const
{
    if (channel_name.empty ())
        return {};
    std::lock_guard lock (_mutex);
    std::vector<admitted_peer_t> result;
    result.reserve (_peers.size ());
    for (const auto &[_, peer] : _peers) {
        if (selectable (peer.descriptor, channel_name))
            result.push_back (peer);
    }
    return result;
}

} // namespace zlink::framework::runtime::mesh
