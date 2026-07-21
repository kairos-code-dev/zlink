/* SPDX-License-Identifier: MPL-2.0 */

#ifndef ZLINK_SERVICE_SPOT_H_INCLUDED
#define ZLINK_SERVICE_SPOT_H_INCLUDED

#include <zlink/common.h>
#include <zlink/message/api.h>
#include <zlink/service/mesh_node.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Spot: logical destination, direct messaging and Logical Multicast.
   Contract: core/doc/spec/core/service/03-spot.md
   zlink_spot_timer_new() is declared in zlink/eventing/api.h. */

#define ZLINK_SPOT_ABI_VERSION 2u

typedef enum zlink_spot_kind_t {
  ZLINK_SPOT_KIND_INVALID = 0,
  ZLINK_SPOT_KIND_ENTRY    = 1,
  ZLINK_SPOT_KIND_USER     = 2,
  ZLINK_SPOT_KIND_INSTANCE = 3
} zlink_spot_kind_t;

typedef enum zlink_spot_activation_state_t {
  ZLINK_SPOT_ACTIVATION_INVALID    = 0,
  ZLINK_SPOT_ACTIVATION_ACTIVATING = 1,
  ZLINK_SPOT_ACTIVATION_READY      = 2,
  ZLINK_SPOT_ACTIVATION_CLOSING    = 3
} zlink_spot_activation_state_t;

typedef enum zlink_spot_subscription_kind_t {
  ZLINK_SPOT_SUBSCRIPTION_EXACT  = 1,
  ZLINK_SPOT_SUBSCRIPTION_PREFIX = 2
} zlink_spot_subscription_kind_t;

typedef struct zlink_spot_status_t {
  uint32_t struct_size;
  uint32_t version;
  zlink_routing_id_t spot_rid;
  zlink_spot_kind_t spot_kind;
  uint64_t lifecycle_generation;
  uint64_t pending_application_messages;
  uint64_t pending_infrastructure_messages;
  uint64_t pending_bytes;
  uint32_t active_actor_count;
  uint32_t draining;
  int32_t last_error;
  uint64_t last_changed_ms;
  zlink_spot_activation_state_t activation_state;
} zlink_spot_status_t;

ZLINK_EXPORT void *zlink_spot_new(void *mesh_node);
ZLINK_EXPORT zlink_config_result_t zlink_mesh_node_entry_spot(
  void *mesh_node,
  void **spot_out);
ZLINK_EXPORT zlink_config_result_t zlink_mesh_node_spot_lookup(
  void *mesh_node,
  const zlink_routing_id_t *spot_rid,
  void **spot_out);
ZLINK_EXPORT zlink_config_result_t zlink_mesh_node_spot_get_or_new(
  void *mesh_node,
  const zlink_routing_id_t *spot_rid,
  void **spot_out,
  uint32_t *created_out);
ZLINK_EXPORT zlink_close_result_t zlink_spot_destroy(void **spot_p);
ZLINK_EXPORT zlink_config_result_t zlink_spot_status(
  void *spot,
  zlink_spot_status_t *status_out);

ZLINK_EXPORT zlink_submit_result_t zlink_spot_send_to_channel(
  void *spot,
  const char *channel_name,
  const zlink_mesh_metadata_view_t *metadata,
  const zlink_msg_t *parts,
  size_t part_count,
  zlink_send_flags_t flags);

ZLINK_EXPORT zlink_submit_result_t zlink_spot_request_to_channel(
  void *spot,
  const char *channel_name,
  const zlink_mesh_metadata_view_t *metadata,
  const zlink_msg_t *parts,
  size_t part_count,
  zlink_mesh_operation_id_t *operation_id_out,
  zlink_send_flags_t flags,
  uint32_t timeout_ms);

ZLINK_EXPORT zlink_submit_result_t zlink_spot_send_to_spot(
  void *spot,
  const zlink_routing_id_t *target_node_rid,
  const zlink_routing_id_t *target_spot_rid,
  uint64_t target_spot_generation,
  const zlink_mesh_metadata_view_t *metadata,
  const zlink_msg_t *parts,
  size_t part_count,
  zlink_send_flags_t flags);

ZLINK_EXPORT zlink_submit_result_t zlink_spot_request_to_spot(
  void *spot,
  const zlink_routing_id_t *target_node_rid,
  const zlink_routing_id_t *target_spot_rid,
  uint64_t target_spot_generation,
  const zlink_mesh_metadata_view_t *metadata,
  const zlink_msg_t *parts,
  size_t part_count,
  zlink_mesh_operation_id_t *operation_id_out,
  zlink_send_flags_t flags,
  uint32_t timeout_ms);

ZLINK_EXPORT zlink_submit_result_t zlink_spot_publish(
  void *spot,
  const char *channel_name,
  const char *topic,
  const zlink_mesh_metadata_view_t *metadata,
  const zlink_msg_t *parts,
  size_t part_count,
  zlink_mesh_publish_detail_t *detail_out,
  zlink_send_flags_t flags);

ZLINK_EXPORT zlink_config_result_t zlink_spot_set_subscription(
  void *spot,
  const char *channel_name,
  const char *topic_filter,
  zlink_spot_subscription_kind_t kind);
ZLINK_EXPORT zlink_config_result_t zlink_spot_unset_subscription(
  void *spot,
  const char *channel_name,
  const char *topic_filter,
  zlink_spot_subscription_kind_t kind);

#ifdef __cplusplus
}
#endif

#endif
