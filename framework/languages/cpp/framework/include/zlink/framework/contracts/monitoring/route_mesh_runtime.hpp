/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include <zlink/Contracts/Core/routing_id.hpp>
#include <zlink/framework/contracts/configuration/drain.hpp>
#include <zlink/framework/contracts/dispatch/task.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace zlink::framework
{

enum class mesh_node_state_t
{
    starting,
    serving,
    draining,
    drained,
    force_stopping,
    stopped,
    faulted
};

struct mesh_peer_snapshot_t
{
    zlink::routing_id_t rid;
    std::uint64_t lifecycle_generation;
    std::uint64_t descriptor_revision;
    std::string endpoint;
    std::string admission_state;
    bool ready;
    std::string drain_state;
    std::vector<std::string> channel_names;
    std::optional<std::string> last_failure;
};

struct mesh_channel_snapshot_t
{
    std::string channel_name;
    int local_weight;
    std::uint64_t ready_member_count;
    bool selectable;
};

struct logical_multicast_snapshot_t
{
    std::uint64_t submitted;
    std::uint64_t backpressured;
    std::uint64_t dropped;
    std::uint64_t remote_snapshot_count;
    std::uint64_t remote_admitted_count;
    std::uint64_t remote_dropped_count;
    std::uint64_t local_snapshot_count;
    std::uint64_t local_admitted_count;
    std::uint64_t local_dropped_count;
};

struct mesh_claim_snapshot_t
{
    bool application_active;
    std::uint64_t pending_application_work;
    bool infrastructure_active;
    std::uint64_t pending_infrastructure_work;
};

struct location_runtime_snapshot_t
{
    std::string state;
    std::optional<std::chrono::system_clock::time_point> last_success_at;
    std::optional<std::chrono::system_clock::time_point> last_failure_at;
};

struct mesh_drain_snapshot_t
{
    mesh_node_state_t state;
    std::optional<std::chrono::system_clock::time_point> deadline;
    bool work_sealed;
    std::uint64_t pending_request_count;
    std::uint64_t pending_transfer_count;
    std::uint64_t pending_stream_barrier_count;
};

struct mesh_node_snapshot_t
{
    std::string mesh_name;
    zlink::routing_id_t rid;
    std::uint64_t lifecycle_generation;
    std::uint64_t descriptor_revision;
    std::string endpoint;
    mesh_node_state_t state;
    std::uint64_t sequence;
    std::chrono::system_clock::time_point observed_at;
    std::vector<std::string> descriptor_sources;
    std::vector<mesh_peer_snapshot_t> peers;
    std::vector<mesh_channel_snapshot_t> channels;
    logical_multicast_snapshot_t multicast;
    mesh_claim_snapshot_t claims;
    location_runtime_snapshot_t location;
    mesh_drain_snapshot_t drain;
};

struct mesh_runtime_event_t
{
    std::string identifier;
    std::uint64_t sequence;
    std::chrono::system_clock::time_point timestamp;
    std::string mesh_name;
    zlink::routing_id_t source_rid;
    std::optional<zlink::routing_id_t> peer_rid;
    std::optional<std::uint64_t> lifecycle_generation;
    std::optional<std::uint64_t> descriptor_revision;
    std::optional<std::string> channel_name;
    std::optional<std::string> claim_domain;
    std::optional<std::string> message_kind;
    std::optional<std::uint64_t> remote_snapshot_count;
    std::optional<std::uint64_t> remote_admitted_count;
    std::optional<std::uint64_t> remote_dropped_count;
    std::optional<std::uint64_t> local_snapshot_count;
    std::optional<std::uint64_t> local_admitted_count;
    std::optional<std::uint64_t> local_dropped_count;
    std::optional<std::string> reason;
    std::optional<mesh_node_state_t> state;
};

class mesh_runtime_observation_t
{
  public:
    virtual ~mesh_runtime_observation_t () = default;
    virtual void close () = 0;
};

class route_mesh_runtime_t
{
  public:
    virtual mesh_node_snapshot_t snapshot (std::string mesh_name) const = 0;
    virtual std::unique_ptr<mesh_runtime_observation_t>
    observe (std::string mesh_name,
             std::size_t capacity,
             std::function<void (const mesh_runtime_event_t &)> observer) = 0;
    virtual bool is_ready (std::string mesh_name) const = 0;
    virtual task_t<drain_result_t>
    drain (std::string mesh_name,
           std::chrono::milliseconds deadline = std::chrono::seconds (30)) = 0;
    virtual task_t<drain_result_t> await_drained (std::string mesh_name) = 0;
};

} // namespace zlink::framework
