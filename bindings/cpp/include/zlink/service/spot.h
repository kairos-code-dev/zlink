/* SPDX-License-Identifier: MPL-2.0 */

#ifndef ZLINK_SERVICE_SPOT_H_INCLUDED
#define ZLINK_SERVICE_SPOT_H_INCLUDED

#include <zlink/common.h>
#include <zlink/socket/api.h>
#include <zlink/service/actor.h>
#include <zlink/eventing/api.h>

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
ZLINK_EXPORT void *zlink_spot_node_new (void *ctx, const zlink_spot_node_options_t *options);

/**
 * @brief Destroy a SPOT node and release all resources.
 *
 * Attached spot nodes are normally shut down by `zlink_discovery_destroy()`.
 */
ZLINK_EXPORT zlink_close_result_t zlink_spot_node_destroy (void **node_p);

ZLINK_EXPORT zlink_config_result_t zlink_spot_node_entry_spot (void *node_, void **spot_out_);

ZLINK_EXPORT zlink_config_result_t zlink_spot_node_spot_lookup (void *node_,
                                                                const zlink_routing_id_t *spot_rid_,
                                                                void **spot_out_);

ZLINK_EXPORT zlink_config_result_t zlink_spot_node_spot_get_or_new (
  void *node_, const zlink_routing_id_t *spot_rid_, void **spot_out_, uint32_t *created_out_);

ZLINK_EXPORT zlink_config_result_t zlink_spot_node_actor_new (void *node_,
                                                              const char *actor_id_,
                                                              zlink_actor_ref_t *actor_out_);

ZLINK_EXPORT zlink_config_result_t
zlink_spot_node_actor_new_with_request (void *node_,
                                        const char *actor_id_,
                                        zlink_msg_t *parts_,
                                        size_t part_count_,
                                        zlink_actor_ref_t *actor_out_);

ZLINK_EXPORT zlink_config_result_t zlink_spot_node_actor_lookup (void *node_,
                                                                 const char *actor_id_,
                                                                 zlink_actor_ref_t *out_);

ZLINK_EXPORT zlink_submit_result_t
zlink_remote_actor_get_ref (void *node_,
                            const zlink_routing_id_t *target_node_rid_,
                            const char *actor_id_,
                            zlink_actor_lookup_handler_fn handler_,
                            void *userdata_,
                            uint32_t timeout_ms_);

ZLINK_EXPORT zlink_submit_result_t zlink_spot_node_actor_destroy (void *node_,
                                                                  const zlink_actor_ref_t *actor_,
                                                                  zlink_reply_handler_fn handler_,
                                                                  void *userdata_,
                                                                  uint32_t timeout_ms_);

ZLINK_EXPORT zlink_submit_result_t
zlink_spot_node_actor_join_spot (void *node_,
                                 const zlink_actor_ref_t *actor_,
                                 const zlink_routing_id_t *dest_node_rid_,
                                 const zlink_routing_id_t *dest_spot_rid_,
                                 zlink_msg_t *parts_,
                                 size_t part_count_,
                                 zlink_actor_join_spot_handler_fn handler_,
                                 void *userdata_,
                                 zlink_send_flags_t flags_,
                                 uint32_t timeout_ms_);

ZLINK_EXPORT zlink_submit_result_t
zlink_spot_node_actor_join_entry_spot (void *node_,
                                       const zlink_actor_ref_t *actor_,
                                       const zlink_routing_id_t *dest_node_rid_,
                                       zlink_msg_t *parts_,
                                       size_t part_count_,
                                       zlink_actor_join_entry_spot_handler_fn handler_,
                                       void *userdata_,
                                       zlink_send_flags_t flags_,
                                       uint32_t timeout_ms_);

ZLINK_EXPORT zlink_recv_result_t zlink_spot_actor_join_recv (void *spot_,
                                                             zlink_actor_join_info_t *info_out_,
                                                             zlink_msg_t **parts_out_,
                                                             size_t *part_count_out_,
                                                             zlink_recv_flags_t flags_);

ZLINK_EXPORT zlink_submit_result_t
zlink_spot_actor_join_reply (void *spot_,
                             const zlink_actor_join_info_t *info_,
                             int32_t join_result_code_,
                             zlink_msg_t *parts_,
                             size_t part_count_);

ZLINK_EXPORT zlink_submit_result_t
zlink_spot_node_actor_leave_spot (void *node_,
                                  const zlink_actor_ref_t *actor_,
                                  const zlink_routing_id_t *current_spot_rid_,
                                  zlink_reply_handler_fn handler_,
                                  void *userdata_,
                                  uint32_t timeout_ms_);

ZLINK_EXPORT zlink_recv_result_t
zlink_spot_node_actor_recv_part (void *node_,
                                 const zlink_actor_ref_t *actor_,
                                 zlink_actor_recv_info_t *info_out_,
                                 zlink_msg_t *part_out_,
                                 zlink_part_flag_t *has_more_out_,
                                 zlink_recv_flags_t flags_);

ZLINK_EXPORT zlink_submit_result_t zlink_spot_node_actor_send_bound_session_msg (
  void *node_, const zlink_actor_ref_t *actor_, zlink_msg_t *message_, zlink_send_flags_t flags_);

ZLINK_EXPORT zlink_submit_result_t zlink_spot_node_actor_forward_bound_session_part (
  void *node_,
  const zlink_actor_ref_t *actor_,
  const zlink_routing_id_t *source_node_rid_,
  const zlink_routing_id_t *source_session_rid_,
  zlink_msg_t *message_,
  zlink_send_flags_t flags_,
  zlink_part_flag_t part_flag_);

ZLINK_EXPORT zlink_config_result_t zlink_spot_node_actor_bind_remote_session (
  void *node_,
  const zlink_actor_ref_t *actor_,
  const zlink_routing_id_t *source_node_rid_,
  const zlink_routing_id_t *source_session_rid_);

ZLINK_EXPORT zlink_recv_result_t zlink_spot_recv_actor_lifecycle (
  void *spot_, zlink_spot_actor_lifecycle_event_t *event_out_, zlink_recv_flags_t flags_);

ZLINK_EXPORT zlink_recv_result_t
zlink_spot_recv_actor_lifecycle_with_request (void *spot_,
                                              zlink_spot_actor_lifecycle_event_t *event_out_,
                                              zlink_msg_t **parts_out_,
                                              size_t *part_count_out_,
                                              zlink_recv_flags_t flags_);

ZLINK_EXPORT zlink_config_result_t
zlink_stream_bound_actors (void *stream_,
                           const zlink_routing_id_t *session_rid_,
                           zlink_actor_ref_t *entries_,
                           size_t *count_);

ZLINK_EXPORT zlink_request_result_t zlink_spot_node_actor_close_bound_session (
  void *node_, const zlink_actor_ref_t *actor_, uint32_t timeout_ms_);

/** @brief Bind the routed ingress endpoint for this SPOT node.
 *
 * This endpoint is used by the node's router socket. A ROUTED-only node starts
 * from this call. A node that also enables PUB/SUB stores the router endpoint
 * and starts when zlink_spot_node_set_pub_bind() is called.
 */
ZLINK_EXPORT zlink_config_result_t zlink_spot_node_set_router_bind (void *node,
                                                                    const char *endpoint);

/** @brief Bind the PUB/SUB mesh endpoint for this SPOT node.
 *
 * Supports port 0 for ephemeral port allocation (e.g. "tcp://127.0.0.1:0").
 * After a successful bind, use zlink_spot_node_status() to retrieve
 * the resolved endpoint (local_endpoint field) with the actual assigned port.
 */
ZLINK_EXPORT zlink_config_result_t zlink_spot_node_set_pub_bind (void *node, const char *endpoint);

ZLINK_EXPORT zlink_config_result_t zlink_spot_node_set_pub_routing_id (
  void *node, const void *data, size_t size);

ZLINK_EXPORT zlink_config_result_t zlink_spot_node_set_sub_routing_id (
  void *node, const void *data, size_t size);

/**
 * @brief Connect to a peer SPOT node endpoint (mesh topology).
 */
ZLINK_EXPORT zlink_connect_result_t zlink_spot_node_connect_peer (void *node,
                                                                  const char *peer_endpoint);
/**
 * @brief Connect to a peer SPOT node endpoint and associate it with a target node RID.
 */
ZLINK_EXPORT zlink_connect_result_t
zlink_spot_node_connect_peer_rid (void *node,
                                  const zlink_routing_id_t *target_node_rid,
                                  const char *peer_endpoint);

/**
 * @brief Disconnect from a peer SPOT node endpoint.
 *
 * Returns EBUSY if discovery is already attached.
 */
ZLINK_EXPORT zlink_connect_result_t zlink_spot_node_disconnect_peer (void *node,
                                                                     const char *peer_endpoint);
ZLINK_EXPORT zlink_connect_result_t
zlink_spot_node_disconnect_peer_rid (void *node, const zlink_routing_id_t *target_node_rid);

/**
 * @brief Attach a Discovery instance for discovery-owned SPOT peer connection.
 *
 * The discovery must provide a SPOT channel view. After attach, the node takes
 * its mesh identity from that channel view and discovery destroy owns
 * participant shutdown.
 */
ZLINK_EXPORT zlink_config_result_t zlink_spot_node_attach_discovery (void *node, void *discovery);

#define ZLINK_SPOT_ROUTE_BRIDGE_CAP_SPOT_ROUTE 0x00000001u
#define ZLINK_SPOT_ROUTE_BRIDGE_ROUTE_ONLY ZLINK_SPOT_ROUTE_BRIDGE_CAP_SPOT_ROUTE

typedef struct zlink_spot_route_bridge_options_t
{
    uint32_t struct_size;
    int default_request_timeout_ms;
    int error_reply_policy;
    int receive_mode;
} zlink_spot_route_bridge_options_t;

typedef struct zlink_spot_route_bridge_endpoint_options_t
{
    uint32_t struct_size;
    uint32_t capabilities;
    int inbound_relay_policy;
} zlink_spot_route_bridge_endpoint_options_t;

ZLINK_EXPORT void *zlink_spot_route_bridge_new (
  void *ctx_, void *spot_node_, const zlink_spot_route_bridge_options_t *options_);

ZLINK_EXPORT int zlink_spot_route_bridge_attach_router_channel (
  void *bridge_,
  const char *channel_name_,
  void *router_socket_,
  const zlink_spot_route_bridge_endpoint_options_t *options_);

ZLINK_EXPORT int zlink_spot_route_bridge_send (void *bridge_,
                                               const char *channel_name_,
                                               const zlink_routing_id_t *target_node_rid_,
                                               const zlink_routing_id_t *target_spot_rid_,
                                               zlink_msg_t *parts_,
                                               size_t part_count_,
                                               zlink_send_flags_t flags_);

ZLINK_EXPORT int zlink_spot_route_bridge_request (void *bridge_,
                                                  const char *channel_name_,
                                                  const zlink_routing_id_t *target_node_rid_,
                                                  const zlink_routing_id_t *target_spot_rid_,
                                                  zlink_msg_t *parts_,
                                                  size_t part_count_,
                                                  zlink_reply_handler_fn callback_,
                                                  void *user_data_,
                                                  zlink_send_flags_t flags_,
                                                  uint32_t timeout_ms_);

ZLINK_EXPORT int zlink_spot_route_bridge_handle_router_received (
  void *bridge_,
  const char *channel_name_,
  const zlink_routing_id_t *source_node_rid_,
  uint64_t request_seq_,
  zlink_msg_t *parts_,
  size_t part_count_,
  bool *handled_);

ZLINK_EXPORT int zlink_spot_route_bridge_drain (void *bridge_);

ZLINK_EXPORT int zlink_spot_route_bridge_close (void *bridge_);

ZLINK_EXPORT void *zlink_spot_node_publisher_new (void *spot_node_);

ZLINK_EXPORT int zlink_spot_node_publisher_publish (void *publisher_,
                                                    const char *topic_,
                                                    zlink_msg_t *parts_,
                                                    size_t part_count_,
                                                    zlink_send_flags_t flags_);

ZLINK_EXPORT int zlink_spot_node_publisher_close (void *publisher_);

ZLINK_EXPORT int zlink_spot_drain_reply (void *spot_);

ZLINK_EXPORT int zlink_spot_drain_channel_reply (void *spot_, void *dealer_subject_);

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

typedef struct zlink_spot_node_socket_filter_t
{
    zlink_spot_node_socket_owner_t owner;
    zlink_socket_type_t socket_type;
    char socket_name[64];
} zlink_spot_node_socket_filter_t;

typedef struct zlink_spot_node_socket_entry_t
{
    zlink_spot_node_socket_owner_t owner;
    uint64_t owner_id;
    char owner_name[64];
    char socket_name[64];
    zlink_socket_type_t socket_type;
    uint32_t auto_hwm_visible;
    zlink_monitor_status_t monitor_status;
} zlink_spot_node_socket_entry_t;

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

ZLINK_EXPORT zlink_config_result_t zlink_spot_node_status (void *node_,
                                                           zlink_spot_node_status_t *out_);
ZLINK_EXPORT zlink_config_result_t
zlink_spot_node_peers (void *node_,
                       const zlink_spot_node_peer_filter_t *filter_,
                       zlink_spot_node_peer_entry_t *entries_,
                       size_t *count_);
ZLINK_EXPORT zlink_config_result_t
zlink_spot_node_subjects (void *node_,
                          const zlink_spot_node_subject_filter_t *filter_,
                          zlink_spot_node_subject_entry_t *entries_,
                          size_t *count_);
ZLINK_EXPORT zlink_config_result_t
zlink_spot_node_internal_sockets (void *node_,
                                  const zlink_spot_node_socket_filter_t *filter_,
                                  zlink_spot_node_socket_entry_t *entries_,
                                  size_t *count_);
ZLINK_EXPORT zlink_config_result_t zlink_spot_node_spots (void *node_,
                                                          zlink_spot_node_spot_entry_t *entries_,
                                                          size_t *count_);
ZLINK_EXPORT zlink_config_result_t zlink_spot_node_actors (void *node_,
                                                           zlink_spot_node_actor_entry_t *entries_,
                                                           size_t *count_);
ZLINK_EXPORT zlink_config_result_t zlink_spot_actors (void *spot_,
                                                      zlink_actor_ref_t *entries_,
                                                      size_t *count_);


#ifdef __cplusplus
}
#endif

#endif
