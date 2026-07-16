/* SPDX-License-Identifier: MPL-2.0 */

#ifndef ZLINK_SERVICE_MESH_NODE_H_INCLUDED
#define ZLINK_SERVICE_MESH_NODE_H_INCLUDED

#include <zlink/common.h>
#include <zlink/message/api.h>
#include <zlink/service/dispatch.h>

#ifdef __cplusplus
extern "C" {
#endif

/* MeshNode: RouteMesh membership, peers and node/channel messaging.
   Contract: core/doc/spec/core/service/01-mesh-node.md */

#define ZLINK_MESH_NODE_ABI_VERSION 1u
#define ZLINK_MESH_NAME_MAX 255u
#define ZLINK_CHANNEL_NAME_MAX 255u
#define ZLINK_MESH_ENDPOINT_MAX 511u
#define ZLINK_MESH_APPLICATION_METADATA_MAX 1024u
#define ZLINK_MESH_TOPIC_MAX 255u

typedef enum zlink_mesh_node_state_t {
  ZLINK_MESH_NODE_CREATED       = 1,
  ZLINK_MESH_NODE_STARTED       = 2,
  ZLINK_MESH_NODE_PARTIAL_READY = 3,
  ZLINK_MESH_NODE_READY         = 4,
  ZLINK_MESH_NODE_DRAINING      = 5,
  ZLINK_MESH_NODE_STOPPED       = 6,
  ZLINK_MESH_NODE_ERROR         = 7
} zlink_mesh_node_state_t;

typedef enum zlink_mesh_peer_source_t {
  ZLINK_MESH_PEER_MANUAL    = 1,
  ZLINK_MESH_PEER_DISCOVERY = 2,
  ZLINK_MESH_PEER_MIXED     = 3
} zlink_mesh_peer_source_t;

typedef enum zlink_mesh_peer_state_t {
  ZLINK_MESH_PEER_CONFIGURED = 1,
  ZLINK_MESH_PEER_CONNECTING = 2,
  ZLINK_MESH_PEER_ADMITTED   = 3,
  ZLINK_MESH_PEER_DRAINING   = 4,
  ZLINK_MESH_PEER_CLOSED     = 5,
  ZLINK_MESH_PEER_ERROR      = 6
} zlink_mesh_peer_state_t;

typedef enum zlink_mesh_node_option_t {
  ZLINK_MESH_NODE_OPT_ROUTER_HWM_PROFILE       = 0x3620,
  ZLINK_MESH_NODE_OPT_ROUTER_HWM               = 0x3621,
  ZLINK_MESH_NODE_OPT_MAILBOX_MESSAGE_BUDGET   = 0x3622,
  ZLINK_MESH_NODE_OPT_MAILBOX_BYTE_BUDGET      = 0x3623
} zlink_mesh_node_option_t;

typedef enum zlink_mesh_publish_option_t {
  ZLINK_MESH_PUBLISH_OPT_NODROP = 0x3630
} zlink_mesh_publish_option_t;

typedef struct zlink_mesh_node_options_t {
  uint32_t struct_size;
  uint32_t version;
  const char *mesh_name;
  size_t mesh_name_size;
  const char *trust_profile;
  size_t trust_profile_size;
} zlink_mesh_node_options_t;

typedef struct zlink_mesh_peer_connection_options_t {
  uint32_t struct_size;
  uint32_t version;
  const char *endpoint;
  size_t endpoint_size;
  uint32_t has_expected_rid;
  zlink_routing_id_t expected_rid;
} zlink_mesh_peer_connection_options_t;

typedef struct zlink_mesh_metadata_view_t {
  const uint8_t *data;
  size_t size;
} zlink_mesh_metadata_view_t;

typedef struct zlink_mesh_publish_detail_t {
  uint32_t struct_size;
  uint32_t version;
  uint32_t snapshot_remote_target_count;
  uint32_t admitted_remote_target_count;
  uint32_t dropped_remote_target_count;
  uint32_t snapshot_local_spot_count;
  uint32_t admitted_local_spot_count;
  uint32_t dropped_local_spot_count;
} zlink_mesh_publish_detail_t;

typedef struct zlink_mesh_node_status_t {
  uint32_t struct_size;
  uint32_t version;
  zlink_mesh_node_state_t state;
  zlink_routing_id_t routing_id;
  char mesh_name[ZLINK_MESH_NAME_MAX + 1];
  char local_endpoint[ZLINK_MESH_ENDPOINT_MAX + 1];
  uint64_t lifecycle_generation;
  uint64_t descriptor_revision;
  uint32_t channel_count;
  uint32_t configured_peer_count;
  uint32_t admitted_peer_count;
  uint32_t draining_peer_count;
  uint64_t pending_application_messages;
  uint64_t pending_infrastructure_messages;
  uint64_t pending_bytes;
  uint64_t multicast_submitted;
  uint64_t multicast_dropped_targets;
  int32_t last_error;
  uint64_t last_changed_ms;
} zlink_mesh_node_status_t;

typedef struct zlink_mesh_peer_entry_t {
  uint32_t struct_size;
  uint32_t version;
  uint64_t connection_intent_id;
  zlink_mesh_peer_source_t source;
  zlink_mesh_peer_state_t state;
  zlink_routing_id_t routing_id;
  uint64_t lifecycle_generation;
  uint64_t descriptor_revision;
  char endpoint[ZLINK_MESH_ENDPOINT_MAX + 1];
  uint32_t channel_count;
  int32_t last_error;
  uint64_t last_changed_ms;
} zlink_mesh_peer_entry_t;

ZLINK_EXPORT void *zlink_mesh_node_new(
  void *ctx,
  const zlink_mesh_node_options_t *options);
ZLINK_EXPORT zlink_config_result_t zlink_mesh_node_set_bind(
  void *mesh_node,
  const char *endpoint);
ZLINK_EXPORT zlink_config_result_t zlink_mesh_node_start(void *mesh_node);
ZLINK_EXPORT zlink_request_result_t zlink_mesh_node_shutdown(
  void *mesh_node,
  uint32_t timeout_ms);
ZLINK_EXPORT zlink_close_result_t zlink_mesh_node_destroy(void **mesh_node_p);

ZLINK_EXPORT zlink_config_result_t zlink_mesh_node_add_channel_name(
  void *mesh_node,
  const char *channel_name);
ZLINK_EXPORT zlink_config_result_t zlink_mesh_node_set_channel_weight(
  void *mesh_node,
  const char *channel_name,
  uint32_t weight);

ZLINK_EXPORT zlink_connect_result_t zlink_mesh_node_connect_peer(
  void *mesh_node,
  const zlink_mesh_peer_connection_options_t *options,
  uint64_t *connection_intent_id_out);
ZLINK_EXPORT zlink_connect_result_t zlink_mesh_node_remove_peer_connection(
  void *mesh_node,
  uint64_t connection_intent_id);
ZLINK_EXPORT zlink_connect_result_t zlink_mesh_node_disconnect_peer(
  void *mesh_node,
  const zlink_routing_id_t *peer_rid,
  uint64_t lifecycle_generation);

ZLINK_EXPORT zlink_submit_result_t zlink_mesh_node_send_to_node(
  void *mesh_node,
  const zlink_routing_id_t *target_rid,
  const zlink_mesh_metadata_view_t *metadata,
  const zlink_msg_t *parts,
  size_t part_count,
  zlink_send_flags_t flags);

ZLINK_EXPORT zlink_submit_result_t zlink_mesh_node_request_to_node(
  void *mesh_node,
  const zlink_routing_id_t *target_rid,
  const zlink_mesh_metadata_view_t *metadata,
  const zlink_msg_t *parts,
  size_t part_count,
  zlink_mesh_operation_id_t *operation_id_out,
  zlink_send_flags_t flags,
  uint32_t timeout_ms);

ZLINK_EXPORT zlink_submit_result_t zlink_mesh_node_send_to_channel(
  void *mesh_node,
  const char *channel_name,
  const zlink_mesh_metadata_view_t *metadata,
  const zlink_msg_t *parts,
  size_t part_count,
  zlink_send_flags_t flags);

ZLINK_EXPORT zlink_submit_result_t zlink_mesh_node_request_to_channel(
  void *mesh_node,
  const char *channel_name,
  const zlink_mesh_metadata_view_t *metadata,
  const zlink_msg_t *parts,
  size_t part_count,
  zlink_mesh_operation_id_t *operation_id_out,
  zlink_send_flags_t flags,
  uint32_t timeout_ms);

ZLINK_EXPORT void *zlink_mesh_node_publisher_new(void *mesh_node);
ZLINK_EXPORT zlink_submit_result_t zlink_mesh_node_publisher_publish(
  void *publisher,
  const char *channel_name,
  const char *topic,
  const zlink_mesh_metadata_view_t *metadata,
  const zlink_msg_t *parts,
  size_t part_count,
  zlink_mesh_publish_detail_t *detail_out,
  zlink_send_flags_t flags);
ZLINK_EXPORT zlink_config_result_t zlink_mesh_node_publisher_set_option(
  void *publisher,
  zlink_mesh_publish_option_t option,
  const void *optval,
  size_t optvallen);
ZLINK_EXPORT zlink_config_result_t zlink_mesh_node_publisher_get_option(
  void *publisher,
  zlink_mesh_publish_option_t option,
  void *optval,
  size_t *optvallen);
ZLINK_EXPORT zlink_close_result_t zlink_mesh_node_publisher_destroy(
  void **publisher_p);

ZLINK_EXPORT zlink_config_result_t zlink_set_mesh_node_option(
  void *mesh_node,
  zlink_mesh_node_option_t option,
  const void *optval,
  size_t optvallen);
ZLINK_EXPORT zlink_config_result_t zlink_get_mesh_node_option(
  void *mesh_node,
  zlink_mesh_node_option_t option,
  void *optval,
  size_t *optvallen);

ZLINK_EXPORT zlink_config_result_t zlink_mesh_node_status(
  void *mesh_node,
  zlink_mesh_node_status_t *status_out);
ZLINK_EXPORT zlink_config_result_t zlink_mesh_node_peers(
  void *mesh_node,
  zlink_mesh_peer_entry_t *entries,
  size_t *count_inout);
ZLINK_EXPORT zlink_config_result_t zlink_mesh_node_peer_channels(
  void *mesh_node,
  const zlink_routing_id_t *peer_rid,
  uint64_t lifecycle_generation,
  char (*channel_names_out)[ZLINK_CHANNEL_NAME_MAX + 1],
  uint32_t *weights_out,
  size_t *count_inout);

#ifdef __cplusplus
}
#endif

#endif
