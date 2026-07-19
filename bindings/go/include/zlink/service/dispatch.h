/* SPDX-License-Identifier: MPL-2.0 */

#ifndef ZLINK_SERVICE_DISPATCH_H_INCLUDED
#define ZLINK_SERVICE_DISPATCH_H_INCLUDED

#include <zlink/common.h>
#include <zlink/message/api.h>
#include <zlink/service/common.h>

#ifdef __cplusplus
extern "C" {
#endif

/* MeshNode service dispatch: ready index, claims, batches and reply.
   Contract: core/doc/spec/core/service/02-dispatch.md */

#define ZLINK_MESH_DISPATCH_ABI_VERSION 1u

typedef uint32_t zlink_mesh_ready_domain_mask_t;

enum {
  ZLINK_MESH_READY_NONE           = 0u,
  ZLINK_MESH_READY_APPLICATION    = 1u << 0,
  ZLINK_MESH_READY_INFRASTRUCTURE = 1u << 1,
  ZLINK_MESH_READY_ALL            = (1u << 0) | (1u << 1)
};

typedef enum zlink_mesh_owner_kind_t {
  ZLINK_MESH_OWNER_NODE  = 1,
  ZLINK_MESH_OWNER_SPOT  = 2,
  ZLINK_MESH_OWNER_ACTOR = 3
} zlink_mesh_owner_kind_t;

typedef enum zlink_mesh_record_kind_t {
  ZLINK_MESH_RECORD_NODE_SEND          = 1,
  ZLINK_MESH_RECORD_NODE_REQUEST       = 2,
  ZLINK_MESH_RECORD_CHANNEL_SEND       = 3,
  ZLINK_MESH_RECORD_CHANNEL_REQUEST    = 4,
  ZLINK_MESH_RECORD_SPOT_SEND          = 5,
  ZLINK_MESH_RECORD_SPOT_REQUEST       = 6,
  ZLINK_MESH_RECORD_SPOT_MULTICAST     = 7,
  ZLINK_MESH_RECORD_SPOT_CONTROL       = 8,
  ZLINK_MESH_RECORD_ACTOR_SEND         = 9,
  ZLINK_MESH_RECORD_ACTOR_REQUEST      = 10,
  ZLINK_MESH_RECORD_COMPLETION         = 11,
  ZLINK_MESH_RECORD_SEND_READY         = 12,
  ZLINK_MESH_RECORD_TRANSFER_CONTROL   = 13
} zlink_mesh_record_kind_t;

typedef enum zlink_mesh_operation_kind_t {
  ZLINK_MESH_OPERATION_NODE_REQUEST          = 1,
  ZLINK_MESH_OPERATION_CHANNEL_REQUEST       = 2,
  ZLINK_MESH_OPERATION_SPOT_REQUEST          = 3,
  ZLINK_MESH_OPERATION_ACTOR_REQUEST         = 4,
  ZLINK_MESH_OPERATION_ACTOR_LOOKUP          = 5,
  ZLINK_MESH_OPERATION_ACTOR_DESTROY         = 6,
  ZLINK_MESH_OPERATION_ACTOR_JOIN            = 7,
  ZLINK_MESH_OPERATION_ACTOR_LEAVE           = 8,
  ZLINK_MESH_OPERATION_STREAM_BIND           = 9,
  ZLINK_MESH_OPERATION_STREAM_UNBIND         = 10,
  ZLINK_MESH_OPERATION_STREAM_CLOSE          = 11
} zlink_mesh_operation_kind_t;

typedef enum zlink_mesh_destination_kind_t {
  ZLINK_MESH_DESTINATION_NODE          = 1,
  ZLINK_MESH_DESTINATION_CHANNEL       = 2,
  ZLINK_MESH_DESTINATION_SPOT          = 3,
  ZLINK_MESH_DESTINATION_ACTOR         = 4,
  ZLINK_MESH_DESTINATION_BOUND_SESSION = 5
} zlink_mesh_destination_kind_t;

typedef struct zlink_mesh_operation_id_t {
  uint64_t high;
  uint64_t low;
} zlink_mesh_operation_id_t;

typedef struct zlink_mesh_reply_token_t {
  uint64_t opaque[4];
} zlink_mesh_reply_token_t;

typedef struct zlink_mesh_claim_t {
  uint64_t opaque[4];
} zlink_mesh_claim_t;

typedef struct zlink_mesh_ready_record_t {
  uint32_t struct_size;
  uint32_t version;
  zlink_mesh_owner_kind_t owner_kind;
  zlink_mesh_ready_domain_mask_t domain;
  zlink_routing_id_t spot_rid;
  zlink_actor_ref_t actor;
} zlink_mesh_ready_record_t;

typedef struct zlink_mesh_receive_requirements_t {
  uint32_t struct_size;
  uint32_t version;
  size_t message_count;
  size_t part_count;
  size_t byte_count;
} zlink_mesh_receive_requirements_t;

typedef struct zlink_mesh_receive_record_t {
  uint32_t struct_size;
  uint32_t version;
  zlink_mesh_record_kind_t kind;
  zlink_mesh_ready_domain_mask_t domain;
  zlink_routing_id_t source_node_rid;
  zlink_routing_id_t source_spot_rid;
  zlink_actor_ref_t source_actor;
  zlink_mesh_operation_id_t operation_id;
  zlink_mesh_operation_kind_t operation_kind;
  zlink_mesh_reply_token_t reply_token;
  const char *channel_name;
  size_t channel_name_size;
  const char *topic;
  size_t topic_size;
  const uint8_t *application_metadata;
  size_t application_metadata_size;
  const void *kind_data;
  size_t kind_data_size;
  size_t part_offset;
  size_t part_count;
  int32_t terminal_result;
  int32_t failure_errno;
} zlink_mesh_receive_record_t;

typedef struct zlink_mesh_send_ready_data_t {
  uint32_t struct_size;
  uint32_t version;
  zlink_mesh_destination_kind_t destination_kind;
  zlink_routing_id_t target_node_rid;
  zlink_routing_id_t target_spot_rid;
  zlink_actor_ref_t target_actor;
  const char *channel_name;
  size_t channel_name_size;
} zlink_mesh_send_ready_data_t;

typedef zlink_mesh_ready_domain_mask_t (*zlink_mesh_ready_handler_fn)(
  void *mesh_node,
  zlink_mesh_ready_domain_mask_t ready_domains,
  void *userdata);

ZLINK_EXPORT zlink_handler_result_t zlink_mesh_node_set_ready_handler(
  void *mesh_node,
  zlink_mesh_ready_handler_fn handler,
  void *userdata);

ZLINK_EXPORT void *zlink_mesh_ready_batch_new(size_t record_capacity);
ZLINK_EXPORT zlink_config_result_t zlink_mesh_ready_batch_reset(void *batch);
ZLINK_EXPORT zlink_close_result_t zlink_mesh_ready_batch_destroy(void **batch_p);

ZLINK_EXPORT zlink_recv_result_t zlink_mesh_node_drain_ready(
  void *mesh_node,
  zlink_mesh_ready_domain_mask_t domains,
  void *batch,
  uint32_t *has_residue_out,
  zlink_recv_flags_t flags);

ZLINK_EXPORT size_t zlink_mesh_ready_batch_count(const void *batch);
ZLINK_EXPORT const zlink_mesh_ready_record_t *zlink_mesh_ready_batch_data(
  const void *batch);
ZLINK_EXPORT zlink_config_result_t zlink_mesh_ready_batch_take_claim(
  void *batch,
  size_t index,
  zlink_mesh_claim_t *claim_out);
ZLINK_EXPORT zlink_close_result_t zlink_mesh_claim_release(
  zlink_mesh_claim_t *claim);

ZLINK_EXPORT void *zlink_mesh_receive_batch_new(
  size_t message_capacity,
  size_t part_capacity,
  size_t byte_capacity);
ZLINK_EXPORT zlink_config_result_t zlink_mesh_receive_batch_reset(void *batch);
ZLINK_EXPORT zlink_close_result_t zlink_mesh_receive_batch_destroy(void **batch_p);

ZLINK_EXPORT zlink_recv_result_t zlink_mesh_claim_recv_batch(
  zlink_mesh_claim_t *claim,
  void *batch,
  zlink_mesh_receive_requirements_t *required_out,
  zlink_recv_flags_t flags);

ZLINK_EXPORT size_t zlink_mesh_receive_batch_count(const void *batch);
ZLINK_EXPORT const zlink_mesh_receive_record_t *zlink_mesh_receive_batch_data(
  const void *batch);
ZLINK_EXPORT size_t zlink_mesh_receive_batch_part_count(const void *batch);
ZLINK_EXPORT const zlink_msg_t *zlink_mesh_receive_batch_parts(const void *batch);
ZLINK_EXPORT zlink_config_result_t zlink_mesh_receive_batch_retain_message(
  const void *batch,
  size_t record_index,
  zlink_msg_t *parts_out,
  size_t *part_count_inout);

ZLINK_EXPORT zlink_submit_result_t zlink_mesh_reply(
  const zlink_mesh_reply_token_t *token,
  const zlink_msg_t *parts,
  size_t part_count,
  zlink_send_flags_t flags);

#ifdef __cplusplus
}
#endif

#endif
