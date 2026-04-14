#pragma once

#include <node_api.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <string>
#include <vector>

#include "zlink.h"

napi_value throw_last_error(napi_env env, const char *prefix);
std::string get_string(napi_env env, napi_value val);
bool build_msg_vector(napi_env env, napi_value arr, std::vector<zlink_msg_t> *out);
void close_msg_vector(std::vector<zlink_msg_t> &parts);
void release_socket_recv_handler_slot(void *socket);
void release_router_handler_slot(void *socket);
void release_socket_subscribe_handler_slot(void *socket);
void release_socket_send_ready_handler_slot(void *socket);
void release_socket_monitor_handler_slot(void *monitor);
void release_service_monitor_handler_slot(void *monitor);
bool attach_socket_subscribe_handler(napi_env env, void *socket, napi_value handler);

napi_value version(napi_env env, napi_callback_info info);

napi_value ctx_new(napi_env env, napi_callback_info info);
napi_value ctx_shutdown(napi_env env, napi_callback_info info);
napi_value ctx_term(napi_env env, napi_callback_info info);
napi_value ctx_setopt(napi_env env, napi_callback_info info);
napi_value ctx_getopt(napi_env env, napi_callback_info info);

napi_value socket_new(napi_env env, napi_callback_info info);
napi_value socket_close(napi_env env, napi_callback_info info);
napi_value socket_bind(napi_env env, napi_callback_info info);
napi_value socket_unbind(napi_env env, napi_callback_info info);
napi_value socket_connect(napi_env env, napi_callback_info info);
napi_value socket_disconnect(napi_env env, napi_callback_info info);
napi_value socket_set_tls_server(napi_env env, napi_callback_info info);
napi_value socket_set_tls_client(napi_env env, napi_callback_info info);
napi_value socket_attach_discovery(napi_env env, napi_callback_info info);
napi_value socket_send(napi_env env, napi_callback_info info);
napi_value socket_send_parts(napi_env env, napi_callback_info info);
napi_value socket_publish(napi_env env, napi_callback_info info);
napi_value socket_try_publish(napi_env env, napi_callback_info info);
napi_value socket_try_send(napi_env env, napi_callback_info info);
napi_value socket_try_send_parts(napi_env env, napi_callback_info info);
napi_value socket_try_send_routing(napi_env env, napi_callback_info info);
napi_value socket_try_send_routing_parts(napi_env env, napi_callback_info info);
napi_value socket_send_from(napi_env env, napi_callback_info info);
napi_value socket_recv(napi_env env, napi_callback_info info);
napi_value socket_recv_message(napi_env env, napi_callback_info info);
napi_value socket_try_recv_message(napi_env env, napi_callback_info info);
napi_value socket_recv_handler(napi_env env, napi_callback_info info);
napi_value socket_subscribe_message(napi_env env, napi_callback_info info);
napi_value socket_try_subscribe_message(napi_env env, napi_callback_info info);
napi_value socket_subscribe_handler(napi_env env, napi_callback_info info);
napi_value socket_send_ready_handler(napi_env env, napi_callback_info info);
napi_value socket_subscription_event(napi_env env, napi_callback_info info);
napi_value socket_try_subscription_event(napi_env env, napi_callback_info info);
napi_value socket_recv_into(napi_env env, napi_callback_info info);
napi_value socket_recv_msg_into(napi_env env, napi_callback_info info);
napi_value socket_stream_attach(napi_env env, napi_callback_info info);
napi_value socket_stream_detach(napi_env env, napi_callback_info info);
napi_value socket_stream_peer_routing_id(napi_env env, napi_callback_info info);
napi_value socket_stream_send(napi_env env, napi_callback_info info);
napi_value socket_setopt(napi_env env, napi_callback_info info);
napi_value socket_getopt(napi_env env, napi_callback_info info);
napi_value handle_set_routing_id(napi_env env, napi_callback_info info);
napi_value handle_get_routing_id(napi_env env, napi_callback_info info);
napi_value dealer_request(napi_env env, napi_callback_info info);
napi_value router_request(napi_env env, napi_callback_info info);
napi_value router_reply(napi_env env, napi_callback_info info);
napi_value router_handler_message(napi_env env, napi_callback_info info);
napi_value router_recv_message(napi_env env, napi_callback_info info);
napi_value router_try_recv_message(napi_env env, napi_callback_info info);
napi_value router_spot_send(napi_env env, napi_callback_info info);
napi_value router_spot_request(napi_env env, napi_callback_info info);
napi_value router_spot_reply(napi_env env, napi_callback_info info);
napi_value router_spot_handler(napi_env env, napi_callback_info info);
napi_value router_spot_recv(napi_env env, napi_callback_info info);

napi_value registry_new(napi_env env, napi_callback_info info);
napi_value registry_set_endpoints(napi_env env, napi_callback_info info);
napi_value registry_set_id(napi_env env, napi_callback_info info);
napi_value registry_add_peer(napi_env env, napi_callback_info info);
napi_value registry_set_heartbeat(napi_env env, napi_callback_info info);
napi_value registry_set_broadcast(napi_env env, napi_callback_info info);
napi_value registry_set_tls_server(napi_env env, napi_callback_info info);
napi_value registry_set_tls_client(napi_env env, napi_callback_info info);
napi_value registry_start(napi_env env, napi_callback_info info);
napi_value registry_setsockopt(napi_env env, napi_callback_info info);
napi_value registry_destroy(napi_env env, napi_callback_info info);

napi_value discovery_new(napi_env env, napi_callback_info info);
napi_value discovery_connect(napi_env env, napi_callback_info info);
napi_value discovery_provider_count(napi_env env, napi_callback_info info);
napi_value discovery_service_available(napi_env env, napi_callback_info info);
napi_value discovery_get_providers(napi_env env, napi_callback_info info);
napi_value discovery_member_peer_metadata(napi_env env, napi_callback_info info);
napi_value discovery_open_monitor(napi_env env, napi_callback_info info);
napi_value service_monitor_handler(napi_env env, napi_callback_info info);
napi_value service_monitor_recv(napi_env env, napi_callback_info info);
napi_value service_monitor_try_recv(napi_env env, napi_callback_info info);
napi_value discovery_set_value(napi_env env, napi_callback_info info);
napi_value discovery_get_value(napi_env env, napi_callback_info info);
napi_value discovery_set_metadata(napi_env env, napi_callback_info info);
napi_value discovery_get_metadata(napi_env env, napi_callback_info info);
napi_value discovery_resolve_spot(napi_env env, napi_callback_info info);
napi_value discovery_set_dealer_peer_mode(napi_env env, napi_callback_info info);
napi_value discovery_destroy(napi_env env, napi_callback_info info);
napi_value discovery_set_tls_client(napi_env env, napi_callback_info info);

napi_value registry_status_snapshot(napi_env env, napi_callback_info info);
napi_value registry_service_summary_snapshot(napi_env env, napi_callback_info info);
napi_value registry_topology_snapshot(napi_env env, napi_callback_info info);
napi_value registry_topology_query(napi_env env, napi_callback_info info);
napi_value registry_member_peers(napi_env env, napi_callback_info info);
napi_value registry_member_peer_metadata(napi_env env, napi_callback_info info);
napi_value registry_query_client_new(napi_env env, napi_callback_info info);
napi_value registry_query_client_connect(napi_env env, napi_callback_info info);
napi_value registry_query_snapshot(napi_env env, napi_callback_info info);
napi_value registry_query_destroy(napi_env env, napi_callback_info info);

napi_value provider_new(napi_env env, napi_callback_info info);
napi_value provider_bind(napi_env env, napi_callback_info info);
napi_value provider_connect_registry(napi_env env, napi_callback_info info);
napi_value provider_register(napi_env env, napi_callback_info info);
napi_value provider_update_weight(napi_env env, napi_callback_info info);
napi_value provider_unregister(napi_env env, napi_callback_info info);
napi_value provider_register_result(napi_env env, napi_callback_info info);
napi_value provider_set_tls_server(napi_env env, napi_callback_info info);
napi_value provider_router(napi_env env, napi_callback_info info);
napi_value provider_router_peers(napi_env env, napi_callback_info info);
napi_value provider_setsockopt(napi_env env, napi_callback_info info);
napi_value provider_destroy(napi_env env, napi_callback_info info);

napi_value spot_node_new(napi_env env, napi_callback_info info);
napi_value spot_node_destroy(napi_env env, napi_callback_info info);
napi_value spot_node_bind(napi_env env, napi_callback_info info);
napi_value spot_node_connect_peer(napi_env env, napi_callback_info info);
napi_value spot_node_disconnect_peer(napi_env env, napi_callback_info info);
napi_value spot_node_register(napi_env env, napi_callback_info info);
napi_value spot_node_unregister(napi_env env, napi_callback_info info);
napi_value spot_node_set_discovery(napi_env env, napi_callback_info info);
napi_value spot_node_set_tls_server(napi_env env, napi_callback_info info);
napi_value spot_node_set_tls_client(napi_env env, napi_callback_info info);
napi_value spot_node_setsockopt(napi_env env, napi_callback_info info);
napi_value spot_node_status_snapshot(napi_env env, napi_callback_info info);
napi_value spot_node_peers_snapshot(napi_env env, napi_callback_info info);
napi_value spot_node_peers_query(napi_env env, napi_callback_info info);
napi_value spot_node_subjects_snapshot(napi_env env, napi_callback_info info);
napi_value spot_node_pub_socket(napi_env env, napi_callback_info info);
napi_value spot_node_sub_socket(napi_env env, napi_callback_info info);
napi_value spot_node_pub_peers(napi_env env, napi_callback_info info);
napi_value spot_node_sub_peers(napi_env env, napi_callback_info info);

napi_value spot_new(napi_env env, napi_callback_info info);
napi_value spot_destroy(napi_env env, napi_callback_info info);
napi_value spot_publish(napi_env env, napi_callback_info info);
napi_value spot_try_publish(napi_env env, napi_callback_info info);
napi_value spot_send_spot(napi_env env, napi_callback_info info);
napi_value spot_request_spot(napi_env env, napi_callback_info info);
napi_value spot_reply_spot(napi_env env, napi_callback_info info);
napi_value spot_send_router(napi_env env, napi_callback_info info);
napi_value spot_request_to_router(napi_env env, napi_callback_info info);
napi_value spot_reply_router(napi_env env, napi_callback_info info);
napi_value spot_routed_handler(napi_env env, napi_callback_info info);
napi_value spot_dispatch_event_handler(napi_env env, napi_callback_info info);
napi_value spot_recv_routed(napi_env env, napi_callback_info info);
napi_value spot_send_ready_handler(napi_env env, napi_callback_info info);
napi_value spot_subscribe(napi_env env, napi_callback_info info);
napi_value spot_subscribe_pattern(napi_env env, napi_callback_info info);
napi_value spot_unsubscribe(napi_env env, napi_callback_info info);
napi_value spot_recv(napi_env env, napi_callback_info info);
napi_value spot_try_recv(napi_env env, napi_callback_info info);
napi_value spot_subscribe_handler(napi_env env, napi_callback_info info);
napi_value monitor_open(napi_env env, napi_callback_info info);
napi_value monitor_handler(napi_env env, napi_callback_info info);
napi_value monitor_recv(napi_env env, napi_callback_info info);
napi_value monitor_try_recv(napi_env env, napi_callback_info info);
napi_value monitor_snapshot(napi_env env, napi_callback_info info);
napi_value monitor_close(napi_env env, napi_callback_info info);

napi_value poller_new(napi_env env, napi_callback_info info);
napi_value poller_destroy(napi_env env, napi_callback_info info);
napi_value poller_size(napi_env env, napi_callback_info info);
napi_value poller_add(napi_env env, napi_callback_info info);
napi_value poller_modify(napi_env env, napi_callback_info info);
napi_value poller_remove(napi_env env, napi_callback_info info);
napi_value poller_add_fd(napi_env env, napi_callback_info info);
napi_value poller_modify_fd(napi_env env, napi_callback_info info);
napi_value poller_remove_fd(napi_env env, napi_callback_info info);
napi_value poller_add_timer(napi_env env, napi_callback_info info);
napi_value poller_remove_timer(napi_env env, napi_callback_info info);
napi_value poller_wait(napi_env env, napi_callback_info info);
napi_value poller_wait_all(napi_env env, napi_callback_info info);

napi_value timer_new(napi_env env, napi_callback_info info);
napi_value spot_timer_new(napi_env env, napi_callback_info info);
napi_value timer_destroy(napi_env env, napi_callback_info info);
napi_value timer_start(napi_env env, napi_callback_info info);
napi_value timer_stop(napi_env env, napi_callback_info info);
napi_value timer_recv(napi_env env, napi_callback_info info);
napi_value timer_handler(napi_env env, napi_callback_info info);

napi_value stopwatch_start(napi_env env, napi_callback_info info);
napi_value stopwatch_intermediate(napi_env env, napi_callback_info info);
napi_value stopwatch_stop(napi_env env, napi_callback_info info);

napi_value poll(napi_env env, napi_callback_info info);
