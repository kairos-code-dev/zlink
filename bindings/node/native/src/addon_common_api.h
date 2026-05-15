/* SPDX-License-Identifier: MPL-2.0 */

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
bool attach_socket_subscribe_handler(napi_env env, void *socket, napi_value handler);
