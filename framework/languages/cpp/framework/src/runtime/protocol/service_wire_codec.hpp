/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include "runtime/mesh/service_topology_registry.hpp"

#include <service_wire_constants.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <stdexcept>
#include <utility>
#include <vector>

namespace zlink::framework::runtime::protocol
{

class service_wire_error_t : public std::runtime_error
{
  public:
    using std::runtime_error::runtime_error;
};

struct liveness_record_t
{
    command kind;
    std::uint64_t probe_id;
};

struct service_wire_header_t
{
    command kind;
    std::uint8_t flags;
};

struct application_payload_t
{
    std::string packet_name;
    std::string content_type;
    std::vector<std::uint8_t> payload;

    friend bool operator== (const application_payload_t &,
                            const application_payload_t &) = default;
};

struct spot_route_fence_t
{
    std::string spot_id;
    std::uint64_t object_generation = 0;
    std::vector<std::uint8_t> target_node_routing_id;
    std::uint64_t target_node_generation = 0;
    std::uint64_t authority_owner_generation = 0;

    friend bool operator== (const spot_route_fence_t &,
                            const spot_route_fence_t &) = default;
};

struct actor_route_fence_t
{
    std::string actor_id;
    std::uint64_t object_generation = 0;
    std::vector<std::uint8_t> target_node_routing_id;
    std::uint64_t target_node_generation = 0;
    std::uint64_t authority_owner_generation = 0;
    std::uint64_t owner_lease_generation = 0;

    friend bool operator== (const actor_route_fence_t &,
                            const actor_route_fence_t &) = default;
};

struct spot_message_header_t
{
    std::optional<std::uint64_t> correlation;
    std::string source_spot_id;
    spot_route_fence_t target;
};

struct actor_message_header_t
{
    std::optional<std::uint64_t> correlation;
    std::optional<std::pair<std::string, std::uint64_t>> source_actor;
    actor_route_fence_t target;
};

struct wire_operation_id_t
{
    std::uint64_t high = 0;
    std::uint64_t low = 0;

    friend bool operator== (const wire_operation_id_t &,
                            const wire_operation_id_t &) = default;
};

struct relocation_id_t
{
    std::uint64_t high = 0;
    std::uint64_t low = 0;

    friend bool operator== (const relocation_id_t &,
                            const relocation_id_t &) = default;
};

struct relocation_coordinator_fence_t
{
    std::string owner_id;
    std::uint64_t lease_generation = 0;
    std::vector<std::uint8_t> node_routing_id;
    std::uint64_t node_generation = 0;
    std::string expected_authority_store_version;

    friend bool operator== (const relocation_coordinator_fence_t &,
                            const relocation_coordinator_fence_t &) = default;
};

struct actor_identity_t
{
    std::string actor_id;
    std::uint64_t object_generation = 0;

    friend bool operator== (const actor_identity_t &,
                            const actor_identity_t &) = default;
};

enum class relocation_role_t : std::uint8_t
{
    source = 1,
    target = 2,
    coordinator = 3
};

enum class session_relocation_route_action_t : std::uint8_t
{
    commit = 1,
    abort = 2
};

struct session_relocation_route_update_t
{
    session_relocation_route_action_t action =
      session_relocation_route_action_t::commit;
    std::uint64_t previous_authority_owner_generation = 0;
    std::uint64_t target_authority_owner_generation = 0;
    std::vector<std::uint8_t> target_node_routing_id;
    std::uint64_t target_node_generation = 0;
    std::uint64_t replayed_high_water = 0;
    std::uint64_t current_authority_owner_generation = 0;

    friend bool operator== (const session_relocation_route_update_t &,
                            const session_relocation_route_update_t &) = default;
};

struct session_relocation_seal_t
{
    relocation_id_t relocation;
    relocation_coordinator_fence_t coordinator;
    relocation_role_t sender_role = relocation_role_t::source;
    actor_route_fence_t actor;
    std::vector<std::uint8_t> session_owner_node_routing_id;
    std::uint64_t session_owner_node_generation = 0;
    std::string session_owner_id;
    std::uint64_t session_owner_lease_generation = 0;
    std::vector<std::uint8_t> session_routing_id;
    std::uint64_t binding_generation = 0;

    friend bool operator== (const session_relocation_seal_t &,
                            const session_relocation_seal_t &) = default;
};

struct session_relocation_sealed_t
{
    relocation_id_t relocation;
    relocation_coordinator_fence_t coordinator;
    actor_route_fence_t actor;
    std::vector<std::uint8_t> session_owner_node_routing_id;
    std::uint64_t session_owner_node_generation = 0;
    std::string session_owner_id;
    std::uint64_t session_owner_lease_generation = 0;
    std::vector<std::uint8_t> session_routing_id;
    std::uint64_t binding_generation = 0;
    std::uint64_t last_accepted_session_sequence = 0;

    friend bool operator== (const session_relocation_sealed_t &,
                            const session_relocation_sealed_t &) = default;
};

struct session_relocation_route_t
{
    relocation_id_t relocation;
    relocation_coordinator_fence_t coordinator;
    relocation_role_t sender_role = relocation_role_t::target;
    actor_identity_t actor;
    std::vector<std::uint8_t> session_owner_node_routing_id;
    std::uint64_t session_owner_node_generation = 0;
    std::string session_owner_id;
    std::uint64_t session_owner_lease_generation = 0;
    std::vector<std::uint8_t> session_routing_id;
    std::uint64_t binding_generation = 0;
    session_relocation_route_update_t route;

    friend bool operator== (const session_relocation_route_t &,
                            const session_relocation_route_t &) = default;
};

struct session_relocation_routed_t
{
    relocation_id_t relocation;
    relocation_coordinator_fence_t coordinator;
    actor_identity_t actor;
    std::vector<std::uint8_t> session_owner_node_routing_id;
    std::uint64_t session_owner_node_generation = 0;
    std::string session_owner_id;
    std::uint64_t session_owner_lease_generation = 0;
    std::vector<std::uint8_t> session_routing_id;
    std::uint64_t binding_generation = 0;
    session_relocation_route_action_t action =
      session_relocation_route_action_t::commit;
    std::uint64_t current_authority_owner_generation = 0;
    std::uint64_t last_accepted_session_sequence = 0;

    friend bool operator== (const session_relocation_routed_t &,
                            const session_relocation_routed_t &) = default;
};

struct reply_header_t
{
    std::uint64_t correlation;
    std::uint32_t terminal_result;
    std::uint32_t failure_code;
};

struct user_spot_reservation_fence_t
{
    std::string reservation_id;
    std::string expected_store_version;
    std::uint64_t object_generation = 0;
    std::uint64_t authority_owner_generation = 0;
    std::vector<std::uint8_t> target_node_routing_id;
    std::uint64_t target_node_generation = 0;
    std::string target_owner_id;
    std::uint64_t target_owner_lease_generation = 0;
    std::uint32_t pending_capacity_delta = 0;

    friend bool operator== (const user_spot_reservation_fence_t &,
                            const user_spot_reservation_fence_t &) = default;
};

struct user_spot_create_header_t
{
    std::uint64_t correlation = 0;
    wire_operation_id_t operation;
    std::vector<std::uint8_t> source_node_routing_id;
    std::uint64_t source_node_generation = 0;
    std::string spot_id;
    std::string stable_type;
    user_spot_reservation_fence_t reservation;
    std::uint64_t deadline_unix_ms = 0;

    friend bool operator== (const user_spot_create_header_t &,
                            const user_spot_create_header_t &) = default;
};

struct instance_spot_activation_target_t
{
    std::vector<std::uint8_t> target_node_routing_id;
    std::uint64_t target_node_generation = 0;
    std::string spot_id;
    std::string mesh_name;
    std::string stable_type;
    std::string descriptor_version;
    std::uint64_t deadline_unix_ms = 0;

    friend bool operator== (const instance_spot_activation_target_t &,
                            const instance_spot_activation_target_t &) = default;
};

struct instance_spot_activation_header_t
{
    instance_spot_activation_target_t target;
    std::uint64_t source_node_generation = 0;
    std::vector<std::uint8_t> source_node_routing_id;
    std::optional<std::string> source_spot_id;
    bool request = false;
    wire_operation_id_t operation;
    std::uint64_t reply_route_id = 0;
    bool has_metadata = false;

    friend bool operator== (const instance_spot_activation_header_t &,
                            const instance_spot_activation_header_t &) = default;
};

struct instance_activation_recovery_t
{
    instance_spot_activation_header_t activation;
    std::optional<std::vector<std::uint8_t>> metadata;
    application_payload_t application_payload;

    friend bool operator== (const instance_activation_recovery_t &,
                            const instance_activation_recovery_t &) = default;
};

struct user_spot_close_fence_t
{
    std::string spot_id;
    std::uint64_t object_generation = 0;
    std::vector<std::uint8_t> target_node_routing_id;
    std::uint64_t target_node_generation = 0;
    std::uint64_t authority_owner_generation = 0;
    std::string expected_store_version;

    friend bool operator== (const user_spot_close_fence_t &,
                            const user_spot_close_fence_t &) = default;
};

struct user_spot_close_header_t
{
    std::uint64_t correlation = 0;
    wire_operation_id_t operation;
    std::vector<std::uint8_t> source_node_routing_id;
    std::uint64_t source_node_generation = 0;
    user_spot_close_fence_t target;
    std::uint64_t deadline_unix_ms = 0;

    friend bool operator== (const user_spot_close_header_t &,
                            const user_spot_close_header_t &) = default;
};

enum class user_spot_create_result_t : std::uint8_t
{
    existing = 1,
    created = 2,
    rejected = 3
};

struct user_spot_create_reply_t
{
    reply_header_t header;
    user_spot_create_result_t result =
      user_spot_create_result_t::rejected;
    std::string spot_id;
    std::uint64_t object_generation = 0;
};

struct user_spot_close_reply_t
{
    reply_header_t header;
    bool closed = false;
};

struct client_server_client_admission_t
{
    std::string channel_name;
    std::string security_identity;
    std::uint32_t effective_max_message_bytes = 0;

    friend bool operator== (const client_server_client_admission_t &,
                            const client_server_client_admission_t &) = default;
};

struct client_server_server_admission_t
{
    std::string channel_name;
    std::vector<std::uint8_t> server_routing_id;
    std::uint64_t lifecycle_generation = 0;
    std::uint64_t descriptor_revision = 0;
    std::uint32_t weight = 100;
    mesh::service_node_state_t state =
      mesh::service_node_state_t::preparing;
    std::string security_identity;
    std::uint32_t effective_max_message_bytes = 0;
    std::string advertised_endpoint;

    friend bool operator== (const client_server_server_admission_t &,
                            const client_server_server_admission_t &) = default;
};

std::vector<std::uint8_t> encode_node_send_header ();
std::vector<std::uint8_t>
encode_node_request_header (std::uint64_t correlation);
std::uint64_t
decode_node_request_header (std::span<const std::uint8_t> bytes);
std::vector<std::uint8_t>
encode_channel_request_header (std::uint64_t correlation,
                               const std::string &channel_name);
struct channel_request_header_t
{
    std::uint64_t correlation;
    std::string channel_name;
};
channel_request_header_t
decode_channel_request_header (std::span<const std::uint8_t> bytes);
std::vector<std::uint8_t>
encode_channel_send_header (const std::string &channel_name);
std::vector<std::uint8_t> encode_spot_message_header (
  command kind,
  const std::string &source_spot_id,
  const spot_route_fence_t &target,
  std::optional<std::uint64_t> correlation = std::nullopt);
spot_message_header_t decode_spot_message_header (
  std::span<const std::uint8_t> bytes,
  command expected_kind);
std::vector<std::uint8_t> encode_actor_message_header (
  command kind,
  const std::optional<std::pair<std::string, std::uint64_t>> &source_actor,
  const actor_route_fence_t &target,
  std::optional<std::uint64_t> correlation = std::nullopt);
actor_message_header_t decode_actor_message_header (
  std::span<const std::uint8_t> bytes,
  command expected_kind);
std::vector<std::uint8_t> encode_session_relocation_route (
  const session_relocation_route_t &record);
session_relocation_route_t decode_session_relocation_route (
  std::span<const std::uint8_t> bytes);
std::vector<std::uint8_t> encode_session_relocation_seal (
  const session_relocation_seal_t &record);
session_relocation_seal_t decode_session_relocation_seal (
  std::span<const std::uint8_t> bytes);
std::vector<std::uint8_t> encode_session_relocation_sealed (
  const session_relocation_sealed_t &record);
session_relocation_sealed_t decode_session_relocation_sealed (
  std::span<const std::uint8_t> bytes);
std::vector<std::uint8_t> encode_session_relocation_routed (
  const session_relocation_routed_t &record);
session_relocation_routed_t decode_session_relocation_routed (
  std::span<const std::uint8_t> bytes);
std::vector<std::uint8_t> encode_user_spot_create_header (
  const user_spot_create_header_t &record);
user_spot_create_header_t decode_user_spot_create_header (
  std::span<const std::uint8_t> bytes);
std::vector<std::uint8_t> encode_instance_spot_activation_header (
  const instance_spot_activation_header_t &record);
instance_spot_activation_header_t decode_instance_spot_activation_header (
  std::span<const std::uint8_t> bytes);
std::vector<std::uint8_t> encode_instance_activation_recovery (
  const instance_activation_recovery_t &record);
instance_activation_recovery_t decode_instance_activation_recovery (
  std::span<const std::uint8_t> bytes);
std::vector<std::uint8_t> encode_user_spot_close_header (
  const user_spot_close_header_t &record);
user_spot_close_header_t decode_user_spot_close_header (
  std::span<const std::uint8_t> bytes);
service_wire_header_t decode_header (std::span<const std::uint8_t> bytes);
std::string
decode_channel_send_header (std::span<const std::uint8_t> bytes);
std::vector<std::uint8_t>
encode_application_payload (const application_payload_t &payload);
application_payload_t
decode_application_payload (std::span<const std::uint8_t> bytes);
std::vector<std::uint8_t>
encode_route_mesh_admission (command kind,
                             const mesh::service_node_descriptor_t &descriptor);
mesh::service_node_descriptor_t
decode_route_mesh_admission (std::span<const std::uint8_t> bytes,
                             command expected_kind,
                             std::vector<std::uint8_t> source_routing_id);
std::vector<std::uint8_t> encode_client_server_client_admission (
  command kind,
  const client_server_client_admission_t &admission);
client_server_client_admission_t decode_client_server_client_admission (
  std::span<const std::uint8_t> bytes,
  command expected_kind);
std::vector<std::uint8_t> encode_client_server_server_admission (
  command kind,
  const client_server_server_admission_t &admission);
client_server_server_admission_t decode_client_server_server_admission (
  std::span<const std::uint8_t> bytes,
  command expected_kind);
std::vector<std::uint8_t> encode_reject (std::uint32_t reason);
std::uint32_t decode_reject (std::span<const std::uint8_t> bytes);
std::vector<std::uint8_t>
encode_reply_header (std::uint64_t correlation,
                     std::uint32_t terminal_result,
                     std::uint32_t failure_code);
reply_header_t decode_reply_header (std::span<const std::uint8_t> bytes);
std::vector<std::uint8_t> encode_user_spot_create_reply (
  std::uint64_t correlation,
  std::uint32_t terminal_result,
  std::uint32_t failure_code,
  user_spot_create_result_t result,
  const std::string &spot_id,
  std::uint64_t object_generation);
user_spot_create_reply_t decode_user_spot_create_reply (
  std::span<const std::uint8_t> bytes);
std::vector<std::uint8_t> encode_user_spot_close_reply (
  std::uint64_t correlation,
  std::uint32_t terminal_result,
  std::uint32_t failure_code,
  bool closed);
user_spot_close_reply_t decode_user_spot_close_reply (
  std::span<const std::uint8_t> bytes);
std::vector<std::uint8_t> encode_liveness (command kind, std::uint64_t probe_id);
liveness_record_t decode_liveness (std::span<const std::uint8_t> bytes);

} // namespace zlink::framework::runtime::protocol
