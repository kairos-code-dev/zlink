/* SPDX-License-Identifier: MPL-2.0 */

// RouteMesh 10.0.0 service layer: MeshNode + pull dispatch + spot/actor/
// stream_session. Each napi_value entry point maps 1:1 to a
// zlink_mesh_node_* / zlink_spot_* / zlink_actor_* / zlink_stream_session_*
// C API in core/include/zlink/service/*.h.

#include "addon_common_api.h"
#include "addon_message_parts.h"
#include "addon_message_values.h"
#include "addon_spot_api.h"

#include <cstring>
#include <new>
#include <string>
#include <vector>

namespace
{

// --- small argument / value helpers -------------------------------------

void *svc_external (napi_env env, napi_value value)
{
    void *ptr = NULL;
    napi_get_value_external (env, value, &ptr);
    return ptr;
}

int32_t svc_int32 (napi_env env, napi_value value)
{
    int32_t out = 0;
    napi_get_value_int32 (env, value, &out);
    return out;
}

uint32_t svc_uint32 (napi_env env, napi_value value)
{
    uint32_t out = 0;
    napi_get_value_uint32 (env, value, &out);
    return out;
}

uint64_t svc_u64 (napi_env env, napi_value value)
{
    napi_valuetype type = napi_undefined;
    napi_typeof (env, value, &type);
    if (type == napi_bigint) {
        uint64_t out = 0;
        bool lossless = false;
        napi_get_value_bigint_uint64 (env, value, &out, &lossless);
        return out;
    }
    int64_t signed_out = 0;
    napi_get_value_int64 (env, value, &signed_out);
    return static_cast<uint64_t> (signed_out);
}

napi_value svc_create_u64 (napi_env env, uint64_t value)
{
    napi_value out;
    napi_create_bigint_uint64 (env, value, &out);
    return out;
}

void svc_set_bigint (napi_env env, napi_value obj, const char *name, uint64_t value)
{
    napi_set_named_property (env, obj, name, svc_create_u64 (env, value));
}

void svc_set_int32 (napi_env env, napi_value obj, const char *name, int32_t value)
{
    napi_value out;
    napi_create_int32 (env, value, &out);
    napi_set_named_property (env, obj, name, out);
}

void svc_set_bool (napi_env env, napi_value obj, const char *name, bool value)
{
    napi_value out;
    napi_get_boolean (env, value, &out);
    napi_set_named_property (env, obj, name, out);
}

// Read an optional string property from a JS object; returns true when present.
bool svc_opt_string (napi_env env, napi_value obj, const char *name, std::string *out)
{
    napi_value value;
    if (napi_get_named_property (env, obj, name, &value) != napi_ok)
        return false;
    napi_valuetype type = napi_undefined;
    napi_typeof (env, value, &type);
    if (type != napi_string)
        return false;
    *out = get_string (env, value);
    return true;
}

// Build a metadata view from a Buffer arg, or NULL when absent/empty.
const zlink_mesh_metadata_view_t *svc_metadata (napi_env env,
                                                napi_value value,
                                                zlink_mesh_metadata_view_t *storage)
{
    napi_valuetype type = napi_undefined;
    napi_typeof (env, value, &type);
    if (type == napi_undefined || type == napi_null)
        return NULL;
    bool is_buffer = false;
    napi_is_buffer (env, value, &is_buffer);
    if (!is_buffer)
        return NULL;
    void *data = NULL;
    size_t len = 0;
    napi_get_buffer_info (env, value, &data, &len);
    if (len == 0)
        return NULL;
    storage->data = static_cast<const uint8_t *> (data);
    storage->size = len;
    return storage;
}

// Build optional parts (accepts array, single Buffer/Message, or undefined/null).
bool svc_build_parts (napi_env env, napi_value value, std::vector<zlink_msg_t> *out)
{
    napi_valuetype type = napi_undefined;
    napi_typeof (env, value, &type);
    if (type == napi_undefined || type == napi_null)
        return true;
    return build_msg_vector_or_single (env, value, out);
}

const zlink_msg_t *svc_parts_ptr (const std::vector<zlink_msg_t> &parts)
{
    return parts.empty () ? NULL : parts.data ();
}

// --- actor_ref marshaling -----------------------------------------------

bool svc_parse_actor_ref (napi_env env, napi_value value, zlink_actor_ref_t *out)
{
    memset (out, 0, sizeof (*out));
    napi_valuetype type = napi_undefined;
    napi_typeof (env, value, &type);
    if (type != napi_object) {
        napi_throw_type_error (env, NULL, "actor ref must be an object");
        return false;
    }
    napi_value node_rid_value;
    napi_get_named_property (env, value, "nodeRid", &node_rid_value);
    napi_valuetype node_rid_type = napi_undefined;
    napi_typeof (env, node_rid_value, &node_rid_type);
    if (node_rid_type == napi_undefined || node_rid_type == napi_null)
        napi_get_named_property (env, value, "node_rid", &node_rid_value);
    if (!parse_routing_id_value (env, node_rid_value, &out->node_rid))
        return false;

    napi_value actor_id_value;
    napi_get_named_property (env, value, "actorId", &actor_id_value);
    napi_valuetype actor_id_type = napi_undefined;
    napi_typeof (env, actor_id_value, &actor_id_type);
    if (actor_id_type != napi_string)
        napi_get_named_property (env, value, "actor_id", &actor_id_value);
    std::string actor_id = get_string (env, actor_id_value);
    if (actor_id.empty () || actor_id.size () > ZLINK_ACTOR_ID_MAX) {
        napi_throw_range_error (env, NULL, "actorId must be 1..255 bytes");
        return false;
    }
    memcpy (out->actor_id, actor_id.data (), actor_id.size ());
    out->actor_id[actor_id.size ()] = '\0';

    napi_value generation_value;
    napi_get_named_property (env, value, "generation", &generation_value);
    napi_valuetype generation_type = napi_undefined;
    napi_typeof (env, generation_value, &generation_type);
    if (generation_type == napi_bigint || generation_type == napi_number)
        out->generation = svc_u64 (env, generation_value);
    return true;
}

napi_value svc_create_actor_ref (napi_env env, const zlink_actor_ref_t &actor)
{
    napi_value obj;
    napi_create_object (env, &obj);
    napi_set_named_property (env, obj, "nodeRid", create_routing_id_value (env, actor.node_rid));
    set_string_property (env, obj, "actorId", actor.actor_id);
    napi_set_named_property (env, obj, "generation", svc_create_u64 (env, actor.generation));
    return obj;
}

napi_value svc_create_operation_id (napi_env env, const zlink_mesh_operation_id_t &op)
{
    napi_value obj;
    napi_create_object (env, &obj);
    napi_set_named_property (env, obj, "high", svc_create_u64 (env, op.high));
    napi_set_named_property (env, obj, "low", svc_create_u64 (env, op.low));
    return obj;
}

napi_value svc_create_publish_detail (napi_env env, const zlink_mesh_publish_detail_t &detail)
{
    napi_value obj;
    napi_create_object (env, &obj);
    set_uint32_property (env, obj, "snapshotRemoteTargetCount", detail.snapshot_remote_target_count);
    set_uint32_property (env, obj, "admittedRemoteTargetCount", detail.admitted_remote_target_count);
    set_uint32_property (env, obj, "droppedRemoteTargetCount", detail.dropped_remote_target_count);
    set_uint32_property (env, obj, "unreachableRemoteTargetCount",
                         detail.unreachable_remote_target_count);
    set_uint32_property (env, obj, "snapshotLocalSpotCount", detail.snapshot_local_spot_count);
    set_uint32_property (env, obj, "admittedLocalSpotCount", detail.admitted_local_spot_count);
    set_uint32_property (env, obj, "droppedLocalSpotCount", detail.dropped_local_spot_count);
    return obj;
}

bool svc_parse_reply_token (napi_env env, napi_value value, zlink_mesh_reply_token_t *out)
{
    bool is_buffer = false;
    napi_is_buffer (env, value, &is_buffer);
    if (!is_buffer) {
        napi_throw_type_error (env, NULL, "reply token must be a Buffer");
        return false;
    }
    void *data = NULL;
    size_t len = 0;
    napi_get_buffer_info (env, value, &data, &len);
    if (len != sizeof (*out)) {
        napi_throw_range_error (env, NULL, "reply token has an unexpected size");
        return false;
    }
    memcpy (out, data, sizeof (*out));
    return true;
}

napi_value svc_create_reply_token (napi_env env, const zlink_mesh_reply_token_t &token)
{
    napi_value buffer;
    void *data = NULL;
    napi_create_buffer_copy (env, sizeof (token), &token, &data, &buffer);
    return buffer;
}

napi_value svc_string_or_null (napi_env env, const char *data, size_t size)
{
    if (!data || size == 0) {
        napi_value out;
        napi_get_null (env, &out);
        return out;
    }
    napi_value out;
    napi_create_string_utf8 (env, data, size, &out);
    return out;
}

napi_value svc_buffer_or_null (napi_env env, const uint8_t *data, size_t size)
{
    if (!data || size == 0) {
        napi_value out;
        napi_get_null (env, &out);
        return out;
    }
    napi_value out;
    void *copy = NULL;
    napi_create_buffer_copy (env, size, data, &copy, &out);
    return out;
}

} // namespace

// ======================================================================
//  MeshNode lifecycle
// ======================================================================

napi_value mesh_node_new (napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *ctx = svc_external (env, argv[0]);

    zlink_mesh_node_options_t options;
    memset (&options, 0, sizeof (options));
    options.struct_size = sizeof (options);
    options.version = ZLINK_MESH_NODE_ABI_VERSION;

    std::string mesh_name;
    std::string trust_profile;
    if (argc >= 2) {
        napi_valuetype type = napi_undefined;
        napi_typeof (env, argv[1], &type);
        if (type == napi_object) {
            if (svc_opt_string (env, argv[1], "meshName", &mesh_name)) {
                options.mesh_name = mesh_name.c_str ();
                options.mesh_name_size = mesh_name.size ();
            }
            if (svc_opt_string (env, argv[1], "trustProfile", &trust_profile)) {
                options.trust_profile = trust_profile.c_str ();
                options.trust_profile_size = trust_profile.size ();
            }
        }
    }

    void *node = zlink_mesh_node_new (ctx, &options);
    if (!node)
        return throw_last_error (env, "meshNodeNew failed");
    napi_value ext;
    napi_create_external (env, node, NULL, NULL, &ext);
    return ext;
}

napi_value mesh_node_set_bind (napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *node = svc_external (env, argv[0]);
    std::string endpoint = get_string (env, argv[1]);
    if (zlink_mesh_node_set_bind (node, endpoint.c_str ()) != ZLINK_CONFIG_OK)
        return throw_last_error (env, "meshNodeSetBind failed");
    napi_value undef;
    napi_get_undefined (env, &undef);
    return undef;
}

napi_value mesh_node_start (napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *node = svc_external (env, argv[0]);
    if (zlink_mesh_node_start (node) != ZLINK_CONFIG_OK)
        return throw_last_error (env, "meshNodeStart failed");
    napi_value undef;
    napi_get_undefined (env, &undef);
    return undef;
}

napi_value mesh_node_shutdown (napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *node = svc_external (env, argv[0]);
    uint32_t timeout_ms = svc_uint32 (env, argv[1]);
    zlink_request_result_t rc = zlink_mesh_node_shutdown (node, timeout_ms);
    napi_value out;
    napi_create_int32 (env, static_cast<int32_t> (rc), &out);
    return out;
}

napi_value mesh_node_destroy (napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *node = svc_external (env, argv[0]);
    zlink_mesh_node_destroy (&node);
    napi_value undef;
    napi_get_undefined (env, &undef);
    return undef;
}

napi_value mesh_node_add_channel_name (napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *node = svc_external (env, argv[0]);
    std::string channel = get_string (env, argv[1]);
    if (zlink_mesh_node_add_channel_name (node, channel.c_str ()) != ZLINK_CONFIG_OK)
        return throw_last_error (env, "meshNodeAddChannelName failed");
    napi_value undef;
    napi_get_undefined (env, &undef);
    return undef;
}

napi_value mesh_node_set_channel_weight (napi_env env, napi_callback_info info)
{
    napi_value argv[3];
    size_t argc = 3;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *node = svc_external (env, argv[0]);
    std::string channel = get_string (env, argv[1]);
    uint32_t weight = svc_uint32 (env, argv[2]);
    if (zlink_mesh_node_set_channel_weight (node, channel.c_str (), weight) != ZLINK_CONFIG_OK)
        return throw_last_error (env, "meshNodeSetChannelWeight failed");
    napi_value undef;
    napi_get_undefined (env, &undef);
    return undef;
}

// ======================================================================
//  Peers
// ======================================================================

napi_value mesh_node_connect_peer (napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *node = svc_external (env, argv[0]);

    zlink_mesh_peer_connection_options_t options;
    memset (&options, 0, sizeof (options));
    options.struct_size = sizeof (options);
    options.version = ZLINK_MESH_NODE_ABI_VERSION;

    std::string endpoint;
    svc_opt_string (env, argv[1], "endpoint", &endpoint);
    options.endpoint = endpoint.c_str ();
    options.endpoint_size = endpoint.size ();

    napi_value expected_rid;
    napi_get_named_property (env, argv[1], "expectedRid", &expected_rid);
    bool is_buffer = false;
    napi_is_buffer (env, expected_rid, &is_buffer);
    if (is_buffer) {
        if (!parse_routing_id_value (env, expected_rid, &options.expected_rid))
            return NULL;
        options.has_expected_rid = 1;
    }

    uint64_t intent_id = 0;
    if (zlink_mesh_node_connect_peer (node, &options, &intent_id) != ZLINK_CONNECT_OK)
        return throw_last_error (env, "meshNodeConnectPeer failed");
    return svc_create_u64 (env, intent_id);
}

napi_value mesh_node_remove_peer_connection (napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *node = svc_external (env, argv[0]);
    uint64_t intent_id = svc_u64 (env, argv[1]);
    if (zlink_mesh_node_remove_peer_connection (node, intent_id) != ZLINK_CONNECT_OK)
        return throw_last_error (env, "meshNodeRemovePeerConnection failed");
    napi_value undef;
    napi_get_undefined (env, &undef);
    return undef;
}

napi_value mesh_node_disconnect_peer (napi_env env, napi_callback_info info)
{
    napi_value argv[3];
    size_t argc = 3;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *node = svc_external (env, argv[0]);
    zlink_routing_id_t peer_rid;
    if (!parse_routing_id_value (env, argv[1], &peer_rid))
        return NULL;
    uint64_t generation = svc_u64 (env, argv[2]);
    if (zlink_mesh_node_disconnect_peer (node, &peer_rid, generation) != ZLINK_CONNECT_OK)
        return throw_last_error (env, "meshNodeDisconnectPeer failed");
    napi_value undef;
    napi_get_undefined (env, &undef);
    return undef;
}

// ======================================================================
//  Node / channel messaging
// ======================================================================

napi_value mesh_node_send_to_node (napi_env env, napi_callback_info info)
{
    napi_value argv[5];
    size_t argc = 5;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *node = svc_external (env, argv[0]);
    zlink_routing_id_t target_rid;
    if (!parse_routing_id_value (env, argv[1], &target_rid))
        return NULL;
    zlink_mesh_metadata_view_t meta_storage;
    const zlink_mesh_metadata_view_t *meta = svc_metadata (env, argv[2], &meta_storage);
    std::vector<zlink_msg_t> parts;
    if (!svc_build_parts (env, argv[3], &parts))
        return NULL;
    zlink_send_flags_t flags = static_cast<zlink_send_flags_t> (svc_int32 (env, argv[4]));
    zlink_submit_result_t rc =
      zlink_mesh_node_send_to_node (node, &target_rid, meta, svc_parts_ptr (parts), parts.size (),
                                    flags);
    close_msg_vector (parts);
    napi_value out;
    napi_create_int32 (env, static_cast<int32_t> (rc), &out);
    return out;
}

napi_value mesh_node_request_to_node (napi_env env, napi_callback_info info)
{
    napi_value argv[6];
    size_t argc = 6;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *node = svc_external (env, argv[0]);
    zlink_routing_id_t target_rid;
    if (!parse_routing_id_value (env, argv[1], &target_rid))
        return NULL;
    zlink_mesh_metadata_view_t meta_storage;
    const zlink_mesh_metadata_view_t *meta = svc_metadata (env, argv[2], &meta_storage);
    std::vector<zlink_msg_t> parts;
    if (!svc_build_parts (env, argv[3], &parts))
        return NULL;
    zlink_send_flags_t flags = static_cast<zlink_send_flags_t> (svc_int32 (env, argv[4]));
    uint32_t timeout_ms = svc_uint32 (env, argv[5]);
    zlink_mesh_operation_id_t op;
    memset (&op, 0, sizeof (op));
    zlink_submit_result_t rc =
      zlink_mesh_node_request_to_node (node, &target_rid, meta, svc_parts_ptr (parts),
                                       parts.size (), &op, flags, timeout_ms);
    close_msg_vector (parts);
    if (rc != ZLINK_SUBMIT_OK)
        return throw_last_error (env, "meshNodeRequestToNode failed");
    return svc_create_operation_id (env, op);
}

napi_value mesh_node_send_to_channel (napi_env env, napi_callback_info info)
{
    napi_value argv[5];
    size_t argc = 5;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *node = svc_external (env, argv[0]);
    std::string channel = get_string (env, argv[1]);
    zlink_mesh_metadata_view_t meta_storage;
    const zlink_mesh_metadata_view_t *meta = svc_metadata (env, argv[2], &meta_storage);
    std::vector<zlink_msg_t> parts;
    if (!svc_build_parts (env, argv[3], &parts))
        return NULL;
    zlink_send_flags_t flags = static_cast<zlink_send_flags_t> (svc_int32 (env, argv[4]));
    zlink_submit_result_t rc =
      zlink_mesh_node_send_to_channel (node, channel.c_str (), meta, svc_parts_ptr (parts),
                                       parts.size (), flags);
    close_msg_vector (parts);
    napi_value out;
    napi_create_int32 (env, static_cast<int32_t> (rc), &out);
    return out;
}

napi_value mesh_node_request_to_channel (napi_env env, napi_callback_info info)
{
    napi_value argv[6];
    size_t argc = 6;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *node = svc_external (env, argv[0]);
    std::string channel = get_string (env, argv[1]);
    zlink_mesh_metadata_view_t meta_storage;
    const zlink_mesh_metadata_view_t *meta = svc_metadata (env, argv[2], &meta_storage);
    std::vector<zlink_msg_t> parts;
    if (!svc_build_parts (env, argv[3], &parts))
        return NULL;
    zlink_send_flags_t flags = static_cast<zlink_send_flags_t> (svc_int32 (env, argv[4]));
    uint32_t timeout_ms = svc_uint32 (env, argv[5]);
    zlink_mesh_operation_id_t op;
    memset (&op, 0, sizeof (op));
    zlink_submit_result_t rc =
      zlink_mesh_node_request_to_channel (node, channel.c_str (), meta, svc_parts_ptr (parts),
                                          parts.size (), &op, flags, timeout_ms);
    close_msg_vector (parts);
    if (rc != ZLINK_SUBMIT_OK)
        return throw_last_error (env, "meshNodeRequestToChannel failed");
    return svc_create_operation_id (env, op);
}

// ======================================================================
//  Options / introspection
// ======================================================================

napi_value mesh_node_set_option (napi_env env, napi_callback_info info)
{
    napi_value argv[3];
    size_t argc = 3;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *node = svc_external (env, argv[0]);
    int32_t option = svc_int32 (env, argv[1]);
    void *data = NULL;
    size_t len = 0;
    napi_get_buffer_info (env, argv[2], &data, &len);
    if (zlink_set_mesh_node_option (node, static_cast<zlink_mesh_node_option_t> (option), data, len)
        != ZLINK_CONFIG_OK)
        return throw_last_error (env, "meshNodeSetOption failed");
    napi_value undef;
    napi_get_undefined (env, &undef);
    return undef;
}

napi_value mesh_node_get_option (napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *node = svc_external (env, argv[0]);
    int32_t option = svc_int32 (env, argv[1]);
    uint8_t buffer[256];
    size_t len = sizeof (buffer);
    if (zlink_get_mesh_node_option (node, static_cast<zlink_mesh_node_option_t> (option), buffer,
                                    &len)
        != ZLINK_CONFIG_OK)
        return throw_last_error (env, "meshNodeGetOption failed");
    napi_value out;
    void *copy = NULL;
    napi_create_buffer_copy (env, len, buffer, &copy, &out);
    return out;
}

napi_value mesh_node_status (napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *node = svc_external (env, argv[0]);
    zlink_mesh_node_status_t status;
    memset (&status, 0, sizeof (status));
    status.struct_size = sizeof (status);
    status.version = ZLINK_MESH_NODE_ABI_VERSION;
    if (zlink_mesh_node_status (node, &status) != ZLINK_CONFIG_OK)
        return throw_last_error (env, "meshNodeStatus failed");

    napi_value obj;
    napi_create_object (env, &obj);
    svc_set_int32 (env, obj, "state", static_cast<int32_t> (status.state));
    napi_set_named_property (env, obj, "routingId",
                             create_routing_id_value (env, status.routing_id));
    set_string_property (env, obj, "meshName", status.mesh_name);
    set_string_property (env, obj, "localEndpoint", status.local_endpoint);
    svc_set_bigint (env, obj, "lifecycleGeneration", status.lifecycle_generation);
    svc_set_bigint (env, obj, "descriptorRevision", status.descriptor_revision);
    set_uint32_property (env, obj, "channelCount", status.channel_count);
    set_uint32_property (env, obj, "configuredPeerCount", status.configured_peer_count);
    set_uint32_property (env, obj, "admittedPeerCount", status.admitted_peer_count);
    set_uint32_property (env, obj, "drainingPeerCount", status.draining_peer_count);
    svc_set_bigint (env, obj, "pendingApplicationMessages", status.pending_application_messages);
    svc_set_bigint (env, obj, "pendingInfrastructureMessages",
                    status.pending_infrastructure_messages);
    svc_set_bigint (env, obj, "pendingBytes", status.pending_bytes);
    svc_set_bigint (env, obj, "multicastSubmitted", status.multicast_submitted);
    svc_set_bigint (env, obj, "multicastDroppedTargets", status.multicast_dropped_targets);
    svc_set_int32 (env, obj, "lastError", status.last_error);
    svc_set_bigint (env, obj, "lastChangedMs", status.last_changed_ms);
    return obj;
}

napi_value mesh_node_peers (napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *node = svc_external (env, argv[0]);

    size_t count = 0;
    zlink_mesh_node_peers (node, NULL, &count);
    std::vector<zlink_mesh_peer_entry_t> entries (count);
    for (size_t i = 0; i < count; ++i) {
        memset (&entries[i], 0, sizeof (zlink_mesh_peer_entry_t));
        entries[i].struct_size = sizeof (zlink_mesh_peer_entry_t);
        entries[i].version = ZLINK_MESH_NODE_ABI_VERSION;
    }
    if (count > 0) {
        if (zlink_mesh_node_peers (node, entries.data (), &count) != ZLINK_CONFIG_OK)
            return throw_last_error (env, "meshNodePeers failed");
    }

    napi_value array;
    napi_create_array_with_length (env, count, &array);
    for (size_t i = 0; i < count; ++i) {
        const zlink_mesh_peer_entry_t &entry = entries[i];
        napi_value obj;
        napi_create_object (env, &obj);
        svc_set_bigint (env, obj, "connectionIntentId", entry.connection_intent_id);
        svc_set_int32 (env, obj, "source", static_cast<int32_t> (entry.source));
        svc_set_int32 (env, obj, "state", static_cast<int32_t> (entry.state));
        napi_set_named_property (env, obj, "routingId",
                                 create_routing_id_value (env, entry.routing_id));
        svc_set_bigint (env, obj, "lifecycleGeneration", entry.lifecycle_generation);
        svc_set_bigint (env, obj, "descriptorRevision", entry.descriptor_revision);
        set_string_property (env, obj, "endpoint", entry.endpoint);
        set_uint32_property (env, obj, "channelCount", entry.channel_count);
        svc_set_int32 (env, obj, "lastError", entry.last_error);
        svc_set_bigint (env, obj, "lastChangedMs", entry.last_changed_ms);
        napi_set_element (env, array, static_cast<uint32_t> (i), obj);
    }
    return array;
}

napi_value mesh_node_peer_channels (napi_env env, napi_callback_info info)
{
    napi_value argv[3];
    size_t argc = 3;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *node = svc_external (env, argv[0]);
    zlink_routing_id_t peer_rid;
    if (!parse_routing_id_value (env, argv[1], &peer_rid))
        return NULL;
    uint64_t generation = svc_u64 (env, argv[2]);

    size_t count = 0;
    zlink_mesh_node_peer_channels (node, &peer_rid, generation, NULL, NULL, &count);
    std::vector<char> name_storage (count * (ZLINK_CHANNEL_NAME_MAX + 1));
    std::vector<uint32_t> weights (count);
    typedef char channel_name_row_t[ZLINK_CHANNEL_NAME_MAX + 1];
    if (count > 0) {
        if (zlink_mesh_node_peer_channels (
              node, &peer_rid, generation,
              reinterpret_cast<channel_name_row_t *> (name_storage.data ()), weights.data (),
              &count)
            != ZLINK_CONFIG_OK)
            return throw_last_error (env, "meshNodePeerChannels failed");
    }

    napi_value names_array;
    napi_create_array_with_length (env, count, &names_array);
    napi_value weights_array;
    napi_create_array_with_length (env, count, &weights_array);
    for (size_t i = 0; i < count; ++i) {
        napi_value name_value;
        napi_create_string_utf8 (env, name_storage.data () + i * (ZLINK_CHANNEL_NAME_MAX + 1),
                                 NAPI_AUTO_LENGTH, &name_value);
        napi_set_element (env, names_array, static_cast<uint32_t> (i), name_value);
        napi_value weight_value;
        napi_create_uint32 (env, weights[i], &weight_value);
        napi_set_element (env, weights_array, static_cast<uint32_t> (i), weight_value);
    }
    napi_value obj;
    napi_create_object (env, &obj);
    napi_set_named_property (env, obj, "names", names_array);
    napi_set_named_property (env, obj, "weights", weights_array);
    return obj;
}

// ======================================================================
//  Publisher
// ======================================================================

napi_value mesh_node_publisher_new (napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *node = svc_external (env, argv[0]);
    void *publisher = zlink_mesh_node_publisher_new (node);
    if (!publisher)
        return throw_last_error (env, "meshNodePublisherNew failed");
    napi_value ext;
    napi_create_external (env, publisher, NULL, NULL, &ext);
    return ext;
}

napi_value mesh_node_publisher_publish (napi_env env, napi_callback_info info)
{
    napi_value argv[6];
    size_t argc = 6;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *publisher = svc_external (env, argv[0]);
    std::string channel = get_string (env, argv[1]);
    std::string topic = get_string (env, argv[2]);
    zlink_mesh_metadata_view_t meta_storage;
    const zlink_mesh_metadata_view_t *meta = svc_metadata (env, argv[3], &meta_storage);
    std::vector<zlink_msg_t> parts;
    if (!svc_build_parts (env, argv[4], &parts))
        return NULL;
    zlink_send_flags_t flags = static_cast<zlink_send_flags_t> (svc_int32 (env, argv[5]));
    zlink_mesh_publish_detail_t detail;
    memset (&detail, 0, sizeof (detail));
    detail.struct_size = sizeof (detail);
    detail.version = ZLINK_MESH_NODE_ABI_VERSION;
    zlink_submit_result_t rc =
      zlink_mesh_node_publisher_publish (publisher, channel.c_str (), topic.c_str (), meta,
                                         svc_parts_ptr (parts), parts.size (), &detail, flags);
    close_msg_vector (parts);
    if (rc != ZLINK_SUBMIT_OK)
        return throw_last_error (env, "meshNodePublisherPublish failed");
    return svc_create_publish_detail (env, detail);
}

napi_value mesh_node_publisher_set_option (napi_env env, napi_callback_info info)
{
    napi_value argv[3];
    size_t argc = 3;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *publisher = svc_external (env, argv[0]);
    int32_t option = svc_int32 (env, argv[1]);
    void *data = NULL;
    size_t len = 0;
    napi_get_buffer_info (env, argv[2], &data, &len);
    if (zlink_mesh_node_publisher_set_option (
          publisher, static_cast<zlink_mesh_publish_option_t> (option), data, len)
        != ZLINK_CONFIG_OK)
        return throw_last_error (env, "meshNodePublisherSetOption failed");
    napi_value undef;
    napi_get_undefined (env, &undef);
    return undef;
}

napi_value mesh_node_publisher_get_option (napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *publisher = svc_external (env, argv[0]);
    int32_t option = svc_int32 (env, argv[1]);
    uint8_t buffer[256];
    size_t len = sizeof (buffer);
    if (zlink_mesh_node_publisher_get_option (
          publisher, static_cast<zlink_mesh_publish_option_t> (option), buffer, &len)
        != ZLINK_CONFIG_OK)
        return throw_last_error (env, "meshNodePublisherGetOption failed");
    napi_value out;
    void *copy = NULL;
    napi_create_buffer_copy (env, len, buffer, &copy, &out);
    return out;
}

napi_value mesh_node_publisher_destroy (napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *publisher = svc_external (env, argv[0]);
    zlink_mesh_node_publisher_destroy (&publisher);
    napi_value undef;
    napi_get_undefined (env, &undef);
    return undef;
}

// ======================================================================
//  Pull dispatch: ready handler / batches / claims / receive / reply
// ======================================================================

namespace
{

struct ready_handler_state_t
{
    napi_threadsafe_function tsfn;
};

void ready_handler_call_js (napi_env env, napi_value js_cb, void *context, void *data)
{
    (void) context;
    uint32_t domains = static_cast<uint32_t> (reinterpret_cast<uintptr_t> (data));
    if (!env || !js_cb)
        return;
    napi_value undef;
    napi_get_undefined (env, &undef);
    napi_value arg;
    napi_create_uint32 (env, domains, &arg);
    napi_value ret;
    (void) napi_call_function (env, undef, js_cb, 1, &arg, &ret);
}

zlink_mesh_ready_domain_mask_t ready_handler_bridge (void *mesh_node,
                                                     zlink_mesh_ready_domain_mask_t ready_domains,
                                                     void *userdata)
{
    (void) mesh_node;
    ready_handler_state_t *state = static_cast<ready_handler_state_t *> (userdata);
    if (state && state->tsfn) {
        (void) napi_call_threadsafe_function (
          state->tsfn, reinterpret_cast<void *> (static_cast<uintptr_t> (ready_domains)),
          napi_tsfn_nonblocking);
    }
    return ready_domains;
}

} // namespace

napi_value mesh_node_set_ready_handler (napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *node = svc_external (env, argv[0]);

    ready_handler_state_t *state = new (std::nothrow) ready_handler_state_t ();
    if (!state) {
        napi_throw_error (env, NULL, "meshNodeSetReadyHandler allocation failed");
        return NULL;
    }
    state->tsfn = NULL;
    napi_value resource_name;
    napi_create_string_utf8 (env, "zlink-mesh-ready-handler", NAPI_AUTO_LENGTH, &resource_name);
    if (napi_create_threadsafe_function (env, argv[1], NULL, resource_name, 0, 1, NULL, NULL, state,
                                         ready_handler_call_js, &state->tsfn)
        != napi_ok) {
        delete state;
        napi_throw_error (env, NULL, "meshNodeSetReadyHandler setup failed");
        return NULL;
    }
    (void) napi_unref_threadsafe_function (env, state->tsfn);
    if (zlink_mesh_node_set_ready_handler (node, ready_handler_bridge, state) != ZLINK_HANDLER_OK) {
        napi_release_threadsafe_function (state->tsfn, napi_tsfn_abort);
        return throw_last_error (env, "meshNodeSetReadyHandler failed");
    }
    napi_value undef;
    napi_get_undefined (env, &undef);
    return undef;
}

napi_value mesh_ready_batch_new (napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    uint32_t capacity = svc_uint32 (env, argv[0]);
    void *batch = zlink_mesh_ready_batch_new (capacity);
    if (!batch)
        return throw_last_error (env, "meshReadyBatchNew failed");
    napi_value ext;
    napi_create_external (env, batch, NULL, NULL, &ext);
    return ext;
}

napi_value mesh_ready_batch_reset (napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *batch = svc_external (env, argv[0]);
    if (zlink_mesh_ready_batch_reset (batch) != ZLINK_CONFIG_OK)
        return throw_last_error (env, "meshReadyBatchReset failed");
    napi_value undef;
    napi_get_undefined (env, &undef);
    return undef;
}

napi_value mesh_ready_batch_destroy (napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *batch = svc_external (env, argv[0]);
    zlink_mesh_ready_batch_destroy (&batch);
    napi_value undef;
    napi_get_undefined (env, &undef);
    return undef;
}

napi_value mesh_node_drain_ready (napi_env env, napi_callback_info info)
{
    napi_value argv[4];
    size_t argc = 4;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *node = svc_external (env, argv[0]);
    uint32_t domains = svc_uint32 (env, argv[1]);
    void *batch = svc_external (env, argv[2]);
    zlink_recv_flags_t flags = static_cast<zlink_recv_flags_t> (svc_int32 (env, argv[3]));

    uint32_t has_residue = 0;
    zlink_recv_result_t rc = zlink_mesh_node_drain_ready (node, domains, batch, &has_residue, flags);

    napi_value obj;
    napi_create_object (env, &obj);
    if (rc == ZLINK_RECV_NO_DATA) {
        svc_set_bool (env, obj, "ok", false);
        svc_set_bool (env, obj, "hasResidue", has_residue != 0);
        napi_value empty;
        napi_create_array_with_length (env, 0, &empty);
        napi_set_named_property (env, obj, "records", empty);
        return obj;
    }
    if (rc != ZLINK_RECV_OK)
        return throw_last_error (env, "meshNodeDrainReady failed");

    size_t count = zlink_mesh_ready_batch_count (batch);
    const zlink_mesh_ready_record_t *records = zlink_mesh_ready_batch_data (batch);
    napi_value array;
    napi_create_array_with_length (env, count, &array);
    for (size_t i = 0; i < count; ++i) {
        const zlink_mesh_ready_record_t &record = records[i];
        napi_value record_obj;
        napi_create_object (env, &record_obj);
        svc_set_int32 (env, record_obj, "ownerKind", static_cast<int32_t> (record.owner_kind));
        set_uint32_property (env, record_obj, "domain", record.domain);
        napi_set_named_property (env, record_obj, "spotRid",
                                 create_routing_id_value (env, record.spot_rid));
        napi_set_named_property (env, record_obj, "actor",
                                 svc_create_actor_ref (env, record.actor));
        napi_set_element (env, array, static_cast<uint32_t> (i), record_obj);
    }
    svc_set_bool (env, obj, "ok", true);
    svc_set_bool (env, obj, "hasResidue", has_residue != 0);
    napi_set_named_property (env, obj, "records", array);
    return obj;
}

napi_value mesh_ready_batch_take_claim (napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *batch = svc_external (env, argv[0]);
    uint32_t index = svc_uint32 (env, argv[1]);
    zlink_mesh_claim_t *claim = new (std::nothrow) zlink_mesh_claim_t ();
    if (!claim) {
        napi_throw_error (env, NULL, "meshReadyBatchTakeClaim allocation failed");
        return NULL;
    }
    memset (claim, 0, sizeof (*claim));
    if (zlink_mesh_ready_batch_take_claim (batch, index, claim) != ZLINK_CONFIG_OK) {
        delete claim;
        return throw_last_error (env, "meshReadyBatchTakeClaim failed");
    }
    napi_value ext;
    napi_create_external (env, claim, NULL, NULL, &ext);
    return ext;
}

napi_value mesh_claim_release (napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    zlink_mesh_claim_t *claim = static_cast<zlink_mesh_claim_t *> (svc_external (env, argv[0]));
    if (claim) {
        zlink_mesh_claim_release (claim);
        delete claim;
    }
    napi_value undef;
    napi_get_undefined (env, &undef);
    return undef;
}

napi_value mesh_receive_batch_new (napi_env env, napi_callback_info info)
{
    napi_value argv[3];
    size_t argc = 3;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    size_t message_cap = static_cast<size_t> (svc_uint32 (env, argv[0]));
    size_t part_cap = static_cast<size_t> (svc_uint32 (env, argv[1]));
    size_t byte_cap = static_cast<size_t> (svc_u64 (env, argv[2]));
    void *batch = zlink_mesh_receive_batch_new (message_cap, part_cap, byte_cap);
    if (!batch)
        return throw_last_error (env, "meshReceiveBatchNew failed");
    napi_value ext;
    napi_create_external (env, batch, NULL, NULL, &ext);
    return ext;
}

napi_value mesh_receive_batch_reset (napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *batch = svc_external (env, argv[0]);
    if (zlink_mesh_receive_batch_reset (batch) != ZLINK_CONFIG_OK)
        return throw_last_error (env, "meshReceiveBatchReset failed");
    napi_value undef;
    napi_get_undefined (env, &undef);
    return undef;
}

napi_value mesh_receive_batch_destroy (napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *batch = svc_external (env, argv[0]);
    zlink_mesh_receive_batch_destroy (&batch);
    napi_value undef;
    napi_get_undefined (env, &undef);
    return undef;
}

napi_value mesh_claim_recv_batch (napi_env env, napi_callback_info info)
{
    napi_value argv[3];
    size_t argc = 3;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    zlink_mesh_claim_t *claim = static_cast<zlink_mesh_claim_t *> (svc_external (env, argv[0]));
    void *batch = svc_external (env, argv[1]);
    zlink_recv_flags_t flags = static_cast<zlink_recv_flags_t> (svc_int32 (env, argv[2]));

    zlink_mesh_receive_requirements_t required;
    memset (&required, 0, sizeof (required));
    required.struct_size = sizeof (required);
    required.version = ZLINK_MESH_DISPATCH_ABI_VERSION;
    zlink_recv_result_t rc = zlink_mesh_claim_recv_batch (claim, batch, &required, flags);

    napi_value obj;
    napi_create_object (env, &obj);
    if (rc == ZLINK_RECV_NO_DATA) {
        svc_set_bool (env, obj, "ok", false);
        napi_value null_required;
        napi_get_null (env, &null_required);
        napi_set_named_property (env, obj, "required", null_required);
        napi_value empty;
        napi_create_array_with_length (env, 0, &empty);
        napi_set_named_property (env, obj, "records", empty);
        return obj;
    }
    if (rc == ZLINK_RECV_BUFFER_TOO_SMALL) {
        svc_set_bool (env, obj, "ok", false);
        napi_value required_obj;
        napi_create_object (env, &required_obj);
        svc_set_bigint (env, required_obj, "messageCount", required.message_count);
        svc_set_bigint (env, required_obj, "partCount", required.part_count);
        svc_set_bigint (env, required_obj, "byteCount", required.byte_count);
        napi_set_named_property (env, obj, "required", required_obj);
        napi_value empty;
        napi_create_array_with_length (env, 0, &empty);
        napi_set_named_property (env, obj, "records", empty);
        return obj;
    }
    if (rc != ZLINK_RECV_OK)
        return throw_last_error (env, "meshClaimRecvBatch failed");

    size_t count = zlink_mesh_receive_batch_count (batch);
    const zlink_mesh_receive_record_t *records = zlink_mesh_receive_batch_data (batch);
    napi_value array;
    napi_create_array_with_length (env, count, &array);
    for (size_t i = 0; i < count; ++i) {
        const zlink_mesh_receive_record_t &record = records[i];
        napi_value record_obj;
        napi_create_object (env, &record_obj);
        svc_set_int32 (env, record_obj, "kind", static_cast<int32_t> (record.kind));
        set_uint32_property (env, record_obj, "domain", record.domain);
        napi_set_named_property (env, record_obj, "sourceNodeRid",
                                 create_routing_id_value (env, record.source_node_rid));
        napi_set_named_property (env, record_obj, "sourceSpotRid",
                                 create_routing_id_value (env, record.source_spot_rid));
        napi_set_named_property (env, record_obj, "sourceActor",
                                 svc_create_actor_ref (env, record.source_actor));
        napi_set_named_property (env, record_obj, "operationId",
                                 svc_create_operation_id (env, record.operation_id));
        svc_set_int32 (env, record_obj, "operationKind",
                       static_cast<int32_t> (record.operation_kind));
        napi_set_named_property (env, record_obj, "replyToken",
                                 svc_create_reply_token (env, record.reply_token));
        napi_set_named_property (env, record_obj, "channelName",
                                 svc_string_or_null (env, record.channel_name,
                                                     record.channel_name_size));
        napi_set_named_property (env, record_obj, "topic",
                                 svc_string_or_null (env, record.topic, record.topic_size));
        napi_set_named_property (env, record_obj, "applicationMetadata",
                                 svc_buffer_or_null (env, record.application_metadata,
                                                     record.application_metadata_size));
        svc_set_int32 (env, record_obj, "terminalResult", record.terminal_result);
        svc_set_int32 (env, record_obj, "failureErrno", record.failure_errno);

        napi_value parts_array;
        napi_create_array_with_length (env, record.part_count, &parts_array);
        if (record.part_count > 0) {
            std::vector<zlink_msg_t> parts (record.part_count);
            size_t part_count = record.part_count;
            if (zlink_mesh_receive_batch_retain_message (batch, i, parts.data (), &part_count)
                == ZLINK_CONFIG_OK) {
                for (size_t p = 0; p < part_count; ++p) {
                    napi_value part_buf = create_message_data_buffer (env, &parts[p]);
                    napi_set_element (env, parts_array, static_cast<uint32_t> (p), part_buf);
                }
            }
        }
        napi_set_named_property (env, record_obj, "parts", parts_array);
        napi_set_element (env, array, static_cast<uint32_t> (i), record_obj);
    }
    svc_set_bool (env, obj, "ok", true);
    napi_value null_required;
    napi_get_null (env, &null_required);
    napi_set_named_property (env, obj, "required", null_required);
    napi_set_named_property (env, obj, "records", array);
    return obj;
}

napi_value mesh_reply (napi_env env, napi_callback_info info)
{
    napi_value argv[3];
    size_t argc = 3;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    zlink_mesh_reply_token_t token;
    if (!svc_parse_reply_token (env, argv[0], &token))
        return NULL;
    std::vector<zlink_msg_t> parts;
    if (!svc_build_parts (env, argv[1], &parts))
        return NULL;
    zlink_send_flags_t flags = static_cast<zlink_send_flags_t> (svc_int32 (env, argv[2]));
    zlink_submit_result_t rc =
      zlink_mesh_reply (&token, svc_parts_ptr (parts), parts.size (), flags);
    close_msg_vector (parts);
    napi_value out;
    napi_create_int32 (env, static_cast<int32_t> (rc), &out);
    return out;
}

// ======================================================================
//  Spot
// ======================================================================

napi_value spot_new (napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *node = svc_external (env, argv[0]);
    void *spot = zlink_spot_new (node);
    if (!spot)
        return throw_last_error (env, "spotNew failed");
    napi_value ext;
    napi_create_external (env, spot, NULL, NULL, &ext);
    return ext;
}

napi_value mesh_node_entry_spot (napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *node = svc_external (env, argv[0]);
    void *spot = NULL;
    if (zlink_mesh_node_entry_spot (node, &spot) != ZLINK_CONFIG_OK)
        return throw_last_error (env, "meshNodeEntrySpot failed");
    napi_value ext;
    napi_create_external (env, spot, NULL, NULL, &ext);
    return ext;
}

napi_value mesh_node_spot_lookup (napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *node = svc_external (env, argv[0]);
    zlink_routing_id_t spot_rid;
    if (!parse_routing_id_value (env, argv[1], &spot_rid))
        return NULL;
    void *spot = NULL;
    zlink_config_result_t rc = zlink_mesh_node_spot_lookup (node, &spot_rid, &spot);
    if (rc == ZLINK_CONFIG_NOT_FOUND) {
        napi_value null_value;
        napi_get_null (env, &null_value);
        return null_value;
    }
    if (rc != ZLINK_CONFIG_OK)
        return throw_last_error (env, "meshNodeSpotLookup failed");
    napi_value ext;
    napi_create_external (env, spot, NULL, NULL, &ext);
    return ext;
}

napi_value mesh_node_spot_get_or_new (napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *node = svc_external (env, argv[0]);
    zlink_routing_id_t spot_rid;
    if (!parse_routing_id_value (env, argv[1], &spot_rid))
        return NULL;
    void *spot = NULL;
    uint32_t created = 0;
    if (zlink_mesh_node_spot_get_or_new (node, &spot_rid, &spot, &created) != ZLINK_CONFIG_OK)
        return throw_last_error (env, "meshNodeSpotGetOrNew failed");
    napi_value ext;
    napi_create_external (env, spot, NULL, NULL, &ext);
    napi_value obj;
    napi_create_object (env, &obj);
    napi_set_named_property (env, obj, "spot", ext);
    svc_set_bool (env, obj, "created", created != 0);
    return obj;
}

napi_value spot_destroy (napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *spot = svc_external (env, argv[0]);
    zlink_spot_destroy (&spot);
    napi_value undef;
    napi_get_undefined (env, &undef);
    return undef;
}

napi_value spot_status (napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *spot = svc_external (env, argv[0]);
    zlink_spot_status_t status;
    memset (&status, 0, sizeof (status));
    status.struct_size = sizeof (status);
    status.version = ZLINK_SPOT_ABI_VERSION;
    if (zlink_spot_status (spot, &status) != ZLINK_CONFIG_OK)
        return throw_last_error (env, "spotStatus failed");
    napi_value obj;
    napi_create_object (env, &obj);
    napi_set_named_property (env, obj, "spotRid", create_routing_id_value (env, status.spot_rid));
    svc_set_int32 (env, obj, "spotKind", static_cast<int32_t> (status.spot_kind));
    svc_set_bigint (env, obj, "lifecycleGeneration", status.lifecycle_generation);
    svc_set_bigint (env, obj, "pendingApplicationMessages", status.pending_application_messages);
    svc_set_bigint (env, obj, "pendingInfrastructureMessages",
                    status.pending_infrastructure_messages);
    svc_set_bigint (env, obj, "pendingBytes", status.pending_bytes);
    set_uint32_property (env, obj, "activeActorCount", status.active_actor_count);
    set_uint32_property (env, obj, "draining", status.draining);
    svc_set_int32 (env, obj, "lastError", status.last_error);
    svc_set_bigint (env, obj, "lastChangedMs", status.last_changed_ms);
    return obj;
}

napi_value spot_send_to_channel (napi_env env, napi_callback_info info)
{
    napi_value argv[5];
    size_t argc = 5;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *spot = svc_external (env, argv[0]);
    std::string channel = get_string (env, argv[1]);
    zlink_mesh_metadata_view_t meta_storage;
    const zlink_mesh_metadata_view_t *meta = svc_metadata (env, argv[2], &meta_storage);
    std::vector<zlink_msg_t> parts;
    if (!svc_build_parts (env, argv[3], &parts))
        return NULL;
    zlink_send_flags_t flags = static_cast<zlink_send_flags_t> (svc_int32 (env, argv[4]));
    zlink_submit_result_t rc =
      zlink_spot_send_to_channel (spot, channel.c_str (), meta, svc_parts_ptr (parts),
                                  parts.size (), flags);
    close_msg_vector (parts);
    napi_value out;
    napi_create_int32 (env, static_cast<int32_t> (rc), &out);
    return out;
}

napi_value spot_request_to_channel (napi_env env, napi_callback_info info)
{
    napi_value argv[6];
    size_t argc = 6;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *spot = svc_external (env, argv[0]);
    std::string channel = get_string (env, argv[1]);
    zlink_mesh_metadata_view_t meta_storage;
    const zlink_mesh_metadata_view_t *meta = svc_metadata (env, argv[2], &meta_storage);
    std::vector<zlink_msg_t> parts;
    if (!svc_build_parts (env, argv[3], &parts))
        return NULL;
    zlink_send_flags_t flags = static_cast<zlink_send_flags_t> (svc_int32 (env, argv[4]));
    uint32_t timeout_ms = svc_uint32 (env, argv[5]);
    zlink_mesh_operation_id_t op;
    memset (&op, 0, sizeof (op));
    zlink_submit_result_t rc =
      zlink_spot_request_to_channel (spot, channel.c_str (), meta, svc_parts_ptr (parts),
                                     parts.size (), &op, flags, timeout_ms);
    close_msg_vector (parts);
    if (rc != ZLINK_SUBMIT_OK)
        return throw_last_error (env, "spotRequestToChannel failed");
    return svc_create_operation_id (env, op);
}

napi_value spot_send_to_spot (napi_env env, napi_callback_info info)
{
    napi_value argv[7];
    size_t argc = 7;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *spot = svc_external (env, argv[0]);
    zlink_routing_id_t target_node_rid;
    zlink_routing_id_t target_spot_rid;
    if (!parse_routing_id_value (env, argv[1], &target_node_rid))
        return NULL;
    if (!parse_routing_id_value (env, argv[2], &target_spot_rid))
        return NULL;
    uint64_t generation = svc_u64 (env, argv[3]);
    zlink_mesh_metadata_view_t meta_storage;
    const zlink_mesh_metadata_view_t *meta = svc_metadata (env, argv[4], &meta_storage);
    std::vector<zlink_msg_t> parts;
    if (!svc_build_parts (env, argv[5], &parts))
        return NULL;
    zlink_send_flags_t flags = static_cast<zlink_send_flags_t> (svc_int32 (env, argv[6]));
    zlink_submit_result_t rc =
      zlink_spot_send_to_spot (spot, &target_node_rid, &target_spot_rid, generation, meta,
                               svc_parts_ptr (parts), parts.size (), flags);
    close_msg_vector (parts);
    napi_value out;
    napi_create_int32 (env, static_cast<int32_t> (rc), &out);
    return out;
}

napi_value spot_request_to_spot (napi_env env, napi_callback_info info)
{
    napi_value argv[8];
    size_t argc = 8;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *spot = svc_external (env, argv[0]);
    zlink_routing_id_t target_node_rid;
    zlink_routing_id_t target_spot_rid;
    if (!parse_routing_id_value (env, argv[1], &target_node_rid))
        return NULL;
    if (!parse_routing_id_value (env, argv[2], &target_spot_rid))
        return NULL;
    uint64_t generation = svc_u64 (env, argv[3]);
    zlink_mesh_metadata_view_t meta_storage;
    const zlink_mesh_metadata_view_t *meta = svc_metadata (env, argv[4], &meta_storage);
    std::vector<zlink_msg_t> parts;
    if (!svc_build_parts (env, argv[5], &parts))
        return NULL;
    zlink_send_flags_t flags = static_cast<zlink_send_flags_t> (svc_int32 (env, argv[6]));
    uint32_t timeout_ms = svc_uint32 (env, argv[7]);
    zlink_mesh_operation_id_t op;
    memset (&op, 0, sizeof (op));
    zlink_submit_result_t rc =
      zlink_spot_request_to_spot (spot, &target_node_rid, &target_spot_rid, generation, meta,
                                  svc_parts_ptr (parts), parts.size (), &op, flags, timeout_ms);
    close_msg_vector (parts);
    if (rc != ZLINK_SUBMIT_OK)
        return throw_last_error (env, "spotRequestToSpot failed");
    return svc_create_operation_id (env, op);
}

napi_value spot_publish (napi_env env, napi_callback_info info)
{
    napi_value argv[6];
    size_t argc = 6;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *spot = svc_external (env, argv[0]);
    std::string channel = get_string (env, argv[1]);
    std::string topic = get_string (env, argv[2]);
    zlink_mesh_metadata_view_t meta_storage;
    const zlink_mesh_metadata_view_t *meta = svc_metadata (env, argv[3], &meta_storage);
    std::vector<zlink_msg_t> parts;
    if (!svc_build_parts (env, argv[4], &parts))
        return NULL;
    zlink_send_flags_t flags = static_cast<zlink_send_flags_t> (svc_int32 (env, argv[5]));
    zlink_mesh_publish_detail_t detail;
    memset (&detail, 0, sizeof (detail));
    detail.struct_size = sizeof (detail);
    detail.version = ZLINK_MESH_NODE_ABI_VERSION;
    zlink_submit_result_t rc =
      zlink_spot_publish (spot, channel.c_str (), topic.c_str (), meta, svc_parts_ptr (parts),
                          parts.size (), &detail, flags);
    close_msg_vector (parts);
    if (rc != ZLINK_SUBMIT_OK)
        return throw_last_error (env, "spotPublish failed");
    return svc_create_publish_detail (env, detail);
}

napi_value spot_set_publish_option (napi_env env, napi_callback_info info)
{
    napi_value argv[3];
    size_t argc = 3;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *spot = svc_external (env, argv[0]);
    int32_t option = svc_int32 (env, argv[1]);
    void *data = NULL;
    size_t len = 0;
    napi_get_buffer_info (env, argv[2], &data, &len);
    if (zlink_spot_set_publish_option (spot, static_cast<zlink_mesh_publish_option_t> (option), data,
                                       len)
        != ZLINK_CONFIG_OK)
        return throw_last_error (env, "spotSetPublishOption failed");
    napi_value undef;
    napi_get_undefined (env, &undef);
    return undef;
}

napi_value spot_get_publish_option (napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *spot = svc_external (env, argv[0]);
    int32_t option = svc_int32 (env, argv[1]);
    uint8_t buffer[256];
    size_t len = sizeof (buffer);
    if (zlink_spot_get_publish_option (spot, static_cast<zlink_mesh_publish_option_t> (option),
                                       buffer, &len)
        != ZLINK_CONFIG_OK)
        return throw_last_error (env, "spotGetPublishOption failed");
    napi_value out;
    void *copy = NULL;
    napi_create_buffer_copy (env, len, buffer, &copy, &out);
    return out;
}

napi_value spot_set_subscription (napi_env env, napi_callback_info info)
{
    napi_value argv[4];
    size_t argc = 4;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *spot = svc_external (env, argv[0]);
    std::string channel = get_string (env, argv[1]);
    std::string topic_filter = get_string (env, argv[2]);
    int32_t kind = svc_int32 (env, argv[3]);
    if (zlink_spot_set_subscription (spot, channel.c_str (), topic_filter.c_str (),
                                     static_cast<zlink_spot_subscription_kind_t> (kind))
        != ZLINK_CONFIG_OK)
        return throw_last_error (env, "spotSetSubscription failed");
    napi_value undef;
    napi_get_undefined (env, &undef);
    return undef;
}

napi_value spot_unset_subscription (napi_env env, napi_callback_info info)
{
    napi_value argv[4];
    size_t argc = 4;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *spot = svc_external (env, argv[0]);
    std::string channel = get_string (env, argv[1]);
    std::string topic_filter = get_string (env, argv[2]);
    int32_t kind = svc_int32 (env, argv[3]);
    if (zlink_spot_unset_subscription (spot, channel.c_str (), topic_filter.c_str (),
                                       static_cast<zlink_spot_subscription_kind_t> (kind))
        != ZLINK_CONFIG_OK)
        return throw_last_error (env, "spotUnsetSubscription failed");
    napi_value undef;
    napi_get_undefined (env, &undef);
    return undef;
}

// ======================================================================
//  Actor
// ======================================================================

napi_value mesh_node_actor_new (napi_env env, napi_callback_info info)
{
    napi_value argv[5];
    size_t argc = 5;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *node = svc_external (env, argv[0]);
    std::string actor_id = get_string (env, argv[1]);
    std::vector<zlink_msg_t> parts;
    if (!svc_build_parts (env, argv[2], &parts))
        return NULL;
    zlink_send_flags_t flags = static_cast<zlink_send_flags_t> (svc_int32 (env, argv[3]));
    uint32_t timeout_ms = svc_uint32 (env, argv[4]);
    zlink_actor_ref_t actor;
    memset (&actor, 0, sizeof (actor));
    zlink_request_result_t rc =
      zlink_mesh_node_actor_new (node, actor_id.c_str (), svc_parts_ptr (parts), parts.size (),
                                 &actor, flags, timeout_ms);
    close_msg_vector (parts);
    if (rc != ZLINK_REQUEST_OK)
        return throw_last_error (env, "meshNodeActorNew failed");
    return svc_create_actor_ref (env, actor);
}

napi_value mesh_node_actor_lookup (napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *node = svc_external (env, argv[0]);
    std::string actor_id = get_string (env, argv[1]);
    zlink_actor_location_t location;
    memset (&location, 0, sizeof (location));
    location.struct_size = sizeof (location);
    location.version = ZLINK_ACTOR_ABI_VERSION;
    if (zlink_mesh_node_actor_lookup (node, actor_id.c_str (), &location) != ZLINK_CONFIG_OK)
        return throw_last_error (env, "meshNodeActorLookup failed");
    napi_value obj;
    napi_create_object (env, &obj);
    napi_set_named_property (env, obj, "actor", svc_create_actor_ref (env, location.actor));
    napi_set_named_property (env, obj, "spotRid",
                             create_routing_id_value (env, location.spot_rid));
    svc_set_bigint (env, obj, "spotGeneration", location.spot_generation);
    svc_set_bigint (env, obj, "membershipEpoch", location.membership_epoch);
    return obj;
}

napi_value mesh_node_actor_lookup_remote (napi_env env, napi_callback_info info)
{
    napi_value argv[4];
    size_t argc = 4;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *node = svc_external (env, argv[0]);
    zlink_routing_id_t target_node_rid;
    if (!parse_routing_id_value (env, argv[1], &target_node_rid))
        return NULL;
    std::string actor_id = get_string (env, argv[2]);
    uint32_t timeout_ms = svc_uint32 (env, argv[3]);
    zlink_mesh_operation_id_t op;
    memset (&op, 0, sizeof (op));
    if (zlink_mesh_node_actor_lookup_remote (node, &target_node_rid, actor_id.c_str (), &op,
                                             timeout_ms)
        != ZLINK_SUBMIT_OK)
        return throw_last_error (env, "meshNodeActorLookupRemote failed");
    return svc_create_operation_id (env, op);
}

napi_value mesh_node_actor_destroy (napi_env env, napi_callback_info info)
{
    napi_value argv[3];
    size_t argc = 3;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *node = svc_external (env, argv[0]);
    zlink_actor_ref_t actor;
    if (!svc_parse_actor_ref (env, argv[1], &actor))
        return NULL;
    uint32_t timeout_ms = svc_uint32 (env, argv[2]);
    zlink_mesh_operation_id_t op;
    memset (&op, 0, sizeof (op));
    if (zlink_mesh_node_actor_destroy (node, &actor, &op, timeout_ms) != ZLINK_SUBMIT_OK)
        return throw_last_error (env, "meshNodeActorDestroy failed");
    return svc_create_operation_id (env, op);
}

napi_value mesh_node_actor_join_spot (napi_env env, napi_callback_info info)
{
    napi_value argv[7];
    size_t argc = 7;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *node = svc_external (env, argv[0]);
    zlink_actor_ref_t actor;
    if (!svc_parse_actor_ref (env, argv[1], &actor))
        return NULL;
    zlink_routing_id_t target_node_rid;
    zlink_routing_id_t target_spot_rid;
    if (!parse_routing_id_value (env, argv[2], &target_node_rid))
        return NULL;
    if (!parse_routing_id_value (env, argv[3], &target_spot_rid))
        return NULL;
    uint64_t generation = svc_u64 (env, argv[4]);
    std::vector<zlink_msg_t> parts;
    if (!svc_build_parts (env, argv[5], &parts))
        return NULL;
    uint32_t timeout_ms = svc_uint32 (env, argv[6]);
    zlink_mesh_operation_id_t op;
    memset (&op, 0, sizeof (op));
    zlink_submit_result_t rc = zlink_mesh_node_actor_join_spot (
      node, &actor, &target_node_rid, &target_spot_rid, generation, svc_parts_ptr (parts),
      parts.size (), &op, timeout_ms);
    close_msg_vector (parts);
    if (rc != ZLINK_SUBMIT_OK)
        return throw_last_error (env, "meshNodeActorJoinSpot failed");
    return svc_create_operation_id (env, op);
}

napi_value mesh_node_actor_join_entry_spot (napi_env env, napi_callback_info info)
{
    napi_value argv[5];
    size_t argc = 5;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *node = svc_external (env, argv[0]);
    zlink_actor_ref_t actor;
    if (!svc_parse_actor_ref (env, argv[1], &actor))
        return NULL;
    zlink_routing_id_t target_node_rid;
    if (!parse_routing_id_value (env, argv[2], &target_node_rid))
        return NULL;
    std::vector<zlink_msg_t> parts;
    if (!svc_build_parts (env, argv[3], &parts))
        return NULL;
    uint32_t timeout_ms = svc_uint32 (env, argv[4]);
    zlink_mesh_operation_id_t op;
    memset (&op, 0, sizeof (op));
    zlink_submit_result_t rc = zlink_mesh_node_actor_join_entry_spot (
      node, &actor, &target_node_rid, svc_parts_ptr (parts), parts.size (), &op, timeout_ms);
    close_msg_vector (parts);
    if (rc != ZLINK_SUBMIT_OK)
        return throw_last_error (env, "meshNodeActorJoinEntrySpot failed");
    return svc_create_operation_id (env, op);
}

napi_value mesh_node_actor_leave_spot (napi_env env, napi_callback_info info)
{
    napi_value argv[4];
    size_t argc = 4;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *node = svc_external (env, argv[0]);
    zlink_actor_ref_t actor;
    if (!svc_parse_actor_ref (env, argv[1], &actor))
        return NULL;
    uint64_t expected_epoch = svc_u64 (env, argv[2]);
    uint32_t timeout_ms = svc_uint32 (env, argv[3]);
    zlink_mesh_operation_id_t op;
    memset (&op, 0, sizeof (op));
    if (zlink_mesh_node_actor_leave_spot (node, &actor, expected_epoch, &op, timeout_ms)
        != ZLINK_SUBMIT_OK)
        return throw_last_error (env, "meshNodeActorLeaveSpot failed");
    return svc_create_operation_id (env, op);
}

napi_value actor_join_reply (napi_env env, napi_callback_info info)
{
    napi_value argv[4];
    size_t argc = 4;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    zlink_mesh_reply_token_t token;
    if (!svc_parse_reply_token (env, argv[0], &token))
        return NULL;
    int32_t join_result = svc_int32 (env, argv[1]);
    std::vector<zlink_msg_t> parts;
    if (!svc_build_parts (env, argv[2], &parts))
        return NULL;
    zlink_send_flags_t flags = static_cast<zlink_send_flags_t> (svc_int32 (env, argv[3]));
    zlink_submit_result_t rc =
      zlink_actor_join_reply (&token, static_cast<zlink_actor_join_result_t> (join_result),
                              svc_parts_ptr (parts), parts.size (), flags);
    close_msg_vector (parts);
    napi_value out;
    napi_create_int32 (env, static_cast<int32_t> (rc), &out);
    return out;
}

napi_value mesh_node_send_to_actor (napi_env env, napi_callback_info info)
{
    napi_value argv[5];
    size_t argc = 5;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *node = svc_external (env, argv[0]);
    zlink_actor_ref_t actor;
    if (!svc_parse_actor_ref (env, argv[1], &actor))
        return NULL;
    zlink_mesh_metadata_view_t meta_storage;
    const zlink_mesh_metadata_view_t *meta = svc_metadata (env, argv[2], &meta_storage);
    std::vector<zlink_msg_t> parts;
    if (!svc_build_parts (env, argv[3], &parts))
        return NULL;
    zlink_send_flags_t flags = static_cast<zlink_send_flags_t> (svc_int32 (env, argv[4]));
    zlink_submit_result_t rc =
      zlink_mesh_node_send_to_actor (node, &actor, meta, svc_parts_ptr (parts), parts.size (),
                                     flags);
    close_msg_vector (parts);
    napi_value out;
    napi_create_int32 (env, static_cast<int32_t> (rc), &out);
    return out;
}

napi_value mesh_node_request_to_actor (napi_env env, napi_callback_info info)
{
    napi_value argv[6];
    size_t argc = 6;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *node = svc_external (env, argv[0]);
    zlink_actor_ref_t actor;
    if (!svc_parse_actor_ref (env, argv[1], &actor))
        return NULL;
    zlink_mesh_metadata_view_t meta_storage;
    const zlink_mesh_metadata_view_t *meta = svc_metadata (env, argv[2], &meta_storage);
    std::vector<zlink_msg_t> parts;
    if (!svc_build_parts (env, argv[3], &parts))
        return NULL;
    zlink_send_flags_t flags = static_cast<zlink_send_flags_t> (svc_int32 (env, argv[4]));
    uint32_t timeout_ms = svc_uint32 (env, argv[5]);
    zlink_mesh_operation_id_t op;
    memset (&op, 0, sizeof (op));
    zlink_submit_result_t rc =
      zlink_mesh_node_request_to_actor (node, &actor, meta, svc_parts_ptr (parts), parts.size (),
                                        &op, flags, timeout_ms);
    close_msg_vector (parts);
    if (rc != ZLINK_SUBMIT_OK)
        return throw_last_error (env, "meshNodeRequestToActor failed");
    return svc_create_operation_id (env, op);
}

napi_value actor_send_to_actor (napi_env env, napi_callback_info info)
{
    napi_value argv[6];
    size_t argc = 6;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *node = svc_external (env, argv[0]);
    zlink_actor_ref_t source_actor;
    zlink_actor_ref_t target_actor;
    if (!svc_parse_actor_ref (env, argv[1], &source_actor))
        return NULL;
    if (!svc_parse_actor_ref (env, argv[2], &target_actor))
        return NULL;
    zlink_mesh_metadata_view_t meta_storage;
    const zlink_mesh_metadata_view_t *meta = svc_metadata (env, argv[3], &meta_storage);
    std::vector<zlink_msg_t> parts;
    if (!svc_build_parts (env, argv[4], &parts))
        return NULL;
    zlink_send_flags_t flags = static_cast<zlink_send_flags_t> (svc_int32 (env, argv[5]));
    zlink_submit_result_t rc =
      zlink_actor_send_to_actor (node, &source_actor, &target_actor, meta, svc_parts_ptr (parts),
                                 parts.size (), flags);
    close_msg_vector (parts);
    napi_value out;
    napi_create_int32 (env, static_cast<int32_t> (rc), &out);
    return out;
}

napi_value actor_request_to_actor (napi_env env, napi_callback_info info)
{
    napi_value argv[7];
    size_t argc = 7;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *node = svc_external (env, argv[0]);
    zlink_actor_ref_t source_actor;
    zlink_actor_ref_t target_actor;
    if (!svc_parse_actor_ref (env, argv[1], &source_actor))
        return NULL;
    if (!svc_parse_actor_ref (env, argv[2], &target_actor))
        return NULL;
    zlink_mesh_metadata_view_t meta_storage;
    const zlink_mesh_metadata_view_t *meta = svc_metadata (env, argv[3], &meta_storage);
    std::vector<zlink_msg_t> parts;
    if (!svc_build_parts (env, argv[4], &parts))
        return NULL;
    zlink_send_flags_t flags = static_cast<zlink_send_flags_t> (svc_int32 (env, argv[5]));
    uint32_t timeout_ms = svc_uint32 (env, argv[6]);
    zlink_mesh_operation_id_t op;
    memset (&op, 0, sizeof (op));
    zlink_submit_result_t rc =
      zlink_actor_request_to_actor (node, &source_actor, &target_actor, meta, svc_parts_ptr (parts),
                                    parts.size (), &op, flags, timeout_ms);
    close_msg_vector (parts);
    if (rc != ZLINK_SUBMIT_OK)
        return throw_last_error (env, "actorRequestToActor failed");
    return svc_create_operation_id (env, op);
}

// ======================================================================
//  Stream session service
// ======================================================================

napi_value stream_session_service_new (napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *node = svc_external (env, argv[0]);
    void *stream = svc_external (env, argv[1]);
    void *service = zlink_stream_session_service_new (node, stream);
    if (!service)
        return throw_last_error (env, "streamSessionServiceNew failed");
    napi_value ext;
    napi_create_external (env, service, NULL, NULL, &ext);
    return ext;
}

napi_value stream_session_service_start (napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *service = svc_external (env, argv[0]);
    if (zlink_stream_session_service_start (service) != ZLINK_CONFIG_OK)
        return throw_last_error (env, "streamSessionServiceStart failed");
    napi_value undef;
    napi_get_undefined (env, &undef);
    return undef;
}

napi_value stream_session_service_shutdown (napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *service = svc_external (env, argv[0]);
    uint32_t timeout_ms = svc_uint32 (env, argv[1]);
    zlink_request_result_t rc = zlink_stream_session_service_shutdown (service, timeout_ms);
    napi_value out;
    napi_create_int32 (env, static_cast<int32_t> (rc), &out);
    return out;
}

napi_value stream_session_service_destroy (napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *service = svc_external (env, argv[0]);
    zlink_stream_session_service_destroy (&service);
    napi_value undef;
    napi_get_undefined (env, &undef);
    return undef;
}

napi_value stream_session_service_status (napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *service = svc_external (env, argv[0]);
    zlink_stream_session_status_t status;
    memset (&status, 0, sizeof (status));
    status.struct_size = sizeof (status);
    status.version = ZLINK_STREAM_SESSION_ABI_VERSION;
    if (zlink_stream_session_service_status (service, &status) != ZLINK_CONFIG_OK)
        return throw_last_error (env, "streamSessionServiceStatus failed");
    napi_value obj;
    napi_create_object (env, &obj);
    svc_set_int32 (env, obj, "state", static_cast<int32_t> (status.state));
    svc_set_bigint (env, obj, "lifecycleGeneration", status.lifecycle_generation);
    svc_set_bigint (env, obj, "sessionCount", status.session_count);
    svc_set_bigint (env, obj, "bindingCount", status.binding_count);
    svc_set_bigint (env, obj, "pendingMessageCount", status.pending_message_count);
    svc_set_bigint (env, obj, "pendingByteCount", status.pending_byte_count);
    svc_set_int32 (env, obj, "lastError", status.last_error);
    return obj;
}

napi_value stream_session_bind_actor (napi_env env, napi_callback_info info)
{
    napi_value argv[4];
    size_t argc = 4;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *service = svc_external (env, argv[0]);
    zlink_routing_id_t session_rid;
    if (!parse_routing_id_value (env, argv[1], &session_rid))
        return NULL;
    zlink_actor_ref_t actor;
    if (!svc_parse_actor_ref (env, argv[2], &actor))
        return NULL;
    uint32_t timeout_ms = svc_uint32 (env, argv[3]);
    zlink_mesh_operation_id_t op;
    memset (&op, 0, sizeof (op));
    if (zlink_stream_session_bind_actor (service, &session_rid, &actor, &op, timeout_ms)
        != ZLINK_SUBMIT_OK)
        return throw_last_error (env, "streamSessionBindActor failed");
    return svc_create_operation_id (env, op);
}

napi_value stream_session_unbind_actor (napi_env env, napi_callback_info info)
{
    napi_value argv[5];
    size_t argc = 5;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *service = svc_external (env, argv[0]);
    zlink_routing_id_t session_rid;
    if (!parse_routing_id_value (env, argv[1], &session_rid))
        return NULL;
    zlink_actor_ref_t actor;
    if (!svc_parse_actor_ref (env, argv[2], &actor))
        return NULL;
    uint64_t expected_generation = svc_u64 (env, argv[3]);
    uint32_t timeout_ms = svc_uint32 (env, argv[4]);
    zlink_mesh_operation_id_t op;
    memset (&op, 0, sizeof (op));
    if (zlink_stream_session_unbind_actor (service, &session_rid, &actor, expected_generation, &op,
                                           timeout_ms)
        != ZLINK_SUBMIT_OK)
        return throw_last_error (env, "streamSessionUnbindActor failed");
    return svc_create_operation_id (env, op);
}

napi_value stream_session_bindings (napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *service = svc_external (env, argv[0]);
    zlink_routing_id_t session_rid;
    if (!parse_routing_id_value (env, argv[1], &session_rid))
        return NULL;

    size_t count = 0;
    zlink_stream_session_bindings (service, &session_rid, NULL, &count);
    std::vector<zlink_stream_session_binding_t> entries (count);
    for (size_t i = 0; i < count; ++i) {
        memset (&entries[i], 0, sizeof (zlink_stream_session_binding_t));
        entries[i].struct_size = sizeof (zlink_stream_session_binding_t);
        entries[i].version = ZLINK_STREAM_SESSION_ABI_VERSION;
    }
    if (count > 0) {
        if (zlink_stream_session_bindings (service, &session_rid, entries.data (), &count)
            != ZLINK_CONFIG_OK)
            return throw_last_error (env, "streamSessionBindings failed");
    }

    napi_value array;
    napi_create_array_with_length (env, count, &array);
    for (size_t i = 0; i < count; ++i) {
        const zlink_stream_session_binding_t &entry = entries[i];
        napi_value obj;
        napi_create_object (env, &obj);
        napi_set_named_property (env, obj, "sessionRid",
                                 create_routing_id_value (env, entry.session_rid));
        napi_set_named_property (env, obj, "actor", svc_create_actor_ref (env, entry.actor));
        svc_set_bigint (env, obj, "bindingGeneration", entry.binding_generation);
        svc_set_bigint (env, obj, "membershipEpoch", entry.membership_epoch);
        napi_set_element (env, array, static_cast<uint32_t> (i), obj);
    }
    return array;
}

napi_value stream_session_send_to_actor (napi_env env, napi_callback_info info)
{
    napi_value argv[6];
    size_t argc = 6;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *service = svc_external (env, argv[0]);
    zlink_routing_id_t session_rid;
    if (!parse_routing_id_value (env, argv[1], &session_rid))
        return NULL;
    zlink_actor_ref_t actor;
    if (!svc_parse_actor_ref (env, argv[2], &actor))
        return NULL;
    zlink_mesh_metadata_view_t meta_storage;
    const zlink_mesh_metadata_view_t *meta = svc_metadata (env, argv[3], &meta_storage);
    std::vector<zlink_msg_t> parts;
    if (!svc_build_parts (env, argv[4], &parts))
        return NULL;
    zlink_send_flags_t flags = static_cast<zlink_send_flags_t> (svc_int32 (env, argv[5]));
    zlink_submit_result_t rc =
      zlink_stream_session_send_to_actor (service, &session_rid, &actor, meta,
                                          svc_parts_ptr (parts), parts.size (), flags);
    close_msg_vector (parts);
    napi_value out;
    napi_create_int32 (env, static_cast<int32_t> (rc), &out);
    return out;
}

napi_value stream_session_request_to_actor (napi_env env, napi_callback_info info)
{
    napi_value argv[7];
    size_t argc = 7;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *service = svc_external (env, argv[0]);
    zlink_routing_id_t session_rid;
    if (!parse_routing_id_value (env, argv[1], &session_rid))
        return NULL;
    zlink_actor_ref_t actor;
    if (!svc_parse_actor_ref (env, argv[2], &actor))
        return NULL;
    zlink_mesh_metadata_view_t meta_storage;
    const zlink_mesh_metadata_view_t *meta = svc_metadata (env, argv[3], &meta_storage);
    std::vector<zlink_msg_t> parts;
    if (!svc_build_parts (env, argv[4], &parts))
        return NULL;
    zlink_send_flags_t flags = static_cast<zlink_send_flags_t> (svc_int32 (env, argv[5]));
    uint32_t timeout_ms = svc_uint32 (env, argv[6]);
    zlink_mesh_operation_id_t op;
    memset (&op, 0, sizeof (op));
    zlink_submit_result_t rc =
      zlink_stream_session_request_to_actor (service, &session_rid, &actor, meta,
                                             svc_parts_ptr (parts), parts.size (), &op, flags,
                                             timeout_ms);
    close_msg_vector (parts);
    if (rc != ZLINK_SUBMIT_OK)
        return throw_last_error (env, "streamSessionRequestToActor failed");
    return svc_create_operation_id (env, op);
}

napi_value mesh_node_actor_send_bound_session (napi_env env, napi_callback_info info)
{
    napi_value argv[4];
    size_t argc = 4;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *node = svc_external (env, argv[0]);
    zlink_actor_ref_t actor;
    if (!svc_parse_actor_ref (env, argv[1], &actor))
        return NULL;
    std::vector<zlink_msg_t> parts;
    if (!svc_build_parts (env, argv[2], &parts))
        return NULL;
    zlink_send_flags_t flags = static_cast<zlink_send_flags_t> (svc_int32 (env, argv[3]));
    zlink_submit_result_t rc =
      zlink_mesh_node_actor_send_bound_session (node, &actor, svc_parts_ptr (parts), parts.size (),
                                                flags);
    close_msg_vector (parts);
    napi_value out;
    napi_create_int32 (env, static_cast<int32_t> (rc), &out);
    return out;
}

napi_value mesh_node_actor_close_bound_session (napi_env env, napi_callback_info info)
{
    napi_value argv[4];
    size_t argc = 4;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *node = svc_external (env, argv[0]);
    zlink_actor_ref_t actor;
    if (!svc_parse_actor_ref (env, argv[1], &actor))
        return NULL;
    uint64_t expected_generation = svc_u64 (env, argv[2]);
    uint32_t timeout_ms = svc_uint32 (env, argv[3]);
    zlink_mesh_operation_id_t op;
    memset (&op, 0, sizeof (op));
    if (zlink_mesh_node_actor_close_bound_session (node, &actor, expected_generation, &op,
                                                   timeout_ms)
        != ZLINK_SUBMIT_OK)
        return throw_last_error (env, "meshNodeActorCloseBoundSession failed");
    return svc_create_operation_id (env, op);
}
