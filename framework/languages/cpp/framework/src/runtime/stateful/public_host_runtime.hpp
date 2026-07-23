/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include "runtime/foundation/operation_registry.hpp"
#include "runtime/mesh/raw_mesh_node_owner.hpp"
#include "runtime/stateful/maintenance_runtime.hpp"
#include "runtime/stateful/stateful_object_runtime.hpp"
#include "runtime/stateful/stream_session_registry.hpp"

#include <zlink.hpp>
#include <zlink/framework/contracts/actors/actor.hpp>

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace zlink::framework::runtime::host
{

struct operation_id_t
{
    std::uint64_t high = 0;
    std::uint64_t low = 0;

    friend bool operator== (const operation_id_t &, const operation_id_t &) = default;
};

enum class record_kind_t
{
    node_send,
    node_request,
    channel_send,
    channel_request,
    spot_send,
    spot_request,
    actor_send,
    actor_request,
    completion,
    send_ready,
    spot_control,
    spot_multicast
};

enum class ready_domain_t
{
    application,
    infrastructure
};

enum class owner_kind_t
{
    node,
    channel,
    spot,
    actor
};

enum class operation_kind_t
{
    none,
    actor_join
};

enum class lifecycle_kind_t
{
    joined,
    left
};

enum class actor_join_result_t
{
    accepted,
    rejected
};

enum class join_admission_t
{
    accepted,
    rejected
};

struct actor_join_completion_t
{
    join_admission_t join_result = join_admission_t::rejected;
    actor_ref_t current_actor;
};

struct actor_control_t
{
    lifecycle_kind_t kind = lifecycle_kind_t::joined;
    actor_ref_t current_actor;
};

struct send_ready_data_t
{
    enum class destination_kind_t
    {
        node,
        channel,
        spot,
        actor,
        bound_session
    };

    destination_kind_t destination_kind = destination_kind_t::node;
    zlink::routing_id_t target_node_rid =
      zlink::routing_id_t::from (std::uint32_t{0});
    zlink::routing_id_t target_spot_rid =
      zlink::routing_id_t::from (std::uint32_t{0});
    std::string channel_name;
    actor_ref_t target_actor;
};

class public_host_runtime_t;

struct reply_token_t
{
    std::weak_ptr<public_host_runtime_t> host;
    std::shared_ptr<mesh::service_mailbox_record_t> request;
};

struct receive_record_t
{
    record_kind_t kind = record_kind_t::node_send;
    ready_domain_t domain = ready_domain_t::application;
    operation_id_t operation_id;
    operation_kind_t operation_kind = operation_kind_t::none;
    zlink::routing_id_t source_node_rid =
      zlink::routing_id_t::from (std::uint32_t{0});
    std::uint64_t source_binding_generation = 0;
    std::string channel_name;
    std::string topic;
    int terminal_result = 0;
    int failure_errno = 0;
    reply_token_t reply_token;
    std::optional<actor_join_completion_t> join_completion;
    std::optional<actor_control_t> actor_control;
    std::optional<send_ready_data_t> send_ready;
};

struct ready_record_t
{
    owner_kind_t owner_kind = owner_kind_t::node;
    ready_domain_t domain = ready_domain_t::application;
    zlink::routing_id_t spot_rid =
      zlink::routing_id_t::from (std::uint32_t{0});
    actor_ref_t actor;
    std::string channel_name;
};

struct node_status_t
{
    enum class state_t
    {
        preparing,
        serving,
        draining,
        stopped,
        error
    };

    state_t state = state_t::stopped;
    zlink::routing_id_t node_routing_id =
      zlink::routing_id_t::from (std::uint32_t{0});
    std::string endpoint;
    std::uint64_t generation = 0;

    zlink::routing_id_t routing_id () const;
    std::string local_endpoint () const;
    std::uint64_t lifecycle_generation () const noexcept;
};

struct spot_status_t
{
    std::uint64_t generation = 0;
    std::uint64_t lifecycle_generation () const noexcept;
};

struct publish_detail_t
{
    std::uint64_t snapshot_remote_target_count = 0;
    std::uint64_t admitted_remote_target_count = 0;
    std::uint64_t dropped_remote_target_count = 0;
    std::uint64_t unreachable_remote_target_count = 0;
    std::uint64_t snapshot_local_spot_count = 0;
    std::uint64_t admitted_local_spot_count = 0;
    std::uint64_t dropped_local_spot_count = 0;
};

struct host_options_t
{
    mesh::raw_mesh_node_options_t mesh;
    std::string entry_spot_name = "entry";
};

enum class actor_transfer_role_t
{
    source,
    target
};

struct actor_transfer_prepare_t
{
    actor_transfer_role_t role = actor_transfer_role_t::source;
    std::string transfer_id;
    actor_ref_t actor;
    zlink::routing_id_t source_spot_rid =
      zlink::routing_id_t::from (std::uint32_t{0});
    zlink::routing_id_t target_spot_rid =
      zlink::routing_id_t::from (std::uint32_t{0});
    zlink::routing_id_t target_node_rid =
      zlink::routing_id_t::from (std::uint32_t{0});
};

struct actor_transfer_prepare_result_t
{
    actor_ref_t current_actor;
    std::uint64_t membership_epoch = 0;
};

class actor_transfer_token_t
{
  public:
    actor_transfer_token_t () = default;
    bool valid () const noexcept;
    bool commit (std::uint64_t membership_epoch);
    bool activate ();
    void abort () noexcept;

  private:
    friend class public_host_runtime_t;
    std::weak_ptr<public_host_runtime_t> _host;
    stateful::membership_token_t _membership;
    actor_transfer_role_t _role = actor_transfer_role_t::source;
    bool _terminal = false;
};

class spot_handle_t
{
  public:
    spot_handle_t () = default;
    spot_handle_t (std::shared_ptr<public_host_runtime_t> host,
                   stateful::object_ref_t object);

    spot_status_t status () const;
    zlink::routing_id_t routing_id () const;
    zlink::submit_result_t send_to_spot (
      const zlink::routing_id_t &target_node_rid,
      const zlink::routing_id_t &target_spot_rid,
      std::uint64_t target_spot_generation,
      const std::vector<zlink::message_t> &parts,
      zlink::send_flags_t flags = zlink::send_flags_t::none,
      std::span<const std::uint8_t> metadata = {});
    zlink::submit_result_t request_to_spot (
      const zlink::routing_id_t &target_node_rid,
      const zlink::routing_id_t &target_spot_rid,
      std::uint64_t target_spot_generation,
      const std::vector<zlink::message_t> &parts,
      operation_id_t &operation,
      zlink::send_flags_t flags,
      std::chrono::milliseconds timeout,
      std::span<const std::uint8_t> metadata = {});
    zlink::submit_result_t publish (
      const std::string &channel_name,
      const std::string &topic,
      const std::vector<zlink::message_t> &parts,
      zlink::send_flags_t flags = zlink::send_flags_t::none,
      std::span<const std::uint8_t> metadata = {},
      publish_detail_t *detail = nullptr);
    void set_subscription (const std::string &channel_name,
                           const std::string &topic);
    void unset_subscription (const std::string &channel_name,
                             const std::string &topic);
    bool close () noexcept;

  private:
    std::shared_ptr<public_host_runtime_t> _host;
    stateful::object_ref_t _object;
};

class actor_handle_t
{
  public:
    actor_handle_t () = default;
    actor_handle_t (std::shared_ptr<public_host_runtime_t> host,
                    actor_ref_t actor,
                    stateful::object_ref_t object);

    const actor_ref_t &ref () const noexcept;
    zlink::submit_result_t join_entry_spot (
      const zlink::routing_id_t &target_node_rid,
      const std::vector<zlink::message_t> &parts,
      operation_id_t &operation,
      std::chrono::milliseconds timeout);
    zlink::submit_result_t join_spot (
      const zlink::routing_id_t &target_node_rid,
      const zlink::routing_id_t &target_spot_rid,
      std::uint64_t target_spot_generation,
      const std::vector<zlink::message_t> &parts,
      operation_id_t &operation,
      std::chrono::milliseconds timeout);
    zlink::submit_result_t send_to (
      const actor_ref_t &target,
      const std::vector<zlink::message_t> &parts,
      zlink::send_flags_t flags = zlink::send_flags_t::none,
      std::span<const std::uint8_t> metadata = {});
    zlink::submit_result_t request_to (
      const actor_ref_t &target,
      const std::vector<zlink::message_t> &parts,
      operation_id_t &operation,
      zlink::send_flags_t flags,
      std::chrono::milliseconds timeout,
      std::span<const std::uint8_t> metadata = {});

  private:
    std::shared_ptr<public_host_runtime_t> _host;
    actor_ref_t _actor;
    stateful::object_ref_t _object;
};

class public_host_runtime_t :
    public std::enable_shared_from_this<public_host_runtime_t>
{
  public:
    explicit public_host_runtime_t (host_options_t options);
    ~public_host_runtime_t ();

    public_host_runtime_t (const public_host_runtime_t &) = delete;
    public_host_runtime_t &operator= (const public_host_runtime_t &) = delete;

    void start ();
    void close () noexcept;
    bool connect_peer (const std::string &endpoint,
                       std::optional<zlink::routing_id_t> expected = std::nullopt);
    void disconnect_peer (const std::string &endpoint) noexcept;
    node_status_t status () const;
    void set_channel_weight (const std::string &channel_name,
                             std::uint32_t weight);
    std::int64_t max_message_size () const;
    void set_max_message_size (std::int64_t value);
    mesh::raw_mesh_node_owner_t &transport () noexcept;
    stateful::stateful_object_runtime_t &objects () noexcept;
    stateful::stream_session_registry_t &sessions () noexcept;
    void configure_maintenance (
      stateful::maintenance_provider_set_t providers,
      stateful::relocation_limits_t limits = {},
      stateful::maintenance_runtime_t::observer_t relocation_observer = {},
      stateful::host_maintenance_runtime_t::observer_t
        termination_observer = {});
    stateful::maintenance_runtime_t *maintenance () noexcept;
    stateful::host_maintenance_runtime_t *termination () noexcept;

    spot_handle_t entry_spot ();
    spot_handle_t get_or_create_spot (const zlink::routing_id_t &routing_id);
    actor_handle_t create_actor (std::string actor_type, std::string actor_id);
    zlink::submit_result_t send_to_actor (
      const actor_ref_t &target,
      const std::vector<zlink::message_t> &parts,
      std::span<const std::uint8_t> metadata = {});
    zlink::submit_result_t request_to_actor (
      const actor_ref_t &target,
      const std::vector<zlink::message_t> &parts,
      operation_id_t &operation,
      std::chrono::milliseconds timeout,
      std::span<const std::uint8_t> metadata = {});
    zlink::submit_result_t send_to_node (
      const zlink::routing_id_t &target,
      const std::vector<zlink::message_t> &parts);
    zlink::submit_result_t request_to_node (
      const zlink::routing_id_t &target,
      const std::vector<zlink::message_t> &parts,
      operation_id_t &operation,
      std::chrono::milliseconds timeout);
    zlink::submit_result_t send_to_channel (
      const std::string &channel_name,
      const std::vector<zlink::message_t> &parts);
    zlink::submit_result_t request_to_channel (
      const std::string &channel_name,
      const std::vector<zlink::message_t> &parts,
      operation_id_t &operation,
      std::chrono::milliseconds timeout);
    std::size_t dispatch_ready (
      const std::function<void (const ready_record_t &,
                                const receive_record_t &,
                                std::vector<zlink::message_t>)> &dispatch);
    bool prepare_actor_transfer (const actor_transfer_prepare_t &prepare,
                                 actor_transfer_token_t &token,
                                 actor_transfer_prepare_result_t &result);
    bool reply (const reply_token_t &token,
                const std::vector<zlink::message_t> &parts);

    static actor_ref_t remote_actor_ref (
      const zlink::routing_id_t &node,
      std::string actor_id,
      std::uint64_t generation)
    {
        return actor_ref_t (
          node_rid_t::from_string (node.to_string ()), {},
          std::move (actor_id), generation);
    }

    std::optional<stateful::object_ref_t>
    resolve_actor (const actor_ref_t &actor) const;
    std::optional<stateful::object_ref_t>
    resolve_spot (const zlink::routing_id_t &spot) const;

  private:
    friend class spot_handle_t;
    friend class actor_handle_t;
    friend class actor_transfer_token_t;

    protocol::application_payload_t encode_application (
      const std::vector<zlink::message_t> &parts,
      std::span<const std::uint8_t> metadata = {}) const;
    std::vector<zlink::message_t> decode_application (
      const protocol::application_payload_t &payload) const;
    actor_ref_t framework_actor_ref (
      const stateful::object_ref_t &object,
      std::string actor_type) const;
    operation_id_t next_operation ();
    void complete_operation (operation_id_t operation,
                             operation_kind_t kind,
                             foundation::operation_terminal_t terminal,
                             std::vector<std::uint8_t> payload);

    host_options_t _options;
    std::shared_ptr<mesh::raw_mesh_node_owner_t> _transport;
    stateful::stateful_object_runtime_t _objects;
    stateful::stream_session_registry_t _sessions;
    std::unique_ptr<stateful::maintenance_runtime_t> _maintenance;
    std::unique_ptr<stateful::host_maintenance_runtime_t> _termination;
    std::function<void ()> _maintenance_started;
    std::function<void ()> _maintenance_closing;
    mutable std::mutex _mutex;
    std::map<std::pair<std::uint64_t, std::uint64_t>,
             std::pair<receive_record_t, std::vector<zlink::message_t>>>
      _completions;
    std::map<std::string, stateful::object_ref_t> _spots;
    std::map<std::string, std::pair<std::string, stateful::object_ref_t>> _actors;
    std::map<std::string, std::string> _peer_endpoints;
    std::uint64_t _next_operation = 1;
    bool _started = false;
};

zlink::submit_result_t reply (const reply_token_t &token,
                              const std::vector<zlink::message_t> &parts);
bool actor_join_reply (const reply_token_t &token,
                       actor_join_result_t result,
                       const std::vector<zlink::message_t> &parts);

using mesh_node_t = public_host_runtime_t;
using spot_t = spot_handle_t;
using actor_t = actor_handle_t;

} // namespace zlink::framework::runtime::host
