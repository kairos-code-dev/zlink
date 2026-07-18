/* SPDX-License-Identifier: MPL-2.0 */

#ifndef ZLINK_SERVICE_STREAM_SESSION_H_INCLUDED
#define ZLINK_SERVICE_STREAM_SESSION_H_INCLUDED

#include <zlink/common.h>
#include <zlink/message/api.h>
#include <zlink/service/actor.h>

#ifdef __cplusplus
extern "C" {
#endif

/* STREAM session service: session-Actor binding and transfer barrier.
   Contract: core/doc/spec/core/service/05-stream-session.md */

#define ZLINK_STREAM_SESSION_ABI_VERSION 1u

typedef enum zlink_stream_session_state_t {
  ZLINK_STREAM_SESSION_CREATED  = 1,
  ZLINK_STREAM_SESSION_STARTED  = 2,
  ZLINK_STREAM_SESSION_DRAINING = 3,
  ZLINK_STREAM_SESSION_STOPPED  = 4,
  ZLINK_STREAM_SESSION_ERROR    = 5
} zlink_stream_session_state_t;

typedef struct zlink_stream_session_binding_t {
  uint32_t struct_size;
  uint32_t version;
  zlink_routing_id_t session_rid;
  zlink_actor_ref_t actor;
  uint64_t binding_generation;
  uint64_t membership_epoch;
} zlink_stream_session_binding_t;

typedef struct zlink_stream_session_status_t {
  uint32_t struct_size;
  uint32_t version;
  zlink_stream_session_state_t state;
  uint64_t lifecycle_generation;
  uint64_t session_count;
  uint64_t binding_count;
  uint64_t pending_message_count;
  uint64_t pending_byte_count;
  int32_t last_error;
} zlink_stream_session_status_t;

ZLINK_EXPORT void *zlink_stream_session_service_new(
  void *mesh_node,
  void *stream);
ZLINK_EXPORT zlink_config_result_t zlink_stream_session_service_start(
  void *service);
ZLINK_EXPORT zlink_request_result_t zlink_stream_session_service_shutdown(
  void *service,
  uint32_t timeout_ms);
ZLINK_EXPORT zlink_close_result_t zlink_stream_session_service_destroy(
  void **service_p);
ZLINK_EXPORT zlink_config_result_t zlink_stream_session_service_status(
  void *service,
  zlink_stream_session_status_t *status_out);

ZLINK_EXPORT zlink_submit_result_t zlink_stream_session_bind_actor(
  void *service,
  const zlink_routing_id_t *session_rid,
  const zlink_actor_ref_t *actor,
  zlink_mesh_operation_id_t *operation_id_out,
  uint32_t timeout_ms);
ZLINK_EXPORT zlink_submit_result_t zlink_stream_session_unbind_actor(
  void *service,
  const zlink_routing_id_t *session_rid,
  const zlink_actor_ref_t *actor,
  uint64_t expected_binding_generation,
  zlink_mesh_operation_id_t *operation_id_out,
  uint32_t timeout_ms);
ZLINK_EXPORT zlink_config_result_t zlink_stream_session_bindings(
  void *service,
  const zlink_routing_id_t *session_rid,
  zlink_stream_session_binding_t *entries,
  size_t *count_inout);

ZLINK_EXPORT zlink_submit_result_t zlink_stream_session_send_to_actor(
  void *service,
  const zlink_routing_id_t *session_rid,
  const zlink_actor_ref_t *actor,
  const zlink_mesh_metadata_view_t *actor_metadata,
  const zlink_msg_t *parts,
  size_t part_count,
  zlink_send_flags_t flags);
ZLINK_EXPORT zlink_submit_result_t zlink_stream_session_request_to_actor(
  void *service,
  const zlink_routing_id_t *session_rid,
  const zlink_actor_ref_t *actor,
  const zlink_mesh_metadata_view_t *actor_metadata,
  const zlink_msg_t *parts,
  size_t part_count,
  zlink_mesh_operation_id_t *operation_id_out,
  zlink_send_flags_t flags,
  uint32_t timeout_ms);

ZLINK_EXPORT zlink_submit_result_t zlink_mesh_node_actor_send_bound_session(
  void *mesh_node,
  const zlink_actor_ref_t *actor,
  const zlink_msg_t *parts,
  size_t part_count,
  zlink_send_flags_t flags);
ZLINK_EXPORT zlink_submit_result_t zlink_mesh_node_actor_close_bound_session(
  void *mesh_node,
  const zlink_actor_ref_t *actor,
  uint64_t expected_binding_generation,
  zlink_mesh_operation_id_t *operation_id_out,
  uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif
