/* SPDX-License-Identifier: MPL-2.0 */

#pragma once

#include "addon_common_api.h"

napi_value registry_new (napi_env env, napi_callback_info info);
napi_value registry_set_endpoints (napi_env env, napi_callback_info info);
napi_value registry_set_id (napi_env env, napi_callback_info info);
napi_value registry_add_peer (napi_env env, napi_callback_info info);
napi_value registry_set_heartbeat (napi_env env, napi_callback_info info);
napi_value registry_set_broadcast (napi_env env, napi_callback_info info);
napi_value registry_set_tls_server (napi_env env, napi_callback_info info);
napi_value registry_set_tls_client (napi_env env, napi_callback_info info);
napi_value registry_start (napi_env env, napi_callback_info info);
napi_value registry_destroy (napi_env env, napi_callback_info info);

napi_value discovery_new (napi_env env, napi_callback_info info);
napi_value discovery_connect (napi_env env, napi_callback_info info);
napi_value discovery_get_providers (napi_env env, napi_callback_info info);
napi_value discovery_set_value (napi_env env, napi_callback_info info);
napi_value discovery_get_value (napi_env env, napi_callback_info info);
napi_value discovery_resolve_spot (napi_env env, napi_callback_info info);
napi_value discovery_resolve_actor (napi_env env, napi_callback_info info);
napi_value discovery_destroy (napi_env env, napi_callback_info info);
napi_value discovery_set_tls_client (napi_env env, napi_callback_info info);

napi_value registry_status (napi_env env, napi_callback_info info);
napi_value registry_service_summary (napi_env env, napi_callback_info info);
napi_value registry_topology (napi_env env, napi_callback_info info);
napi_value registry_member_peers (napi_env env, napi_callback_info info);
napi_value registry_query_client_new (napi_env env, napi_callback_info info);
napi_value registry_query_client_connect (napi_env env, napi_callback_info info);
napi_value registry_query_topology (napi_env env, napi_callback_info info);
napi_value registry_query_destroy (napi_env env, napi_callback_info info);
