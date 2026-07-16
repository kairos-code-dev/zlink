/* SPDX-License-Identifier: MPL-2.0 */

#ifndef ZLINK_SERVICE_ACTOR_H_INCLUDED
#define ZLINK_SERVICE_ACTOR_H_INCLUDED

#include <zlink/common.h>
#include <zlink/message/api.h>
#include <zlink/service/mesh_node.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Actor service: ActorRef lifecycle, Spot membership and transfer fence.
   Contract: core/doc/spec/core/service/04-actor.md
   zlink_actor_ref_t is declared in zlink/service/common.h. */

#define ZLINK_ACTOR_ABI_VERSION 1u


typedef enum zlink_actor_lifecycle_kind_t {
  ZLINK_ACTOR_LIFECYCLE_CREATED      = 1,
  ZLINK_ACTOR_LIFECYCLE_JOINED       = 2,
  ZLINK_ACTOR_LIFECYCLE_LEFT         = 3,
  ZLINK_ACTOR_LIFECYCLE_DISCONNECTED = 4,
  ZLINK_ACTOR_LIFECYCLE_DESTROYED    = 5
} zlink_actor_lifecycle_kind_t;

typedef enum zlink_actor_join_result_t {
  ZLINK_ACTOR_JOIN_ACCEPTED = 0,
  ZLINK_ACTOR_JOIN_REJECTED = 1
} zlink_actor_join_result_t;

typedef enum zlink_actor_transfer_role_t {
  ZLINK_ACTOR_TRANSFER_SOURCE = 1,
  ZLINK_ACTOR_TRANSFER_TARGET = 2
} zlink_actor_transfer_role_t;

typedef enum zlink_actor_transfer_phase_t {
  ZLINK_ACTOR_TRANSFER_PREPARING = 1,
  ZLINK_ACTOR_TRANSFER_FENCED    = 2,
  ZLINK_ACTOR_TRANSFER_COMMITTED = 3,
  ZLINK_ACTOR_TRANSFER_ACTIVATED = 4,
  ZLINK_ACTOR_TRANSFER_ABORTED   = 5
} zlink_actor_transfer_phase_t;

typedef struct zlink_actor_location_t {
  uint32_t struct_size;
  uint32_t version;
  zlink_actor_ref_t actor;
  zlink_routing_id_t spot_rid;
  uint64_t spot_generation;
  uint64_t membership_epoch;
} zlink_actor_location_t;

typedef struct zlink_actor_control_record_t {
  uint32_t struct_size;
  uint32_t version;
  zlink_actor_lifecycle_kind_t kind;
  zlink_actor_ref_t previous_actor;
  zlink_actor_ref_t current_actor;
  zlink_routing_id_t previous_spot_rid;
  zlink_routing_id_t current_spot_rid;
  uint64_t previous_spot_generation;
  uint64_t current_spot_generation;
  uint64_t previous_membership_epoch;
  uint64_t current_membership_epoch;
  int32_t result_code;
} zlink_actor_control_record_t;

typedef struct zlink_actor_join_completion_t {
  uint32_t struct_size;
  uint32_t version;
  zlink_actor_join_result_t join_result;
  zlink_actor_ref_t actor;
  zlink_actor_location_t location;
} zlink_actor_join_completion_t;

typedef struct zlink_actor_transfer_id_t {
  uint64_t high;
  uint64_t low;
} zlink_actor_transfer_id_t;

typedef struct zlink_actor_transfer_token_t {
  uint64_t opaque[8];
} zlink_actor_transfer_token_t;

typedef struct zlink_actor_transfer_prepare_t {
  uint32_t struct_size;
  uint32_t version;
  zlink_actor_transfer_role_t role;
  zlink_actor_transfer_id_t transfer_id;
  zlink_actor_ref_t actor;
  uint64_t expected_membership_epoch;
  zlink_routing_id_t peer_node_rid;
  uint64_t final_sequence;
  uint64_t reserve_message_count;
  uint64_t reserve_byte_count;
} zlink_actor_transfer_prepare_t;

typedef struct zlink_actor_transfer_prepare_result_t {
  uint32_t struct_size;
  uint32_t version;
  zlink_actor_transfer_role_t role;
  zlink_actor_transfer_id_t transfer_id;
  zlink_actor_ref_t actor;
  uint64_t final_sequence;
  uint64_t reserve_message_count;
  uint64_t reserve_byte_count;
} zlink_actor_transfer_prepare_result_t;

typedef struct zlink_actor_transfer_control_t {
  uint32_t struct_size;
  uint32_t version;
  zlink_actor_transfer_phase_t phase;
  zlink_actor_transfer_role_t role;
  zlink_actor_transfer_id_t transfer_id;
  zlink_actor_ref_t actor;
  uint64_t membership_epoch;
  uint64_t final_sequence;
  int32_t result_code;
  int32_t failure_errno;
} zlink_actor_transfer_control_t;
ZLINK_EXPORT zlink_request_result_t zlink_mesh_node_actor_new(
  void *mesh_node,
  const char *actor_id,
  const zlink_msg_t *creation_parts,
  size_t creation_part_count,
  zlink_actor_ref_t *actor_out,
  zlink_send_flags_t flags,
  uint32_t timeout_ms);
ZLINK_EXPORT zlink_config_result_t zlink_mesh_node_actor_lookup(
  void *mesh_node,
  const char *actor_id,
  zlink_actor_location_t *location_out);
ZLINK_EXPORT zlink_submit_result_t zlink_mesh_node_actor_lookup_remote(
  void *mesh_node,
  const zlink_routing_id_t *target_node_rid,
  const char *actor_id,
  zlink_mesh_operation_id_t *operation_id_out,
  uint32_t timeout_ms);
ZLINK_EXPORT zlink_submit_result_t zlink_mesh_node_actor_destroy(
  void *mesh_node,
  const zlink_actor_ref_t *actor,
  zlink_mesh_operation_id_t *operation_id_out,
  uint32_t timeout_ms);

ZLINK_EXPORT zlink_submit_result_t zlink_mesh_node_actor_join_spot(
  void *mesh_node,
  const zlink_actor_ref_t *actor,
  const zlink_routing_id_t *target_node_rid,
  const zlink_routing_id_t *target_spot_rid,
  uint64_t target_spot_generation,
  const zlink_msg_t *creation_parts,
  size_t creation_part_count,
  zlink_mesh_operation_id_t *operation_id_out,
  uint32_t timeout_ms);
ZLINK_EXPORT zlink_submit_result_t zlink_mesh_node_actor_join_entry_spot(
  void *mesh_node,
  const zlink_actor_ref_t *actor,
  const zlink_routing_id_t *target_node_rid,
  const zlink_msg_t *creation_parts,
  size_t creation_part_count,
  zlink_mesh_operation_id_t *operation_id_out,
  uint32_t timeout_ms);
ZLINK_EXPORT zlink_submit_result_t zlink_mesh_node_actor_leave_spot(
  void *mesh_node,
  const zlink_actor_ref_t *actor,
  uint64_t expected_membership_epoch,
  zlink_mesh_operation_id_t *operation_id_out,
  uint32_t timeout_ms);
ZLINK_EXPORT zlink_submit_result_t zlink_actor_join_reply(
  const zlink_mesh_reply_token_t *token,
  zlink_actor_join_result_t join_result,
  const zlink_msg_t *parts,
  size_t part_count,
  zlink_send_flags_t flags);

ZLINK_EXPORT zlink_submit_result_t zlink_mesh_node_send_to_actor(
  void *mesh_node,
  const zlink_actor_ref_t *actor,
  const zlink_mesh_metadata_view_t *actor_metadata,
  const zlink_msg_t *parts,
  size_t part_count,
  zlink_send_flags_t flags);
ZLINK_EXPORT zlink_submit_result_t zlink_mesh_node_request_to_actor(
  void *mesh_node,
  const zlink_actor_ref_t *actor,
  const zlink_mesh_metadata_view_t *actor_metadata,
  const zlink_msg_t *parts,
  size_t part_count,
  zlink_mesh_operation_id_t *operation_id_out,
  zlink_send_flags_t flags,
  uint32_t timeout_ms);
ZLINK_EXPORT zlink_submit_result_t zlink_actor_send_to_actor(
  void *mesh_node,
  const zlink_actor_ref_t *source_actor,
  const zlink_actor_ref_t *target_actor,
  const zlink_mesh_metadata_view_t *actor_metadata,
  const zlink_msg_t *parts,
  size_t part_count,
  zlink_send_flags_t flags);
ZLINK_EXPORT zlink_submit_result_t zlink_actor_request_to_actor(
  void *mesh_node,
  const zlink_actor_ref_t *source_actor,
  const zlink_actor_ref_t *target_actor,
  const zlink_mesh_metadata_view_t *actor_metadata,
  const zlink_msg_t *parts,
  size_t part_count,
  zlink_mesh_operation_id_t *operation_id_out,
  zlink_send_flags_t flags,
  uint32_t timeout_ms);

ZLINK_EXPORT zlink_request_result_t zlink_mesh_node_actor_transfer_prepare(
  void *mesh_node,
  const zlink_actor_transfer_prepare_t *prepare,
  uint32_t timeout_ms,
  zlink_actor_transfer_token_t *token_out,
  zlink_actor_transfer_prepare_result_t *result_out);
ZLINK_EXPORT zlink_config_result_t zlink_mesh_node_actor_transfer_commit(
  const zlink_actor_transfer_token_t *token,
  uint64_t new_membership_epoch);
ZLINK_EXPORT zlink_config_result_t zlink_mesh_node_actor_transfer_activate(
  const zlink_actor_transfer_token_t *token);
ZLINK_EXPORT zlink_config_result_t zlink_mesh_node_actor_transfer_abort(
  const zlink_actor_transfer_token_t *token);

#ifdef __cplusplus
}
#endif

#endif
