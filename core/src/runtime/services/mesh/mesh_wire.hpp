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
    wire_transfer_complete = 35
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

//  Sends the current descriptor to every admitted peer. Node mutex held.
void wire_broadcast_update_locked (mesh_node_t *node_);

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
                                        zlink_send_flags_t flags_);

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
                                        zlink_send_flags_t flags_);

//  Remote leg of a Logical Multicast. Caller holds the node mutex; probes
//  every target pipe first so NODROP commits to all snapshot targets or to
//  none. Fills admitted/dropped counts and returns BACKPRESSURED (EAGAIN)
//  when NODROP cannot reserve every target.
zlink_submit_result_t wire_publish_remote_locked (mesh_node_t *node_,
                                                  const std::vector<rid_bytes_t> &targets_,
                                                  const std::string &channel_,
                                                  const std::string &topic_,
                                                  const rid_bytes_t &source_spot_rid_,
                                                  int nodrop_,
                                                  const zlink_mesh_metadata_view_t *metadata_,
                                                  const zlink_msg_t *parts_,
                                                  size_t part_count_,
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
                                              zlink_send_flags_t flags_);

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
                                               size_t part_count_);

//  Terminal reply for a remote-origin request. Success carries the reply
//  parts; failures carry the terminal result and errno instead.
zlink_submit_result_t wire_submit_reply (mesh_node_t *node_,
                                         const rid_bytes_t &peer_rid_,
                                         uint64_t correlation_,
                                         int32_t terminal_result_,
                                         int32_t failure_errno_,
                                         const zlink_msg_t *parts_,
                                         size_t part_count_);
}
}

#endif
