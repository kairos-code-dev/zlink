/* SPDX-License-Identifier: MPL-2.0 */

#ifndef ZLINK_SPOT_H_INCLUDED
#define ZLINK_SPOT_H_INCLUDED

#include <zlink/common.h>
#include <zlink/socket.h>
#include <zlink/actor.h>
#include <zlink/monitoring.h>

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
/*  SPOT PUB/SUB API                                                          */
/******************************************************************************/

/* SPOT -------------------------------------------------------------------- */

/**
 * @brief Create a unified SPOT facade over an existing SPOT node.
 *
 * The returned handle borrows `node`, lazily creates side sockets as needed,
 * and must be released with `zlink_spot_destroy()`. Destroying the facade does
 * not destroy the underlying node.
 */
ZLINK_EXPORT void *zlink_spot_new (void *node);

/** @brief Destroy a unified SPOT handle. */
ZLINK_EXPORT zlink_close_result_t zlink_spot_destroy (void **spot_p);

/* SPOT Node --------------------------------------------------------------- */

typedef struct zlink_spot_node_options_t
{
    zlink_spot_node_mode_t mode;
} zlink_spot_node_options_t;

/**
 * @brief Create a SPOT node runtime for topology, discovery, and lifecycle.
 *
 * If options is NULL or options->mode is 0, the node enables all SPOT
 * features. A node created with PUBSUB mode rejects routed request/reply APIs
 * with ENOTSUP. A node created with ROUTED mode rejects topic pub/sub APIs with
 * ENOTSUP.
 */
ZLINK_EXPORT void *zlink_spot_node_new (
  void *ctx,
  const zlink_spot_node_options_t *options);

/**
 * @brief Destroy a SPOT node and release all resources.
 *
 * Attached spot nodes are normally shut down by `zlink_discovery_destroy()`.
 */
ZLINK_EXPORT zlink_close_result_t zlink_spot_node_destroy (void **node_p);

ZLINK_EXPORT zlink_config_result_t zlink_spot_node_entry_spot (
  void *node_,
  void **spot_out_);

ZLINK_EXPORT zlink_config_result_t zlink_spot_node_spot_lookup (
  void *node_,
  const zlink_routing_id_t *spot_rid_,
  void **spot_out_);

ZLINK_EXPORT zlink_config_result_t zlink_spot_node_spot_get_or_new (
  void *node_,
  const zlink_routing_id_t *spot_rid_,
  void **spot_out_,
  uint32_t *created_out_);

ZLINK_EXPORT zlink_config_result_t zlink_spot_node_actor_new (
  void *node_,
  const char *actor_id_,
  zlink_actor_ref_t *actor_out_);

ZLINK_EXPORT zlink_config_result_t zlink_spot_node_actor_lookup (
  void *node_,
  const char *actor_id_,
  zlink_actor_ref_t *out_);

ZLINK_EXPORT zlink_submit_result_t zlink_remote_actor_get_ref (
  void *node_,
  const zlink_routing_id_t *target_node_rid_,
  const char *actor_id_,
  zlink_actor_lookup_handler_fn handler_,
  void *userdata_,
  uint32_t timeout_ms_);

ZLINK_EXPORT zlink_submit_result_t zlink_spot_node_actor_destroy (
  void *node_,
  const zlink_actor_ref_t *actor_,
  zlink_reply_handler_fn handler_,
  void *userdata_,
  uint32_t timeout_ms_);

ZLINK_EXPORT zlink_submit_result_t zlink_spot_node_actor_join_spot (
  void *node_,
  const zlink_actor_ref_t *actor_,
  const zlink_routing_id_t *dest_node_rid_,
  const zlink_routing_id_t *dest_spot_rid_,
  zlink_msg_t *parts_,
  size_t part_count_,
  zlink_actor_join_spot_handler_fn handler_,
  void *userdata_,
  zlink_send_flags_t flags_,
  uint32_t timeout_ms_);

ZLINK_EXPORT zlink_submit_result_t zlink_spot_node_actor_join_entry_spot (
  void *node_,
  const zlink_actor_ref_t *actor_,
  const zlink_routing_id_t *dest_node_rid_,
  zlink_actor_join_entry_spot_handler_fn handler_,
  void *userdata_,
  uint32_t timeout_ms_);

ZLINK_EXPORT zlink_recv_result_t zlink_spot_actor_join_recv (
  void *spot_,
  zlink_actor_join_info_t *info_out_,
  zlink_msg_t **parts_out_,
  size_t *part_count_out_,
  zlink_recv_flags_t flags_);

ZLINK_EXPORT zlink_submit_result_t zlink_spot_actor_join_reply (
  void *spot_,
  const zlink_actor_join_info_t *info_,
  uint32_t accepted_,
  zlink_msg_t *parts_,
  size_t part_count_);

ZLINK_EXPORT zlink_submit_result_t zlink_spot_node_actor_leave_spot (
  void *node_,
  const zlink_actor_ref_t *actor_,
  const zlink_routing_id_t *current_spot_rid_,
  zlink_reply_handler_fn handler_,
  void *userdata_,
  uint32_t timeout_ms_);

ZLINK_EXPORT zlink_recv_result_t zlink_spot_node_actor_recv_part (
  void *node_,
  const zlink_actor_ref_t *actor_,
  zlink_actor_recv_info_t *info_out_,
  zlink_msg_t *part_out_,
  zlink_part_flag_t *has_more_out_,
  zlink_recv_flags_t flags_);

ZLINK_EXPORT zlink_submit_result_t zlink_spot_node_actor_send_bound_session_msg (
  void *node_,
  const zlink_actor_ref_t *actor_,
  zlink_msg_t *message_,
  zlink_send_flags_t flags_);

ZLINK_EXPORT zlink_handler_result_t zlink_spot_actor_lifecycle_handler (
  void *spot_,
  zlink_spot_actor_lifecycle_handler_fn on_join_,
  zlink_spot_actor_lifecycle_handler_fn on_leave_,
  void *userdata_);

ZLINK_EXPORT zlink_config_result_t zlink_stream_bound_actors (
  void *stream_,
  const zlink_routing_id_t *session_rid_,
  zlink_actor_ref_t *entries_,
  size_t *count_);

ZLINK_EXPORT zlink_request_result_t zlink_spot_node_actor_close_bound_session (
  void *node_,
  const zlink_actor_ref_t *actor_,
  uint32_t timeout_ms_);

/** @brief Bind the routed ingress endpoint for this SPOT node.
 *
 * This endpoint is used by the node's router socket. A ROUTED-only node starts
 * from this call. A node that also enables PUB/SUB stores the router endpoint
 * and starts when zlink_spot_node_set_pub_bind() is called.
 */
ZLINK_EXPORT zlink_config_result_t zlink_spot_node_set_router_bind (
  void *node, const char *endpoint);

/** @brief Bind the PUB/SUB mesh endpoint for this SPOT node.
 *
 * Supports port 0 for ephemeral port allocation (e.g. "tcp://127.0.0.1:0").
 * After a successful bind, use zlink_spot_node_status_snapshot() to retrieve
 * the resolved endpoint (local_endpoint field) with the actual assigned port.
 */
ZLINK_EXPORT zlink_config_result_t zlink_spot_node_set_pub_bind (
  void *node, const char *endpoint);

/**
 * @brief Connect to a peer SPOT node endpoint (mesh topology).
 *
 * Returns EBUSY if discovery is already attached.
 */
ZLINK_EXPORT zlink_connect_result_t zlink_spot_node_connect_peer (void *node,
                                               const char *peer_endpoint);

/**
 * @brief Disconnect from a peer SPOT node endpoint.
 *
 * Returns EBUSY if discovery is already attached.
 */
ZLINK_EXPORT zlink_connect_result_t zlink_spot_node_disconnect_peer (
  void *node, const char *peer_endpoint);
ZLINK_EXPORT zlink_connect_result_t zlink_spot_node_disconnect_peer_rid (
  void *node, const zlink_routing_id_t *target_node_rid);

ZLINK_EXPORT zlink_connect_result_t zlink_spot_node_connect_router_channel_peer (
  void *node,
  const char *channel_name,
  const char *endpoint);
ZLINK_EXPORT zlink_connect_result_t
zlink_spot_node_connect_router_channel_peer_rid (
  void *node,
  const char *channel_name,
  const zlink_routing_id_t *peer_rid,
  const char *endpoint);
ZLINK_EXPORT zlink_connect_result_t
zlink_spot_node_disconnect_router_channel_peer (
  void *node,
  const char *channel_name,
  const char *endpoint);
ZLINK_EXPORT zlink_connect_result_t
zlink_spot_node_disconnect_router_channel_peer_rid (
  void *node,
  const char *channel_name,
  const zlink_routing_id_t *peer_rid);

/**
 * @brief Attach a Discovery instance for discovery-owned SPOT peer connection.
 *
 * The discovery must provide a SPOT channel view. After attach, the node takes
 * its mesh identity from that channel view and discovery destroy owns
 * participant shutdown.
 */
ZLINK_EXPORT zlink_config_result_t zlink_spot_node_attach_discovery (void *node,
                                                   void *discovery);
ZLINK_EXPORT zlink_config_result_t
zlink_spot_node_attach_router_channel_discovery (
  void *node,
  const char *channel_name,
  void *discovery);

ZLINK_EXPORT zlink_config_result_t zlink_spot_node_attach_channel_dealer (
  void *node_,
  void *discovery_,
  void *dealer_);

ZLINK_EXPORT zlink_config_result_t zlink_spot_node_attach_channel_dealer_manual (
  void *node_,
  const char *channel_name_,
  void *dealer_);

ZLINK_EXPORT zlink_config_result_t zlink_spot_node_attach_pub_ingress (
  void *node_,
  void *pub_);

typedef struct zlink_spot_node_status_t
{
    char channel_name[256];
    char local_endpoint[256];
    zlink_routing_id_t node_routing_id;
    zlink_spot_node_state_t state;
    uint32_t configured_peer_count;
    uint32_t active_peer_count;
    uint32_t connected_peer_count;
    uint32_t subject_count;
    uint32_t ready_subject_count;
    uint32_t disconnected_sub_target_count;
    uint32_t disconnected_routed_target_count;
    int32_t last_error;
    uint64_t last_changed_ms;
} zlink_spot_node_status_t;

typedef struct zlink_spot_node_peer_entry_t
{
    char channel_name[256];
    char local_endpoint[256];
    char peer_endpoint[256];
    zlink_spot_peer_source_t source;
    zlink_spot_peer_kind_t kind;
    zlink_spot_peer_state_t state;
    uint32_t weight;
    uint64_t connected_since_ms;
    uint64_t last_changed_ms;
} zlink_spot_node_peer_entry_t;

typedef struct zlink_spot_node_peer_filter_t
{
    char peer_endpoint[256];
    zlink_spot_peer_source_t source;
    zlink_spot_peer_state_t state;
} zlink_spot_node_peer_filter_t;

typedef struct zlink_spot_node_subject_entry_t
{
    zlink_spot_role_t role;
    char subject[256];
    uint32_t subject_kind;
    uint32_t ready_peer_count;
    uint32_t active_peer_count;
    uint64_t last_changed_ms;
} zlink_spot_node_subject_entry_t;

typedef struct zlink_spot_node_subject_filter_t
{
    zlink_spot_role_t role;
    char subject[256];
    uint32_t subject_kind;
} zlink_spot_node_subject_filter_t;

typedef struct zlink_spot_node_socket_snapshot_filter_t
{
    zlink_spot_node_socket_owner_t owner;
    zlink_socket_type_t socket_type;
    char socket_name[64];
} zlink_spot_node_socket_snapshot_filter_t;

typedef struct zlink_spot_node_socket_snapshot_entry_t
{
    zlink_spot_node_socket_owner_t owner;
    uint64_t owner_id;
    char owner_name[64];
    char socket_name[64];
    zlink_socket_type_t socket_type;
    uint32_t auto_hwm_visible;
    zlink_monitor_snapshot_t snapshot;
} zlink_spot_node_socket_snapshot_entry_t;

typedef struct zlink_spot_node_spot_entry_t
{
    zlink_routing_id_t spot_rid;
    zlink_spot_kind_t spot_kind;
    uint32_t dispatch_handler_attached;
    uint32_t joined_actor_count;
    uint32_t pending_actor_join_count;
    uint32_t route_synced;
    uint64_t last_changed_ms;
} zlink_spot_node_spot_entry_t;

typedef struct zlink_spot_node_actor_entry_t
{
    zlink_actor_ref_t actor;
    zlink_routing_id_t current_spot_rid;
    zlink_spot_kind_t current_spot_kind;
    uint32_t route_synced;
    uint32_t pending_message_count;
    uint64_t last_changed_ms;
} zlink_spot_node_actor_entry_t;

ZLINK_EXPORT zlink_config_result_t zlink_spot_node_status_snapshot (
  void *node_,
  zlink_spot_node_status_t *out_);
ZLINK_EXPORT zlink_config_result_t zlink_spot_node_peers_snapshot (
  void *node_,
  zlink_spot_node_peer_entry_t *entries_,
  size_t *count_);
ZLINK_EXPORT zlink_config_result_t zlink_spot_node_peers_query (
  void *node_,
  const zlink_spot_node_peer_filter_t *filter_,
  zlink_spot_node_peer_entry_t *entries_,
  size_t *count_);
ZLINK_EXPORT zlink_config_result_t zlink_spot_node_subjects_snapshot (
  void *node_,
  const zlink_spot_node_subject_filter_t *filter_,
  zlink_spot_node_subject_entry_t *entries_,
  size_t *count_);
ZLINK_EXPORT zlink_config_result_t zlink_spot_node_internal_sockets_snapshot (
  void *node_,
  const zlink_spot_node_socket_snapshot_filter_t *filter_,
  zlink_spot_node_socket_snapshot_entry_t *entries_,
  size_t *count_);
ZLINK_EXPORT zlink_config_result_t zlink_spot_node_spots_snapshot (
  void *node_,
  zlink_spot_node_spot_entry_t *entries_,
  size_t *count_);
ZLINK_EXPORT zlink_config_result_t zlink_spot_node_actors_snapshot (
  void *node_,
  zlink_spot_node_actor_entry_t *entries_,
  size_t *count_);
ZLINK_EXPORT zlink_config_result_t zlink_spot_actors_snapshot (
  void *spot_,
  zlink_actor_ref_t *entries_,
  size_t *count_);


#ifdef __cplusplus
}
#endif

#endif
