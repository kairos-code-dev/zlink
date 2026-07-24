/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#include "runtime/mesh/service_topology_registry.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace zlink::framework::runtime::mesh
{

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
    _local = std::move (descriptor);
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
    if (!valid_descriptor (descriptor) || connection_id.empty ()) {
        return peer_admission_result_t::invalid_descriptor;
    }
    std::lock_guard lock (_mutex);
    if (descriptor.mesh_name != _local.mesh_name) {
        return peer_admission_result_t::mesh_mismatch;
    }
    if (descriptor.node_routing_id == _local.node_routing_id) {
        return peer_admission_result_t::invalid_descriptor;
    }
    const auto found = _peers.find (descriptor.node_routing_id);
    if (found != _peers.end ()
        && found->second.descriptor.lifecycle_generation
             == descriptor.lifecycle_generation
        && found->second.descriptor.descriptor_revision
             > descriptor.descriptor_revision) {
        return peer_admission_result_t::stale_descriptor;
    }
    if (found != _peers.end ()
        && found->second.descriptor.lifecycle_generation
             == descriptor.lifecycle_generation
        && found->second.descriptor.descriptor_revision
             == descriptor.descriptor_revision
        && found->second.descriptor != descriptor) {
        return peer_admission_result_t::stale_descriptor;
    }
    auto key = descriptor.node_routing_id;
    _peers.insert_or_assign (
      std::move (key),
      admitted_peer_t{std::move (descriptor), std::move (connection_id)});
    return peer_admission_result_t::admitted;
}

bool service_topology_registry_t::disconnect (
  const std::vector<std::uint8_t> &node_routing_id,
  const std::vector<std::uint8_t> &connection_id)
{
    std::lock_guard lock (_mutex);
    const auto found = _peers.find (node_routing_id);
    if (found == _peers.end () || found->second.connection_id != connection_id) {
        return false;
    }
    _peers.erase (found);
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
