/* SPDX-License-Identifier: MPL-2.0 */

#pragma once

#include "addon_common_api.h"

napi_value spot_node_new(napi_env env, napi_callback_info info);
napi_value spot_node_destroy(napi_env env, napi_callback_info info);
napi_value spot_node_set_pub_bind(napi_env env, napi_callback_info info);
napi_value spot_node_set_router_bind(napi_env env, napi_callback_info info);
napi_value spot_node_connect_peer(napi_env env, napi_callback_info info);
napi_value spot_node_disconnect_peer(napi_env env, napi_callback_info info);
napi_value spot_node_disconnect_peer_rid(napi_env env, napi_callback_info info);
napi_value spot_node_connect_router_channel_peer(napi_env env, napi_callback_info info);
napi_value spot_node_disconnect_router_channel_peer(napi_env env, napi_callback_info info);
napi_value spot_node_disconnect_router_channel_peer_rid(napi_env env, napi_callback_info info);
napi_value spot_node_set_discovery(napi_env env, napi_callback_info info);
napi_value spot_node_attach_router_channel_discovery(napi_env env, napi_callback_info info);
napi_value spot_node_attach_channel_dealer(napi_env env, napi_callback_info info);
napi_value spot_node_attach_channel_dealer_manual(napi_env env, napi_callback_info info);
napi_value spot_node_attach_pub_ingress(napi_env env, napi_callback_info info);
napi_value spot_node_set_tls_server(napi_env env, napi_callback_info info);
napi_value spot_node_set_tls_client(napi_env env, napi_callback_info info);
napi_value spot_node_setsockopt(napi_env env, napi_callback_info info);
napi_value spot_node_getsockopt(napi_env env, napi_callback_info info);
napi_value spot_node_entry_spot(napi_env env, napi_callback_info info);
napi_value spot_node_spot_lookup(napi_env env, napi_callback_info info);
napi_value spot_node_spot_get_or_new(napi_env env, napi_callback_info info);
napi_value spot_node_status(napi_env env, napi_callback_info info);
napi_value spot_node_peers(napi_env env, napi_callback_info info);
napi_value spot_node_peers_query(napi_env env, napi_callback_info info);
napi_value spot_node_subjects(napi_env env, napi_callback_info info);
napi_value spot_node_internal_sockets(napi_env env, napi_callback_info info);
napi_value spot_node_spots(napi_env env, napi_callback_info info);
napi_value spot_node_actors(napi_env env, napi_callback_info info);
napi_value spot_node_actor_new(napi_env env, napi_callback_info info);
napi_value spot_node_actor_destroy(napi_env env, napi_callback_info info);
napi_value spot_node_actor_lookup(napi_env env, napi_callback_info info);
napi_value spot_node_actor_join_spot(napi_env env, napi_callback_info info);
napi_value spot_node_actor_join_entry_spot(napi_env env, napi_callback_info info);
napi_value spot_node_actor_leave_spot(napi_env env, napi_callback_info info);
napi_value spot_node_actor_recv_part(napi_env env, napi_callback_info info);
napi_value spot_node_actor_send_bound_session_msg(napi_env env, napi_callback_info info);
napi_value spot_node_actor_close_bound_session(napi_env env, napi_callback_info info);
napi_value stream_attach_actor_gateway(napi_env env, napi_callback_info info);
napi_value stream_bind_actor(napi_env env, napi_callback_info info);
napi_value stream_unbind_actor(napi_env env, napi_callback_info info);
napi_value stream_send_bound_actor_part(napi_env env, napi_callback_info info);
napi_value stream_bound_actors(napi_env env, napi_callback_info info);
napi_value remote_actor_get_ref(napi_env env, napi_callback_info info);

napi_value spot_new(napi_env env, napi_callback_info info);
napi_value spot_destroy(napi_env env, napi_callback_info info);
napi_value spot_publish(napi_env env, napi_callback_info info);
napi_value spot_send_channel(napi_env env, napi_callback_info info);
napi_value spot_send_spot(napi_env env, napi_callback_info info);
napi_value spot_send_spot_no_wait_result(napi_env env, napi_callback_info info);
napi_value spot_request_channel(napi_env env, napi_callback_info info);
napi_value spot_request_spot(napi_env env, napi_callback_info info);
napi_value spot_request_router(napi_env env, napi_callback_info info);
napi_value spot_set_option(napi_env env, napi_callback_info info);
napi_value spot_get_option(napi_env env, napi_callback_info info);
napi_value spot_reply_spot(napi_env env, napi_callback_info info);
napi_value spot_reply_router(napi_env env, napi_callback_info info);
napi_value spot_dispatch_event_handler(napi_env env, napi_callback_info info);
napi_value spot_recv_routed(napi_env env, napi_callback_info info);
napi_value spot_recv_routed_no_wait(napi_env env, napi_callback_info info);
napi_value spot_recv_actor_lifecycle(napi_env env, napi_callback_info info);
napi_value spot_actor_join_recv(napi_env env, napi_callback_info info);
napi_value spot_actor_join_reply(napi_env env, napi_callback_info info);
napi_value spot_actors(napi_env env, napi_callback_info info);
napi_value spot_send_ready_handler(napi_env env, napi_callback_info info);
napi_value spot_subscribe(napi_env env, napi_callback_info info);
napi_value spot_unsubscribe(napi_env env, napi_callback_info info);
napi_value spot_recv(napi_env env, napi_callback_info info);
napi_value spot_try_recv(napi_env env, napi_callback_info info);
