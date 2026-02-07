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

napi_value version(napi_env env, napi_callback_info info);

napi_value ctx_new(napi_env env, napi_callback_info info);
napi_value ctx_term(napi_env env, napi_callback_info info);

napi_value socket_new(napi_env env, napi_callback_info info);
napi_value socket_close(napi_env env, napi_callback_info info);
napi_value socket_bind(napi_env env, napi_callback_info info);
napi_value socket_connect(napi_env env, napi_callback_info info);
napi_value socket_send(napi_env env, napi_callback_info info);
napi_value socket_recv(napi_env env, napi_callback_info info);
napi_value socket_setopt(napi_env env, napi_callback_info info);
napi_value socket_getopt(napi_env env, napi_callback_info info);

napi_value registry_new(napi_env env, napi_callback_info info);
napi_value registry_set_endpoints(napi_env env, napi_callback_info info);
napi_value registry_set_id(napi_env env, napi_callback_info info);
napi_value registry_add_peer(napi_env env, napi_callback_info info);
napi_value registry_set_heartbeat(napi_env env, napi_callback_info info);
napi_value registry_set_broadcast(napi_env env, napi_callback_info info);
napi_value registry_start(napi_env env, napi_callback_info info);
napi_value registry_setsockopt(napi_env env, napi_callback_info info);
napi_value registry_destroy(napi_env env, napi_callback_info info);

napi_value discovery_new(napi_env env, napi_callback_info info);
napi_value discovery_connect(napi_env env, napi_callback_info info);
napi_value discovery_subscribe(napi_env env, napi_callback_info info);
napi_value discovery_unsubscribe(napi_env env, napi_callback_info info);
napi_value discovery_provider_count(napi_env env, napi_callback_info info);
napi_value discovery_service_available(napi_env env, napi_callback_info info);
napi_value discovery_get_providers(napi_env env, napi_callback_info info);
napi_value discovery_destroy(napi_env env, napi_callback_info info);
napi_value discovery_setsockopt(napi_env env, napi_callback_info info);

napi_value gateway_new(napi_env env, napi_callback_info info);
napi_value gateway_send(napi_env env, napi_callback_info info);
napi_value gateway_recv(napi_env env, napi_callback_info info);
napi_value gateway_set_lb(napi_env env, napi_callback_info info);
napi_value gateway_set_tls(napi_env env, napi_callback_info info);
napi_value gateway_connection_count(napi_env env, napi_callback_info info);
napi_value gateway_setsockopt(napi_env env, napi_callback_info info);
napi_value gateway_destroy(napi_env env, napi_callback_info info);

napi_value provider_new(napi_env env, napi_callback_info info);
napi_value provider_bind(napi_env env, napi_callback_info info);
napi_value provider_connect_registry(napi_env env, napi_callback_info info);
napi_value provider_register(napi_env env, napi_callback_info info);
napi_value provider_update_weight(napi_env env, napi_callback_info info);
napi_value provider_unregister(napi_env env, napi_callback_info info);
napi_value provider_register_result(napi_env env, napi_callback_info info);
napi_value provider_set_tls_server(napi_env env, napi_callback_info info);
napi_value provider_router(napi_env env, napi_callback_info info);
napi_value provider_setsockopt(napi_env env, napi_callback_info info);
napi_value provider_destroy(napi_env env, napi_callback_info info);

napi_value spot_node_new(napi_env env, napi_callback_info info);
napi_value spot_node_destroy(napi_env env, napi_callback_info info);
napi_value spot_node_bind(napi_env env, napi_callback_info info);
napi_value spot_node_connect_registry(napi_env env, napi_callback_info info);
napi_value spot_node_connect_peer(napi_env env, napi_callback_info info);
napi_value spot_node_disconnect_peer(napi_env env, napi_callback_info info);
napi_value spot_node_register(napi_env env, napi_callback_info info);
napi_value spot_node_unregister(napi_env env, napi_callback_info info);
napi_value spot_node_set_discovery(napi_env env, napi_callback_info info);
napi_value spot_node_set_tls_server(napi_env env, napi_callback_info info);
napi_value spot_node_set_tls_client(napi_env env, napi_callback_info info);
napi_value spot_node_pub_socket(napi_env env, napi_callback_info info);
napi_value spot_node_sub_socket(napi_env env, napi_callback_info info);

napi_value spot_new(napi_env env, napi_callback_info info);
napi_value spot_destroy(napi_env env, napi_callback_info info);
napi_value spot_topic_create(napi_env env, napi_callback_info info);
napi_value spot_topic_destroy(napi_env env, napi_callback_info info);
napi_value spot_publish(napi_env env, napi_callback_info info);
napi_value spot_subscribe(napi_env env, napi_callback_info info);
napi_value spot_subscribe_pattern(napi_env env, napi_callback_info info);
napi_value spot_unsubscribe(napi_env env, napi_callback_info info);
napi_value spot_recv(napi_env env, napi_callback_info info);
napi_value spot_pub_socket(napi_env env, napi_callback_info info);
napi_value spot_sub_socket(napi_env env, napi_callback_info info);

napi_value monitor_open(napi_env env, napi_callback_info info);
napi_value monitor_recv(napi_env env, napi_callback_info info);

napi_value poll(napi_env env, napi_callback_info info);
