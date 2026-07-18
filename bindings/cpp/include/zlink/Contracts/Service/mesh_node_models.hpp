/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Core/routing_id.hpp"

#include <chrono>
#include <cstdint>
#include <string>

namespace zlink
{

namespace detail
{
struct service_model_access_t;
} // namespace detail

/// @brief The lifecycle state of a mesh node.
enum class mesh_node_state_t : int
{
    created = 1,
    started = 2,
    partial_ready = 3,
    ready = 4,
    draining = 5,
    stopped = 6,
    error = 7
};

/// @brief How a mesh peer became known to the node.
enum class mesh_peer_source_t : int
{
    manual = 1,
    discovery = 2,
    mixed = 3
};

/// @brief The connection state of a mesh peer.
enum class mesh_peer_state_t : int
{
    configured = 1,
    connecting = 2,
    admitted = 3,
    draining = 4,
    closed = 5,
    error = 6
};

/// @brief A snapshot of a mesh node's membership and pending-work counters.
class mesh_node_status_t
{
  public:
    mesh_node_status_t () :
        routing_id_ (detail::unchecked_empty_routing_id ()),
        mesh_name_ (),
        local_endpoint_ (),
        state_ (mesh_node_state_t::created),
        lifecycle_generation_ (0),
        descriptor_revision_ (0),
        channel_count_ (0),
        configured_peer_count_ (0),
        admitted_peer_count_ (0),
        draining_peer_count_ (0),
        pending_application_messages_ (0),
        pending_infrastructure_messages_ (0),
        pending_bytes_ (0),
        multicast_submitted_ (0),
        multicast_dropped_targets_ (0),
        last_error_ (0),
        last_changed_ms_ (0)
    {
    }

    const routing_id_t &routing_id () const noexcept { return routing_id_; }

    const std::string &mesh_name () const noexcept { return mesh_name_; }

    const std::string &local_endpoint () const noexcept { return local_endpoint_; }

    mesh_node_state_t state () const noexcept { return state_; }

    uint64_t lifecycle_generation () const noexcept { return lifecycle_generation_; }

    uint64_t descriptor_revision () const noexcept { return descriptor_revision_; }

    uint32_t channel_count () const noexcept { return channel_count_; }

    uint32_t configured_peer_count () const noexcept { return configured_peer_count_; }

    uint32_t admitted_peer_count () const noexcept { return admitted_peer_count_; }

    uint32_t draining_peer_count () const noexcept { return draining_peer_count_; }

    uint64_t pending_application_messages () const noexcept
    {
        return pending_application_messages_;
    }

    uint64_t pending_infrastructure_messages () const noexcept
    {
        return pending_infrastructure_messages_;
    }

    uint64_t pending_bytes () const noexcept { return pending_bytes_; }

    uint64_t multicast_submitted () const noexcept { return multicast_submitted_; }

    uint64_t multicast_dropped_targets () const noexcept { return multicast_dropped_targets_; }

    int32_t last_error () const noexcept { return last_error_; }

    std::chrono::milliseconds last_changed () const noexcept
    {
        return std::chrono::milliseconds (static_cast<int64_t> (last_changed_ms_));
    }

  private:
    routing_id_t routing_id_;
    std::string mesh_name_;
    std::string local_endpoint_;
    mesh_node_state_t state_;
    uint64_t lifecycle_generation_;
    uint64_t descriptor_revision_;
    uint32_t channel_count_;
    uint32_t configured_peer_count_;
    uint32_t admitted_peer_count_;
    uint32_t draining_peer_count_;
    uint64_t pending_application_messages_;
    uint64_t pending_infrastructure_messages_;
    uint64_t pending_bytes_;
    uint64_t multicast_submitted_;
    uint64_t multicast_dropped_targets_;
    int32_t last_error_;
    uint64_t last_changed_ms_;
    friend struct detail::service_model_access_t;
};

/// @brief One peer of a mesh node and its connection details.
class mesh_peer_entry_t
{
  public:
    mesh_peer_entry_t () :
        routing_id_ (detail::unchecked_empty_routing_id ()),
        endpoint_ (),
        connection_intent_id_ (0),
        source_ (mesh_peer_source_t::manual),
        state_ (mesh_peer_state_t::configured),
        lifecycle_generation_ (0),
        descriptor_revision_ (0),
        channel_count_ (0),
        last_error_ (0),
        last_changed_ms_ (0)
    {
    }

    const routing_id_t &routing_id () const noexcept { return routing_id_; }

    const std::string &endpoint () const noexcept { return endpoint_; }

    uint64_t connection_intent_id () const noexcept { return connection_intent_id_; }

    mesh_peer_source_t source () const noexcept { return source_; }

    mesh_peer_state_t state () const noexcept { return state_; }

    uint64_t lifecycle_generation () const noexcept { return lifecycle_generation_; }

    uint64_t descriptor_revision () const noexcept { return descriptor_revision_; }

    uint32_t channel_count () const noexcept { return channel_count_; }

    int32_t last_error () const noexcept { return last_error_; }

    std::chrono::milliseconds last_changed () const noexcept
    {
        return std::chrono::milliseconds (static_cast<int64_t> (last_changed_ms_));
    }

  private:
    routing_id_t routing_id_;
    std::string endpoint_;
    uint64_t connection_intent_id_;
    mesh_peer_source_t source_;
    mesh_peer_state_t state_;
    uint64_t lifecycle_generation_;
    uint64_t descriptor_revision_;
    uint32_t channel_count_;
    int32_t last_error_;
    uint64_t last_changed_ms_;
    friend struct detail::service_model_access_t;
};

} // namespace zlink
