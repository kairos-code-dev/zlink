/* SPDX-License-Identifier: MPL-2.0 */

#include "addon_spot_actor_values.h"
#include "addon_message_values.h"

bool parse_routing_id_value (napi_env env, napi_value value, zlink_routing_id_t *routing_id)
{
    void *data = NULL;
    size_t len = 0;
    if (napi_get_buffer_info (env, value, &data, &len) != napi_ok) {
        napi_throw_type_error (env, NULL, "routingId must be Buffer");
        return false;
    }
    if (len == 0 || len > sizeof (routing_id->data)) {
        napi_throw_range_error (env, NULL, "routingId length must be 1..255 bytes");
        return false;
    }
    memset (routing_id, 0, sizeof (*routing_id));
    routing_id->size = static_cast<uint8_t> (len);
    memcpy (routing_id->data, data, len);
    return true;
}

namespace
{

std::string get_optional_string_property (napi_env env, napi_value obj, const char *name)
{
    napi_value prop;
    bool has_prop = false;
    if (napi_has_named_property (env, obj, name, &has_prop) != napi_ok || !has_prop
        || napi_get_named_property (env, obj, name, &prop) != napi_ok) {
        return std::string ();
    }
    return get_string (env, prop);
}

bool get_optional_routing_id_property (napi_env env,
                                       napi_value obj,
                                       const char *name,
                                       zlink_routing_id_t *out)
{
    memset (out, 0, sizeof (*out));
    napi_value prop;
    bool has_prop = false;
    if (napi_has_named_property (env, obj, name, &has_prop) != napi_ok || !has_prop
        || napi_get_named_property (env, obj, name, &prop) != napi_ok) {
        return true;
    }
    napi_valuetype type = napi_undefined;
    napi_typeof (env, prop, &type);
    if (type == napi_null || type == napi_undefined)
        return true;
    return parse_routing_id_value (env, prop, out);
}

napi_value create_actor_recv_info_value (napi_env env, const zlink_actor_recv_info_t &info)
{
    napi_value obj;
    napi_create_object (env, &obj);
    napi_set_named_property (env, obj, "actor", create_actor_ref_value (env, info.actor));
    napi_set_named_property (env, obj, "sourceNodeRid",
                             create_routing_id_value (env, info.source_node_rid));
    napi_set_named_property (env, obj, "sourceSessionRid",
                             create_routing_id_value (env, info.source_session_rid));
    napi_value request_id;
    napi_create_bigint_uint64 (env, info.request_id, &request_id);
    napi_set_named_property (env, obj, "requestId", request_id);
    set_uint32_property (env, obj, "flags", info.flags);
    return obj;
}

} // namespace

bool parse_actor_ref_value (napi_env env, napi_value value, zlink_actor_ref_t *out)
{
    memset (out, 0, sizeof (*out));
    napi_valuetype type = napi_undefined;
    if (napi_typeof (env, value, &type) != napi_ok || type != napi_object) {
        napi_throw_type_error (env, NULL, "actorRef must be an object");
        return false;
    }
    if (!get_optional_routing_id_property (env, value, "nodeRid", &out->node_rid)
        || (out->node_rid.size == 0
            && !get_optional_routing_id_property (env, value, "node_rid", &out->node_rid))) {
        return false;
    }
    if (out->node_rid.size == 0) {
        napi_throw_type_error (env, NULL, "actorRef.nodeRid must be a RoutingId");
        return false;
    }
    std::string actor_id = get_optional_string_property (env, value, "actorId");
    if (actor_id.empty ())
        actor_id = get_optional_string_property (env, value, "actor_id");
    if (actor_id.empty () || actor_id.size () >= sizeof (out->actor_id)) {
        napi_throw_range_error (env, NULL, "actorId must be 1..255 bytes");
        return false;
    }
    memcpy (out->actor_id, actor_id.c_str (), actor_id.size ());
    napi_value generation_value;
    bool has_generation = false;
    if (napi_has_named_property (env, value, "generation", &has_generation) == napi_ok
        && has_generation
        && napi_get_named_property (env, value, "generation", &generation_value) == napi_ok) {
        bool lossless = false;
        napi_get_value_bigint_uint64 (env, generation_value, &out->generation, &lossless);
    }
    return true;
}

bool parse_actor_recv_info_value (napi_env env, napi_value value, zlink_actor_recv_info_t *out)
{
    memset (out, 0, sizeof (*out));
    napi_valuetype type = napi_undefined;
    if (napi_typeof (env, value, &type) != napi_ok || type != napi_object) {
        napi_throw_type_error (env, NULL, "actorRecvInfo must be an object");
        return false;
    }
    napi_value actor_value;
    if (napi_get_named_property (env, value, "actor", &actor_value) != napi_ok
        || !parse_actor_ref_value (env, actor_value, &out->actor)) {
        return false;
    }
    if (!get_optional_routing_id_property (env, value, "sourceNodeRid", &out->source_node_rid)
        || out->source_node_rid.size == 0) {
        napi_throw_type_error (env, NULL, "actorRecvInfo.sourceNodeRid must be a RoutingId");
        return false;
    }
    if (!get_optional_routing_id_property (env, value, "sourceSessionRid", &out->source_session_rid)
        || out->source_session_rid.size == 0) {
        napi_throw_type_error (env, NULL, "actorRecvInfo.sourceSessionRid must be a RoutingId");
        return false;
    }
    napi_value request_id_value;
    bool lossless = false;
    if (napi_get_named_property (env, value, "requestId", &request_id_value) != napi_ok
        || napi_get_value_bigint_uint64 (env, request_id_value, &out->request_id, &lossless)
             != napi_ok
        || !lossless) {
        napi_throw_type_error (env, NULL, "actorRecvInfo.requestId must be uint64 BigInt");
        return false;
    }
    napi_value flags_value;
    if (napi_get_named_property (env, value, "flags", &flags_value) == napi_ok)
        napi_get_value_uint32 (env, flags_value, &out->flags);
    return true;
}

napi_value create_actor_ref_value (napi_env env, const zlink_actor_ref_t &actor)
{
    napi_value obj;
    napi_create_object (env, &obj);
    napi_value node_rid = create_routing_id_value (env, actor.node_rid);
    napi_set_named_property (env, obj, "nodeRid", node_rid);
    set_string_property (env, obj, "actorId", actor.actor_id);
    napi_value generation;
    napi_create_bigint_uint64 (env, actor.generation, &generation);
    napi_set_named_property (env, obj, "generation", generation);
    return obj;
}

napi_value create_actor_part_value (napi_env env,
                                    const zlink_actor_recv_info_t &info,
                                    zlink_msg_t *part,
                                    int more)
{
    napi_value obj;
    napi_create_object (env, &obj);
    napi_set_named_property (env, obj, "info", create_actor_recv_info_value (env, info));
    napi_value msg = create_message_data_buffer (env, part);
    if (!msg)
        return NULL;
    napi_set_named_property (env, obj, "message", msg);
    napi_value more_value;
    napi_get_boolean (env, more != ZLINK_PART_FINAL, &more_value);
    napi_set_named_property (env, obj, "more", more_value);
    return obj;
}

napi_value create_spot_node_actor_entry_value (napi_env env,
                                               const zlink_spot_node_actor_entry_t &entry)
{
    napi_value obj;
    napi_create_object (env, &obj);
    napi_set_named_property (env, obj, "actor", create_actor_ref_value (env, entry.actor));
    napi_set_named_property (env, obj, "currentSpotRid",
                             create_routing_id_value (env, entry.current_spot_rid));
    set_uint32_property (env, obj, "currentSpotKind",
                         static_cast<uint32_t> (entry.current_spot_kind));
    napi_value route_synced;
    napi_get_boolean (env, entry.route_synced != 0, &route_synced);
    napi_set_named_property (env, obj, "routeSynced", route_synced);
    set_uint32_property (env, obj, "pendingMessageCount", entry.pending_message_count);
    napi_value changed;
    napi_create_bigint_uint64 (env, entry.last_changed_ms, &changed);
    napi_set_named_property (env, obj, "lastChangedMs", changed);
    return obj;
}
