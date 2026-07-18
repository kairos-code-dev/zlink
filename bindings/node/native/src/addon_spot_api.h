/* SPDX-License-Identifier: MPL-2.0 */

#pragma once

#include "addon_common_api.h"

// RouteMesh 10.0.0 service layer entry points (implemented in
// addon_mesh_service.cc). Registered by define_spot_exports().

// --- MeshNode lifecycle ---
napi_value mesh_node_new (napi_env env, napi_callback_info info);
napi_value mesh_node_set_bind (napi_env env, napi_callback_info info);
napi_value mesh_node_start (napi_env env, napi_callback_info info);
napi_value mesh_node_shutdown (napi_env env, napi_callback_info info);
napi_value mesh_node_destroy (napi_env env, napi_callback_info info);
napi_value mesh_node_add_channel_name (napi_env env, napi_callback_info info);
napi_value mesh_node_set_channel_weight (napi_env env, napi_callback_info info);

// --- Peers ---
napi_value mesh_node_connect_peer (napi_env env, napi_callback_info info);
napi_value mesh_node_remove_peer_connection (napi_env env, napi_callback_info info);
napi_value mesh_node_disconnect_peer (napi_env env, napi_callback_info info);

// --- Node / channel messaging ---
napi_value mesh_node_send_to_node (napi_env env, napi_callback_info info);
napi_value mesh_node_request_to_node (napi_env env, napi_callback_info info);
napi_value mesh_node_send_to_channel (napi_env env, napi_callback_info info);
napi_value mesh_node_request_to_channel (napi_env env, napi_callback_info info);

// --- Options / introspection ---
napi_value mesh_node_set_option (napi_env env, napi_callback_info info);
napi_value mesh_node_get_option (napi_env env, napi_callback_info info);
napi_value mesh_node_status (napi_env env, napi_callback_info info);
napi_value mesh_node_peers (napi_env env, napi_callback_info info);
napi_value mesh_node_peer_channels (napi_env env, napi_callback_info info);

// --- Publisher ---
napi_value mesh_node_publisher_new (napi_env env, napi_callback_info info);
napi_value mesh_node_publisher_publish (napi_env env, napi_callback_info info);
napi_value mesh_node_publisher_set_option (napi_env env, napi_callback_info info);
napi_value mesh_node_publisher_get_option (napi_env env, napi_callback_info info);
napi_value mesh_node_publisher_destroy (napi_env env, napi_callback_info info);

// --- Pull dispatch ---
napi_value mesh_node_set_ready_handler (napi_env env, napi_callback_info info);
napi_value mesh_ready_batch_new (napi_env env, napi_callback_info info);
napi_value mesh_ready_batch_reset (napi_env env, napi_callback_info info);
napi_value mesh_ready_batch_destroy (napi_env env, napi_callback_info info);
napi_value mesh_node_drain_ready (napi_env env, napi_callback_info info);
napi_value mesh_ready_batch_take_claim (napi_env env, napi_callback_info info);
napi_value mesh_claim_release (napi_env env, napi_callback_info info);
napi_value mesh_receive_batch_new (napi_env env, napi_callback_info info);
napi_value mesh_receive_batch_reset (napi_env env, napi_callback_info info);
napi_value mesh_receive_batch_destroy (napi_env env, napi_callback_info info);
napi_value mesh_claim_recv_batch (napi_env env, napi_callback_info info);
napi_value mesh_reply (napi_env env, napi_callback_info info);

// --- Spot ---
napi_value spot_new (napi_env env, napi_callback_info info);
napi_value mesh_node_entry_spot (napi_env env, napi_callback_info info);
napi_value mesh_node_spot_lookup (napi_env env, napi_callback_info info);
napi_value mesh_node_spot_get_or_new (napi_env env, napi_callback_info info);
napi_value spot_destroy (napi_env env, napi_callback_info info);
napi_value spot_status (napi_env env, napi_callback_info info);
napi_value spot_send_to_channel (napi_env env, napi_callback_info info);
napi_value spot_request_to_channel (napi_env env, napi_callback_info info);
napi_value spot_send_to_spot (napi_env env, napi_callback_info info);
napi_value spot_request_to_spot (napi_env env, napi_callback_info info);
napi_value spot_publish (napi_env env, napi_callback_info info);
napi_value spot_set_publish_option (napi_env env, napi_callback_info info);
napi_value spot_get_publish_option (napi_env env, napi_callback_info info);
napi_value spot_set_subscription (napi_env env, napi_callback_info info);
napi_value spot_unset_subscription (napi_env env, napi_callback_info info);

// --- Actor ---
napi_value mesh_node_actor_new (napi_env env, napi_callback_info info);
napi_value mesh_node_actor_lookup (napi_env env, napi_callback_info info);
napi_value mesh_node_actor_lookup_remote (napi_env env, napi_callback_info info);
napi_value mesh_node_actor_destroy (napi_env env, napi_callback_info info);
napi_value mesh_node_actor_join_spot (napi_env env, napi_callback_info info);
napi_value mesh_node_actor_join_entry_spot (napi_env env, napi_callback_info info);
napi_value mesh_node_actor_leave_spot (napi_env env, napi_callback_info info);
napi_value actor_join_reply (napi_env env, napi_callback_info info);
napi_value mesh_node_send_to_actor (napi_env env, napi_callback_info info);
napi_value mesh_node_request_to_actor (napi_env env, napi_callback_info info);
napi_value actor_send_to_actor (napi_env env, napi_callback_info info);
napi_value actor_request_to_actor (napi_env env, napi_callback_info info);

// --- Stream session service ---
napi_value stream_session_service_new (napi_env env, napi_callback_info info);
napi_value stream_session_service_start (napi_env env, napi_callback_info info);
napi_value stream_session_service_shutdown (napi_env env, napi_callback_info info);
napi_value stream_session_service_destroy (napi_env env, napi_callback_info info);
napi_value stream_session_service_status (napi_env env, napi_callback_info info);
napi_value stream_session_bind_actor (napi_env env, napi_callback_info info);
napi_value stream_session_unbind_actor (napi_env env, napi_callback_info info);
napi_value stream_session_bindings (napi_env env, napi_callback_info info);
napi_value stream_session_send_to_actor (napi_env env, napi_callback_info info);
napi_value stream_session_request_to_actor (napi_env env, napi_callback_info info);
napi_value mesh_node_actor_send_bound_session (napi_env env, napi_callback_info info);
napi_value mesh_node_actor_close_bound_session (napi_env env, napi_callback_info info);
