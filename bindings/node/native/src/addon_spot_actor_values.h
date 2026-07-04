/* SPDX-License-Identifier: MPL-2.0 */

#pragma once

#include "addon_common_api.h"

bool parse_routing_id_value (napi_env env, napi_value value, zlink_routing_id_t *routing_id);
bool parse_actor_ref_value (napi_env env, napi_value value, zlink_actor_ref_t *out);
bool parse_actor_recv_info_value (napi_env env, napi_value value, zlink_actor_recv_info_t *out);
napi_value create_actor_ref_value (napi_env env, const zlink_actor_ref_t &actor);
napi_value create_actor_part_value (napi_env env,
                                    const zlink_actor_recv_info_t &info,
                                    zlink_msg_t *part,
                                    int more);
napi_value create_spot_node_actor_entry_value (napi_env env,
                                               const zlink_spot_node_actor_entry_t &entry);
