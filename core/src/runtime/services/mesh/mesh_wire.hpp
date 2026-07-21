/* SPDX-License-Identifier: MPL-2.0 */

#ifndef ZLINK_RUNTIME_SERVICES_MESH_WIRE_HPP_INCLUDED
#define ZLINK_RUNTIME_SERVICES_MESH_WIRE_HPP_INCLUDED

#include "services/mesh/mesh_runtime.hpp"

//  RouteMesh wire: one ROUTER socket per MeshNode plus an ingress thread.
//  The envelope, admission handshake and operation correlation live here and
//  are never exposed through the public surface.
namespace zlink
{
namespace mesh
{
//  Versioned service envelope types (frame 0 of every wire message).
enum wire_type_t
{
    wire_hello = 1,
    wire_admit = 2,
    wire_reject = 3,
    wire_update = 4,
    wire_node_send = 16,
    wire_node_request = 17,
    wire_channel_send = 18,
    wire_channel_request = 19,
    wire_reply = 20,
    wire_spot_send = 21,
    wire_spot_request = 22,
    wire_multicast = 23,
    wire_actor_send = 24,
    wire_actor_request = 25,
    wire_actor_lookup = 26,
    wire_actor_destroy = 27,
    wire_actor_join = 28,
    wire_actor_left = 29,
    wire_transfer_ready = 30,
    wire_transfer_data = 31,
    wire_transfer_ack = 32,
    wire_reply_relay = 33,
    wire_transfer_seal = 34,
    wire_transfer_complete = 35,
    wire_bound_session_send = 36,
    wire_actor_joined = 37,
    wire_bound_session_bind = 38,
    wire_instance_spot = 39
};

//  Creates, configures and binds the node's ROUTER socket, resolves the
//  actual bind endpoint into node_->bind_endpoint and spawns the ingress
//  thread. Called from start after configuration validation, without the
//  node mutex held. Returns 0 or -1 with errno.
int wire_start (mesh_node_t *node_);

//  Stops the ingress thread and closes the wire. Idempotent.
void wire_stop (mesh_node_t *node_);

//  Issues the transport connect for a peer intent endpoint.
int wire_connect_endpoint (mesh_node_t *node_, const std::string &endpoint_);

//  Retires a configured outbound connector, including its reconnect state.
//  ENOENT is returned when the connector already disappeared.
int wire_disconnect_endpoint (mesh_node_t *node_, const std::string &endpoint_);

//  Retires the currently routed transport for an inbound-observed peer.
int wire_disconnect_peer (mesh_node_t *node_, const rid_bytes_t &peer_rid_);

//  Snapshots the current descriptor and admitted peers under the node mutex,
//  then sends the update after releasing it.
void wire_broadcast_update (mesh_node_t *node_);

//  Data submit to an admitted peer pipe. correlation_ is the requester-side
//  operation serial for request types and unused otherwise. channel_ names
//  the receiver-side selection for channel types.
zlink_submit_result_t wire_submit_data (mesh_node_t *node_,
                                        const rid_bytes_t &peer_rid_,
                                        wire_type_t type_,
                                        uint64_t correlation_,
                                        const std::string &channel_,
                                        const zlink_mesh_metadata_view_t *metadata_,
                                        const zlink_msg_t *parts_,
                                        size_t part_count_,
                                        zlink_send_flags_t flags_,
                                        const send_ready_interest_t *interest_ = NULL);

//  Spot direct submit to an admitted peer. The target Spot is addressed by
//  rid + lifecycle generation; absence and generation conflicts come back as
//  terminal completions for requests.
zlink_submit_result_t wire_submit_spot (mesh_node_t *node_,
                                        const rid_bytes_t &peer_rid_,
                                        bool is_request_,
                                        uint64_t correlation_,
                                        const rid_bytes_t &source_spot_rid_,
                                        const rid_bytes_t &target_spot_rid_,
                                        uint64_t target_spot_generation_,
                                        const zlink_mesh_metadata_view_t *metadata_,
                                        const zlink_msg_t *parts_,
                                        size_t part_count_,
                                        zlink_send_flags_t flags_,
                                        const send_ready_interest_t *interest_ = NULL,
                                        uint64_t expected_connection_id_ = 0);

//  Cold Instance Spot traffic carries the Framework-selected placement. The
//  target node validates the exact node generation before exposing activation.
zlink_submit_result_t wire_submit_instance (
  mesh_node_t *node_,
  const instance_placement_value_t &target_,
  bool is_request_,
  uint64_t correlation_,
  uint32_t timeout_ms_,
  const rid_bytes_t &source_spot_rid_,
  const zlink_mesh_metadata_view_t *metadata_,
  const zlink_msg_t *parts_,
  size_t part_count_,
  zlink_send_flags_t flags_,
  const send_ready_interest_t *interest_ = NULL,
  uint64_t *connection_id_out_ = NULL,
  uint64_t expected_connection_id_ = 0);

//  Moves a placement record once to an exact Ready Spot after a remote
//  location CAS loss. A request keeps its original reply route through a
//  sealed relay serial; success consumes record_.
zlink_submit_result_t wire_redirect_instance (
  mesh_node_t *node_,
  const instance_placement_value_t &target_,
  uint64_t target_spot_generation_,
  std::unique_ptr<queued_record_t> &record_);

//  Remote leg of a Logical Multicast. Each target is submitted independently
//  through the node ROUTER with the caller's flags; successful earlier
//  submissions remain committed if a later target is backpressured.
zlink_submit_result_t wire_publish_remote (mesh_node_t *node_,
                                           const std::vector<rid_bytes_t> &targets_,
                                           const std::string &channel_,
                                           const std::string &topic_,
                                           const rid_bytes_t &source_spot_rid_,
                                           const zlink_mesh_metadata_view_t *metadata_,
                                           const zlink_msg_t *parts_,
                                           size_t part_count_,
                                           zlink_send_flags_t flags_,
                                           uint32_t *admitted_out_,
                                           uint32_t *dropped_out_,
                                           uint32_t *unreachable_out_);

//  Actor data submit to the ActorRef's node pipe. source_actor_ is NULL for
//  node-originated calls.
zlink_submit_result_t wire_submit_actor_data (mesh_node_t *node_,
                                              const rid_bytes_t &peer_rid_,
                                              bool is_request_,
                                              uint64_t correlation_,
                                              const zlink_actor_ref_t *source_actor_,
                                              const zlink_actor_ref_t &target_actor_,
                                              const zlink_mesh_metadata_view_t *metadata_,
                                              const zlink_msg_t *parts_,
                                              size_t part_count_,
                                              zlink_send_flags_t flags_,
                                              const send_ready_interest_t *interest_ = NULL);

//  Actor data originating from a STREAM binding. The receiver records the
//  reverse node route before admitting the actor message.
zlink_submit_result_t wire_submit_bound_actor_data (
  mesh_node_t *node_,
  const rid_bytes_t &peer_rid_,
  bool is_request_,
  uint64_t correlation_,
  const rid_bytes_t &source_session_rid_,
  uint64_t source_binding_generation_,
  const zlink_actor_ref_t &target_actor_,
  const zlink_mesh_metadata_view_t *metadata_,
  const zlink_msg_t *parts_,
  size_t part_count_,
  zlink_send_flags_t flags_,
  const send_ready_interest_t *interest_ = NULL);

//  Reverse leg of a transferred actor's bound STREAM session. The target
//  actor routes payload back to the node that physically owns the session.
zlink_submit_result_t wire_submit_bound_session (
  mesh_node_t *node_,
  const rid_bytes_t &peer_rid_,
  const zlink_actor_ref_t &actor_,
  uint64_t expected_binding_generation_,
  const zlink_msg_t *parts_,
  size_t part_count_,
  zlink_send_flags_t flags_);

//  Announces one STREAM binding generation to an admitted peer. Generation
//  zero is a tombstone and retired_binding_generation identifies the exact
//  cached generation that may be removed.
zlink_submit_result_t wire_submit_bound_session_bind (
  mesh_node_t *node_,
  const rid_bytes_t &peer_rid_,
  uint64_t correlation_,
  const zlink_actor_ref_t &actor_,
  uint64_t binding_generation_,
  uint64_t retired_binding_generation_ = 0);

void wire_submit_bound_session_bind_reply (
  mesh_node_t *node_,
  const rid_bytes_t &peer_rid_,
  uint64_t correlation_,
  int32_t terminal_result_,
  int32_t failure_errno_,
  uint64_t binding_generation_,
  uint64_t membership_epoch_);

//  Announces a STREAM binding to every currently admitted peer. Actor owners
//  and transfer targets retain the reverse route to the node that owns the
//  physical session. Generation zero conditionally removes the retired
//  generation so a delayed tombstone cannot erase a newer rebind.
void wire_broadcast_bound_session_bind (mesh_node_t *node_,
                                        const zlink_actor_ref_t &actor_,
                                        uint64_t binding_generation_,
                                        uint64_t retired_binding_generation_ = 0);

//  Remote actor infrastructure operations (lookup / destroy / join). The
//  join carries creation parts as payload frames; entry_ addresses the
//  target node's entry Spot without a generation.
zlink_submit_result_t wire_submit_actor_lookup (mesh_node_t *node_,
                                                const rid_bytes_t &peer_rid_,
                                                uint64_t correlation_,
                                                const std::string &actor_id_);
zlink_submit_result_t wire_submit_actor_destroy (mesh_node_t *node_,
                                                 const rid_bytes_t &peer_rid_,
                                                 uint64_t correlation_,
                                                 const zlink_actor_ref_t &actor_);
zlink_submit_result_t wire_submit_actor_join (mesh_node_t *node_,
                                              const rid_bytes_t &peer_rid_,
                                              uint64_t correlation_,
                                              const zlink_actor_ref_t &actor_,
                                              bool entry_,
                                              const rid_bytes_t &target_spot_rid_,
                                              uint64_t target_spot_generation_,
                                              const zlink_msg_t *creation_parts_,
                                              size_t creation_part_count_);

//  One-way lifecycle notification to the previous Spot's node after the
//  actor moved away.
void wire_notify_actor_left (mesh_node_t *node_,
                             const rid_bytes_t &peer_rid_,
                             const zlink_actor_ref_t &actor_,
                             const rid_bytes_t &previous_spot_rid_,
                             uint64_t previous_spot_generation_,
                             uint64_t previous_membership_epoch_,
                             uint64_t current_membership_epoch_);

//  One-way post-commit notification to the target Spot. This is distinct
//  from wire_actor_join, which asks the target to accept or reject admission.
void wire_notify_actor_joined (mesh_node_t *node_,
                               const rid_bytes_t &peer_rid_,
                               const zlink_actor_ref_t &actor_,
                               const rid_bytes_t &previous_spot_rid_,
                               uint64_t previous_spot_generation_,
                               uint64_t previous_membership_epoch_,
                               const rid_bytes_t &current_spot_rid_,
                               uint64_t current_spot_generation_,
                               uint64_t current_membership_epoch_);

//  Sends a join verdict back to the requester with the actual joined Spot
//  address (used by the remote-origin branch of zlink_actor_join_reply).
zlink_submit_result_t wire_submit_join_reply (mesh_node_t *node_,
                                              const rid_bytes_t &peer_rid_,
                                              uint64_t correlation_,
                                              uint32_t join_result_,
                                              const rid_bytes_t &spot_rid_,
                                              uint64_t spot_generation_,
                                              const zlink_msg_t *parts_,
                                              size_t part_count_,
                                              zlink_send_flags_t flags_);

//  Sends a lookup verdict with the actor location fields.
zlink_submit_result_t wire_submit_lookup_reply (mesh_node_t *node_,
                                                const rid_bytes_t &peer_rid_,
                                                uint64_t correlation_,
                                                const zlink_actor_ref_t &ref_,
                                                const rid_bytes_t &spot_rid_,
                                                uint64_t spot_generation_,
                                                uint64_t membership_epoch_);

//  Transfer data plane. READY is the readiness exchange (both directions),
//  DATA carries one frozen record with its participant sequence, ACK returns
//  the target's contiguous staged high-water and REPLY_RELAY routes a reply
//  for a transferred request back through the source's original route.
zlink_submit_result_t wire_submit_transfer_ready (mesh_node_t *node_,
                                                  const rid_bytes_t &peer_rid_,
                                                  const zlink_actor_transfer_id_t &transfer_id_,
                                                  const zlink_actor_ref_t &actor_,
                                                  uint64_t expected_epoch_,
                                                  uint64_t final_sequence_,
                                                  uint8_t role_,
                                                  uint64_t offered_messages_,
                                                  uint64_t offered_bytes_,
                                                  const std::vector<transfer_participant_descriptor_t>
                                                    &participants_);
zlink_submit_result_t wire_submit_transfer_data (mesh_node_t *node_,
                                                 const rid_bytes_t &peer_rid_,
                                                 const zlink_actor_transfer_id_t &transfer_id_,
                                                 uint64_t participant_id_,
                                                 uint64_t sequence_,
                                                 const queued_record_t &record_,
                                                 uint64_t relay_serial_);
zlink_submit_result_t wire_submit_transfer_ack (mesh_node_t *node_,
                                                const rid_bytes_t &peer_rid_,
                                                const zlink_actor_transfer_id_t &transfer_id_,
                                                uint64_t participant_id_,
                                                uint64_t high_water_);
zlink_submit_result_t wire_submit_transfer_seal (
  mesh_node_t *node_,
  const rid_bytes_t &peer_rid_,
  const zlink_actor_transfer_id_t &transfer_id_,
  bool response_,
  const std::vector<transfer_participant_terminal_t> &terminals_);
zlink_submit_result_t wire_submit_transfer_complete (
  mesh_node_t *node_,
  const rid_bytes_t &peer_rid_,
  const zlink_actor_transfer_id_t &transfer_id_);
zlink_submit_result_t wire_submit_reply_relay (mesh_node_t *node_,
                                               const rid_bytes_t &peer_rid_,
                                               uint64_t relay_serial_,
                                               int32_t terminal_result_,
                                               int32_t failure_errno_,
                                               const zlink_msg_t *parts_,
                                               size_t part_count_,
                                               uint64_t expected_connection_id_ = 0);

//  Terminal reply for a remote-origin request. Success carries the reply
//  parts; failures carry the terminal result and errno instead.
zlink_submit_result_t wire_submit_reply (mesh_node_t *node_,
                                         const rid_bytes_t &peer_rid_,
                                         uint64_t correlation_,
                                         int32_t terminal_result_,
                                         int32_t failure_errno_,
                                         const zlink_msg_t *parts_,
                                         size_t part_count_,
                                         uint64_t expected_connection_id_ = 0);
}
}

#endif
