/* SPDX-License-Identifier: MPL-2.0 */

#include "addon_spot_api.h"
#include "addon_message_values.h"
#include <vector>

namespace
{

napi_value create_spot_node_spot_entry_value (napi_env env,
                                              const zlink_spot_node_spot_entry_t &entry)
{
    napi_value obj;
    napi_create_object (env, &obj);
    napi_set_named_property (env, obj, "spotRid", create_routing_id_value (env, entry.spot_rid));
    set_uint32_property (env, obj, "spotKind", static_cast<uint32_t> (entry.spot_kind));
    napi_value dispatch_attached;
    napi_get_boolean (env, entry.dispatch_handler_attached != 0, &dispatch_attached);
    napi_set_named_property (env, obj, "dispatchHandlerAttached", dispatch_attached);
    set_uint32_property (env, obj, "joinedActorCount", entry.joined_actor_count);
    set_uint32_property (env, obj, "pendingActorJoinCount", entry.pending_actor_join_count);
    napi_value route_synced;
    napi_get_boolean (env, entry.route_synced != 0, &route_synced);
    napi_set_named_property (env, obj, "routeSynced", route_synced);
    napi_value changed;
    napi_create_bigint_uint64 (env, entry.last_changed_ms, &changed);
    napi_set_named_property (env, obj, "lastChangedMs", changed);
    return obj;
}

bool build_spot_node_peer_filter (napi_env env,
                                  napi_value value,
                                  zlink_spot_node_peer_filter_t *out)
{
    memset (out, 0, sizeof (*out));
    if (value == NULL)
        return false;
    napi_valuetype type = napi_undefined;
    if (napi_typeof (env, value, &type) != napi_ok || type == napi_undefined || type == napi_null)
        return false;

    napi_value prop;
    bool has_prop = false;
    if (napi_has_named_property (env, value, "peerEndpoint", &has_prop) == napi_ok && has_prop
        && napi_get_named_property (env, value, "peerEndpoint", &prop) == napi_ok) {
        std::string peer_endpoint = get_string (env, prop);
        strncpy (out->peer_endpoint, peer_endpoint.c_str (), sizeof (out->peer_endpoint) - 1);
    }
    if (napi_has_named_property (env, value, "source", &has_prop) == napi_ok && has_prop
        && napi_get_named_property (env, value, "source", &prop) == napi_ok) {
        uint32_t raw = 0;
        napi_get_value_uint32 (env, prop, &raw);
        out->source = static_cast<zlink_spot_peer_source_t> (raw);
    }
    if (napi_has_named_property (env, value, "state", &has_prop) == napi_ok && has_prop
        && napi_get_named_property (env, value, "state", &prop) == napi_ok) {
        uint32_t raw = 0;
        napi_get_value_uint32 (env, prop, &raw);
        out->state = static_cast<zlink_spot_peer_state_t> (raw);
    }
    return true;
}

bool build_spot_node_subject_filter (napi_env env,
                                     napi_value value,
                                     zlink_spot_node_subject_filter_t *out)
{
    memset (out, 0, sizeof (*out));
    if (value == NULL)
        return false;
    napi_valuetype type = napi_undefined;
    if (napi_typeof (env, value, &type) != napi_ok || type == napi_undefined || type == napi_null)
        return false;

    napi_value prop;
    bool has_prop = false;
    if (napi_has_named_property (env, value, "role", &has_prop) == napi_ok && has_prop
        && napi_get_named_property (env, value, "role", &prop) == napi_ok) {
        uint32_t raw = 0;
        napi_get_value_uint32 (env, prop, &raw);
        out->role = static_cast<zlink_spot_role_t> (raw);
    }
    if (napi_has_named_property (env, value, "subject", &has_prop) == napi_ok && has_prop
        && napi_get_named_property (env, value, "subject", &prop) == napi_ok) {
        std::string subject = get_string (env, prop);
        strncpy (out->subject, subject.c_str (), sizeof (out->subject) - 1);
    }
    if (napi_has_named_property (env, value, "subjectKind", &has_prop) == napi_ok && has_prop
        && napi_get_named_property (env, value, "subjectKind", &prop) == napi_ok) {
        uint32_t raw = 0;
        napi_get_value_uint32 (env, prop, &raw);
        out->subject_kind = raw;
    }
    return true;
}

bool build_spot_node_socket_filter (napi_env env,
                                    napi_value value,
                                    zlink_spot_node_socket_filter_t *out)
{
    memset (out, 0, sizeof (*out));
    out->owner = ZLINK_SPOT_NODE_SOCKET_OWNER_ANY;
    out->socket_type = ZLINK_SOCKET_ANY;
    if (value == NULL)
        return false;
    napi_valuetype type = napi_undefined;
    if (napi_typeof (env, value, &type) != napi_ok || type == napi_undefined || type == napi_null)
        return false;

    napi_value prop;
    bool has_prop = false;
    if (napi_has_named_property (env, value, "owner", &has_prop) == napi_ok && has_prop
        && napi_get_named_property (env, value, "owner", &prop) == napi_ok) {
        uint32_t raw = 0;
        napi_get_value_uint32 (env, prop, &raw);
        out->owner = static_cast<zlink_spot_node_socket_owner_t> (raw);
    }
    if (napi_has_named_property (env, value, "socketType", &has_prop) == napi_ok && has_prop
        && napi_get_named_property (env, value, "socketType", &prop) == napi_ok) {
        uint32_t raw = 0;
        napi_get_value_uint32 (env, prop, &raw);
        out->socket_type = static_cast<zlink_socket_type_t> (raw);
    }
    if (napi_has_named_property (env, value, "socketName", &has_prop) == napi_ok && has_prop
        && napi_get_named_property (env, value, "socketName", &prop) == napi_ok) {
        std::string socket_name = get_string (env, prop);
        strncpy (out->socket_name, socket_name.c_str (), sizeof (out->socket_name) - 1);
    }
    return true;
}

napi_value create_monitor_status_value (napi_env env, const zlink_monitor_status_t &snapshot)
{
    napi_value obj;
    napi_create_object (env, &obj);
    set_uint32_property (env, obj, "sourceKind", static_cast<uint32_t> (snapshot.source_kind));
    set_uint32_property (env, obj, "stateFlags", snapshot.state_flags);
    set_uint32_property (env, obj, "detailFlags", snapshot.detail_flags);
    set_int64_property (env, obj, "sndPendingMsgs",
                        static_cast<int64_t> (snapshot.snd_pending_msgs));
    set_int64_property (env, obj, "rcvPendingMsgs",
                        static_cast<int64_t> (snapshot.rcv_pending_msgs));
    napi_value auto_hwm_enabled;
    napi_get_boolean (env, snapshot.auto_hwm_enabled != 0, &auto_hwm_enabled);
    napi_set_named_property (env, obj, "autoHwmEnabled", auto_hwm_enabled);
    set_uint32_property (env, obj, "autoHwmProfile", snapshot.auto_hwm_profile);
    set_uint32_property (env, obj, "autoHwmRole", snapshot.auto_hwm_role);
    set_uint32_property (env, obj, "autoHwmPolicyClass", snapshot.auto_hwm_policy_class);
    set_int64_property (env, obj, "autoHwmUnitBudgetBytes",
                        static_cast<int64_t> (snapshot.auto_hwm_unit_budget_bytes));
    set_uint32_property (env, obj, "autoHwmSizeCap", snapshot.auto_hwm_size_cap);
    set_int64_property (env, obj, "autoHwmSocketMessageSlots",
                        static_cast<int64_t> (snapshot.auto_hwm_socket_message_slots));
    napi_value auto_hwm_connection_bucket_enabled;
    napi_get_boolean (env, snapshot.auto_hwm_connection_bucket_enabled != 0,
                      &auto_hwm_connection_bucket_enabled);
    napi_set_named_property (env, obj, "autoHwmConnectionBucketEnabled",
                             auto_hwm_connection_bucket_enabled);
    set_uint32_property (env, obj, "autoHwmConnectionBucketCount",
                         snapshot.auto_hwm_connection_bucket_count);
    set_uint32_property (env, obj, "autoHwmConnectionBucketIndex",
                         snapshot.auto_hwm_connection_bucket_index);
    set_uint32_property (env, obj, "autoHwmConnectionBucketHwm4K",
                         snapshot.auto_hwm_connection_bucket_hwm_4k);
    napi_value auto_hwm_connection_bucket_hysteresis_retained;
    napi_get_boolean (env, snapshot.auto_hwm_connection_bucket_hysteresis_retained != 0,
                      &auto_hwm_connection_bucket_hysteresis_retained);
    napi_set_named_property (env, obj, "autoHwmConnectionBucketHysteresisRetained",
                             auto_hwm_connection_bucket_hysteresis_retained);
    set_int64_property (env, obj, "autoHwmEffectiveMessageBytes",
                        static_cast<int64_t> (snapshot.auto_hwm_effective_message_bytes));
    set_int64_property (env, obj, "autoHwmAppliedSndHwm", snapshot.auto_hwm_applied_sndhwm);
    set_int64_property (env, obj, "autoHwmAppliedRcvHwm", snapshot.auto_hwm_applied_rcvhwm);
    set_int64_property (env, obj, "autoHwmEffectiveSndBuf", snapshot.auto_hwm_effective_sndbuf);
    set_int64_property (env, obj, "autoHwmEffectiveRcvBuf", snapshot.auto_hwm_effective_rcvbuf);
    set_int64_property (env, obj, "autoHwmLastRecalcMs",
                        static_cast<int64_t> (snapshot.auto_hwm_last_recalc_ms));
    set_uint32_property (env, obj, "autoHwmLastRecalcReason", snapshot.auto_hwm_last_recalc_reason);
    set_uint32_property (env, obj, "autoHwmSendBlockedRatioPpm",
                         snapshot.auto_hwm_send_blocked_ratio_ppm);
    set_int64_property (env, obj, "autoHwmDeferredSndHwm", snapshot.auto_hwm_deferred_sndhwm);
    set_int64_property (env, obj, "autoHwmDeferredRcvHwm", snapshot.auto_hwm_deferred_rcvhwm);
    return obj;
}

} // namespace

napi_value spot_node_status (napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *node = NULL;
    napi_get_value_external (env, argv[0], &node);

    zlink_spot_node_status_t status;
    memset (&status, 0, sizeof (status));
    int rc = zlink_spot_node_status (node, &status);
    if (rc != 0)
        return throw_last_error (env, "spot_node_status failed");

    napi_value obj;
    napi_create_object (env, &obj);
    set_string_property (env, obj, "channelName", status.channel_name);
    set_string_property (env, obj, "localEndpoint", status.local_endpoint);
    napi_value rid = create_routing_id_value (env, status.node_routing_id);
    napi_set_named_property (env, obj, "nodeRoutingId", rid);
    set_uint32_property (env, obj, "state", static_cast<uint32_t> (status.state));
    set_uint32_property (env, obj, "configuredPeerCount", status.configured_peer_count);
    set_uint32_property (env, obj, "activePeerCount", status.active_peer_count);
    set_uint32_property (env, obj, "connectedPeerCount", status.connected_peer_count);
    set_uint32_property (env, obj, "subjectCount", status.subject_count);
    set_uint32_property (env, obj, "readySubjectCount", status.ready_subject_count);
    set_uint32_property (env, obj, "disconnectedSubTargetCount",
                         status.disconnected_sub_target_count);
    set_uint32_property (env, obj, "disconnectedRoutedTargetCount",
                         status.disconnected_routed_target_count);
    set_int64_property (env, obj, "lastError", status.last_error);
    set_int64_property (env, obj, "lastChangedMs", static_cast<int64_t> (status.last_changed_ms));
    return obj;
}

napi_value spot_node_peers (napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *node = NULL;
    napi_get_value_external (env, argv[0], &node);

    size_t count = 0;
    int rc = zlink_spot_node_peers (node, NULL, NULL, &count);
    if (rc != 0)
        return throw_last_error (env, "spot_node_peers failed");
    napi_value arr;
    napi_create_array_with_length (env, count, &arr);
    if (count == 0)
        return arr;

    std::vector<zlink_spot_node_peer_entry_t> entries (count);
    rc = zlink_spot_node_peers (node, NULL, entries.data (), &count);
    if (rc != 0)
        return throw_last_error (env, "spot_node_peers failed");
    for (size_t i = 0; i < count; ++i) {
        napi_value obj;
        napi_create_object (env, &obj);
        set_string_property (env, obj, "channelName", entries[i].channel_name);
        set_string_property (env, obj, "localEndpoint", entries[i].local_endpoint);
        set_string_property (env, obj, "peerEndpoint", entries[i].peer_endpoint);
        set_uint32_property (env, obj, "source", static_cast<uint32_t> (entries[i].source));
        set_uint32_property (env, obj, "kind", static_cast<uint32_t> (entries[i].kind));
        set_uint32_property (env, obj, "state", static_cast<uint32_t> (entries[i].state));
        set_uint32_property (env, obj, "weight", static_cast<uint32_t> (entries[i].weight));
        set_int64_property (env, obj, "connectedSinceMs",
                            static_cast<int64_t> (entries[i].connected_since_ms));
        set_int64_property (env, obj, "lastChangedMs",
                            static_cast<int64_t> (entries[i].last_changed_ms));
        napi_set_element (env, arr, static_cast<uint32_t> (i), obj);
    }
    return arr;
}

napi_value spot_node_peers_query (napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *node = NULL;
    napi_get_value_external (env, argv[0], &node);

    zlink_spot_node_peer_filter_t filter;
    zlink_spot_node_peer_filter_t *filter_ptr =
      build_spot_node_peer_filter (env, argc >= 2 ? argv[1] : NULL, &filter) ? &filter : NULL;

    size_t count = 0;
    int rc = zlink_spot_node_peers (node, filter_ptr, NULL, &count);
    if (rc != 0)
        return throw_last_error (env, "spot_node_peers_query failed");
    napi_value arr;
    napi_create_array_with_length (env, count, &arr);
    if (count == 0)
        return arr;

    std::vector<zlink_spot_node_peer_entry_t> entries (count);
    rc = zlink_spot_node_peers (node, filter_ptr, entries.data (), &count);
    if (rc != 0)
        return throw_last_error (env, "spot_node_peers_query failed");
    for (size_t i = 0; i < count; ++i) {
        napi_value obj;
        napi_create_object (env, &obj);
        set_string_property (env, obj, "channelName", entries[i].channel_name);
        set_string_property (env, obj, "localEndpoint", entries[i].local_endpoint);
        set_string_property (env, obj, "peerEndpoint", entries[i].peer_endpoint);
        set_uint32_property (env, obj, "source", static_cast<uint32_t> (entries[i].source));
        set_uint32_property (env, obj, "kind", static_cast<uint32_t> (entries[i].kind));
        set_uint32_property (env, obj, "state", static_cast<uint32_t> (entries[i].state));
        set_uint32_property (env, obj, "weight", static_cast<uint32_t> (entries[i].weight));
        set_int64_property (env, obj, "connectedSinceMs",
                            static_cast<int64_t> (entries[i].connected_since_ms));
        set_int64_property (env, obj, "lastChangedMs",
                            static_cast<int64_t> (entries[i].last_changed_ms));
        napi_set_element (env, arr, static_cast<uint32_t> (i), obj);
    }
    return arr;
}

napi_value spot_node_subjects (napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *node = NULL;
    napi_get_value_external (env, argv[0], &node);
    zlink_spot_node_subject_filter_t filter;
    zlink_spot_node_subject_filter_t *filter_ptr =
      build_spot_node_subject_filter (env, argc >= 2 ? argv[1] : NULL, &filter) ? &filter : NULL;

    size_t count = 0;
    int rc = zlink_spot_node_subjects (node, filter_ptr, NULL, &count);
    if (rc != 0)
        return throw_last_error (env, "spot_node_subjects failed");
    napi_value arr;
    napi_create_array_with_length (env, count, &arr);
    if (count == 0)
        return arr;

    std::vector<zlink_spot_node_subject_entry_t> entries (count);
    rc = zlink_spot_node_subjects (node, filter_ptr, entries.data (), &count);
    if (rc != 0)
        return throw_last_error (env, "spot_node_subjects failed");
    for (size_t i = 0; i < count; ++i) {
        napi_value obj;
        napi_create_object (env, &obj);
        set_uint32_property (env, obj, "role", static_cast<uint32_t> (entries[i].role));
        set_string_property (env, obj, "subject", entries[i].subject);
        set_uint32_property (env, obj, "subjectKind", entries[i].subject_kind);
        set_uint32_property (env, obj, "readyPeerCount", entries[i].ready_peer_count);
        set_uint32_property (env, obj, "activePeerCount", entries[i].active_peer_count);
        set_int64_property (env, obj, "lastChangedMs",
                            static_cast<int64_t> (entries[i].last_changed_ms));
        napi_set_element (env, arr, static_cast<uint32_t> (i), obj);
    }
    return arr;
}

napi_value spot_node_internal_sockets (napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *node = NULL;
    napi_get_value_external (env, argv[0], &node);
    zlink_spot_node_socket_filter_t filter;
    zlink_spot_node_socket_filter_t *filter_ptr =
      build_spot_node_socket_filter (env, argc >= 2 ? argv[1] : NULL, &filter) ? &filter : NULL;

    size_t count = 0;
    int rc = zlink_spot_node_internal_sockets (node, filter_ptr, NULL, &count);
    if (rc != 0)
        return throw_last_error (env, "spot_node_internal_sockets failed");
    napi_value arr;
    napi_create_array_with_length (env, count, &arr);
    if (count == 0)
        return arr;

    std::vector<zlink_spot_node_socket_entry_t> entries (count);
    rc = zlink_spot_node_internal_sockets (node, filter_ptr, entries.data (), &count);
    if (rc != 0)
        return throw_last_error (env, "spot_node_internal_sockets failed");
    for (size_t i = 0; i < count; ++i) {
        napi_value obj;
        napi_create_object (env, &obj);
        set_uint32_property (env, obj, "owner", static_cast<uint32_t> (entries[i].owner));
        set_int64_property (env, obj, "ownerId", static_cast<int64_t> (entries[i].owner_id));
        set_string_property (env, obj, "ownerName", entries[i].owner_name);
        set_string_property (env, obj, "socketName", entries[i].socket_name);
        set_uint32_property (env, obj, "socketType",
                             static_cast<uint32_t> (entries[i].socket_type));
        napi_value visible;
        napi_get_boolean (env, entries[i].auto_hwm_visible != 0, &visible);
        napi_set_named_property (env, obj, "autoHwmVisible", visible);
        napi_value snapshot = create_monitor_status_value (env, entries[i].monitor_status);
        napi_set_named_property (env, obj, "snapshot", snapshot);
        napi_set_element (env, arr, static_cast<uint32_t> (i), obj);
    }
    return arr;
}

napi_value spot_node_spots (napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *node = NULL;
    napi_get_value_external (env, argv[0], &node);
    size_t count = 0;
    int rc = zlink_spot_node_spots (node, NULL, &count);
    if (rc != 0)
        return throw_last_error (env, "spotNodeSpots failed");
    napi_value arr;
    napi_create_array_with_length (env, count, &arr);
    if (count == 0)
        return arr;
    std::vector<zlink_spot_node_spot_entry_t> entries (count);
    rc = zlink_spot_node_spots (node, entries.data (), &count);
    if (rc != 0)
        return throw_last_error (env, "spotNodeSpots failed");
    for (size_t i = 0; i < count; ++i) {
        napi_set_element (env, arr, static_cast<uint32_t> (i),
                          create_spot_node_spot_entry_value (env, entries[i]));
    }
    return arr;
}
