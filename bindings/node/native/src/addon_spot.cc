/* SPDX-License-Identifier: MPL-2.0 */

#include "addon_api.h"

namespace {

napi_value unsupported_spot_node(napi_env env, const char *method)
{
    napi_throw_error(env, NULL, method);
    return NULL;
}

void set_uint32_property(napi_env env, napi_value obj, const char *name, uint32_t value)
{
    napi_value out;
    napi_create_uint32(env, value, &out);
    napi_set_named_property(env, obj, name, out);
}

void set_int64_property(napi_env env, napi_value obj, const char *name, int64_t value)
{
    napi_value out;
    napi_create_int64(env, value, &out);
    napi_set_named_property(env, obj, name, out);
}

void set_string_property(napi_env env,
                         napi_value obj,
                         const char *name,
                         const char *value)
{
    napi_value out;
    napi_create_string_utf8(env, value ? value : "", NAPI_AUTO_LENGTH, &out);
    napi_set_named_property(env, obj, name, out);
}

napi_value create_routing_id_value(napi_env env, const zlink_routing_id_t &rid)
{
    if (rid.size == 0) {
        napi_value none;
        napi_get_null(env, &none);
        return none;
    }

    napi_value out;
    napi_create_buffer_copy(env, rid.size, rid.data, NULL, &out);
    return out;
}

} // namespace

napi_value spot_node_new(napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *ctx = NULL;
    napi_get_value_external(env, argv[0], &ctx);
    void *node = zlink_spot_node_new(ctx);
    if (!node)
        return throw_last_error(env, "spot_node_new failed");
    napi_value ext;
    napi_create_external(env, node, NULL, NULL, &ext);
    return ext;
}

napi_value spot_node_destroy(napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *node = NULL;
    napi_get_value_external(env, argv[0], &node);
    void *tmp = node;
    int rc = zlink_spot_node_destroy(&tmp);
    if (rc != 0)
        return throw_last_error(env, "spot_node_destroy failed");
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

napi_value spot_node_bind(napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *node = NULL;
    napi_get_value_external(env, argv[0], &node);
    std::string ep = get_string(env, argv[1]);
    int rc = zlink_spot_node_bind(node, ep.c_str());
    if (rc != 0)
        return throw_last_error(env, "spot_node_bind failed");
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

napi_value spot_node_connect_peer(napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *node = NULL;
    napi_get_value_external(env, argv[0], &node);
    std::string ep = get_string(env, argv[1]);
    int rc = zlink_spot_node_connect_peer(node, ep.c_str());
    if (rc != 0)
        return throw_last_error(env, "spot_node_connect_peer failed");
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

napi_value spot_node_disconnect_peer(napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *node = NULL;
    napi_get_value_external(env, argv[0], &node);
    std::string ep = get_string(env, argv[1]);
    int rc = zlink_spot_node_disconnect_peer(node, ep.c_str());
    if (rc != 0)
        return throw_last_error(env, "spot_node_disconnect_peer failed");
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

napi_value spot_node_register(napi_env env, napi_callback_info info)
{
    (void) info;
    return unsupported_spot_node(
      env, "SpotNode.register is not available on the aligned public API");
}

napi_value spot_node_unregister(napi_env env, napi_callback_info info)
{
    (void) info;
    return unsupported_spot_node(
      env, "SpotNode.unregister is not available on the aligned public API");
}

napi_value spot_node_set_discovery(napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *node = NULL;
    void *discovery = NULL;
    napi_get_value_external(env, argv[0], &node);
    napi_get_value_external(env, argv[1], &discovery);
    int rc = zlink_spot_node_attach_discovery(node, discovery);
    if (rc != 0)
        return throw_last_error(env, "spot_node_attach_discovery failed");
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

napi_value spot_node_set_tls_server(napi_env env, napi_callback_info info)
{
    napi_value argv[4];
    size_t argc = 4;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *node = NULL;
    napi_get_value_external(env, argv[0], &node);
    std::string cert = get_string(env, argv[1]);
    std::string key = get_string(env, argv[2]);
    int32_t require_client = 0;
    if (argc >= 4)
        napi_get_value_int32(env, argv[3], &require_client);
    int rc = zlink_set_tls_server(node, cert.c_str(), key.c_str(),
                                  require_client);
    if (rc != 0)
        return throw_last_error(env, "spot_node_set_tls_server failed");
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

napi_value spot_node_set_tls_client(napi_env env, napi_callback_info info)
{
    napi_value argv[4];
    size_t argc = 4;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *node = NULL;
    napi_get_value_external(env, argv[0], &node);
    std::string ca = get_string(env, argv[1]);
    std::string host = get_string(env, argv[2]);
    int32_t trust = 0;
    napi_get_value_int32(env, argv[3], &trust);
    int rc = zlink_set_tls_client(node, ca.c_str(), host.c_str(), trust);
    if (rc != 0)
        return throw_last_error(env, "spot_node_set_tls_client failed");
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

napi_value spot_node_setsockopt(napi_env env, napi_callback_info info)
{
    (void) info;
    return unsupported_spot_node(
      env, "SpotNode.setSockOpt is not available on the aligned public API");
}

napi_value spot_node_open_monitor(napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *node = NULL;
    napi_get_value_external(env, argv[0], &node);
    uint32_t events = 0;
    napi_get_value_uint32(env, argv[1], &events);

    zlink_service_monitor_open_options_t options;
    options.events = static_cast<zlink_service_monitor_event_mask_t>(events);
    void *monitor = zlink_service_monitor_open(node, &options);
    if (!monitor)
        return throw_last_error(env, "spot_node_open_monitor failed");

    napi_value ext;
    napi_create_external(env, monitor, NULL, NULL, &ext);
    return ext;
}

napi_value spot_node_status_snapshot(napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *node = NULL;
    napi_get_value_external(env, argv[0], &node);

    zlink_spot_node_status_t status;
    memset(&status, 0, sizeof(status));
    int rc = zlink_spot_node_status_snapshot(node, &status);
    if (rc != 0)
        return throw_last_error(env, "spot_node_status_snapshot failed");

    napi_value obj;
    napi_create_object(env, &obj);
    set_string_property(env, obj, "serviceName", status.service_name);
    set_string_property(env, obj, "localEndpoint", status.local_endpoint);
    napi_value rid = create_routing_id_value(env, status.node_routing_id);
    napi_set_named_property(env, obj, "nodeRoutingId", rid);
    set_uint32_property(env, obj, "state", static_cast<uint32_t>(status.state));
    set_uint32_property(env, obj, "configuredPeerCount", status.configured_peer_count);
    set_uint32_property(env, obj, "activePeerCount", status.active_peer_count);
    set_uint32_property(env, obj, "connectedPeerCount", status.connected_peer_count);
    set_uint32_property(env, obj, "subjectCount", status.subject_count);
    set_uint32_property(env, obj, "readySubjectCount", status.ready_subject_count);
    set_int64_property(env, obj, "lastError", status.last_error);
    set_int64_property(env, obj, "lastChangedMs",
                       static_cast<int64_t>(status.last_changed_ms));
    return obj;
}

napi_value spot_node_peers_snapshot(napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *node = NULL;
    napi_get_value_external(env, argv[0], &node);

    size_t count = 0;
    int rc = zlink_spot_node_peers_snapshot(node, NULL, &count);
    if (rc != 0)
        return throw_last_error(env, "spot_node_peers_snapshot failed");
    napi_value arr;
    napi_create_array_with_length(env, count, &arr);
    if (count == 0)
        return arr;

    std::vector<zlink_spot_node_peer_entry_t> entries(count);
    rc = zlink_spot_node_peers_snapshot(node, entries.data(), &count);
    if (rc != 0)
        return throw_last_error(env, "spot_node_peers_snapshot failed");
    for (size_t i = 0; i < count; ++i) {
        napi_value obj;
        napi_create_object(env, &obj);
        set_string_property(env, obj, "serviceName", entries[i].service_name);
        set_string_property(env, obj, "localEndpoint", entries[i].local_endpoint);
        set_string_property(env, obj, "peerEndpoint", entries[i].peer_endpoint);
        set_uint32_property(env, obj, "source", static_cast<uint32_t>(entries[i].source));
        set_uint32_property(env, obj, "state", static_cast<uint32_t>(entries[i].state));
        set_int64_property(env, obj, "connectedSinceMs",
                           static_cast<int64_t>(entries[i].connected_since_ms));
        set_int64_property(env, obj, "lastChangedMs",
                           static_cast<int64_t>(entries[i].last_changed_ms));
        napi_set_element(env, arr, static_cast<uint32_t>(i), obj);
    }
    return arr;
}

napi_value spot_node_subjects_snapshot(napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *node = NULL;
    napi_get_value_external(env, argv[0], &node);

    size_t count = 0;
    int rc = zlink_spot_node_subjects_snapshot(node, NULL, NULL, &count);
    if (rc != 0)
        return throw_last_error(env, "spot_node_subjects_snapshot failed");
    napi_value arr;
    napi_create_array_with_length(env, count, &arr);
    if (count == 0)
        return arr;

    std::vector<zlink_spot_node_subject_entry_t> entries(count);
    rc = zlink_spot_node_subjects_snapshot(node, NULL, entries.data(), &count);
    if (rc != 0)
        return throw_last_error(env, "spot_node_subjects_snapshot failed");
    for (size_t i = 0; i < count; ++i) {
        napi_value obj;
        napi_create_object(env, &obj);
        set_uint32_property(env, obj, "role", static_cast<uint32_t>(entries[i].role));
        set_string_property(env, obj, "subject", entries[i].subject);
        set_uint32_property(env, obj, "subjectKind", entries[i].subject_kind);
        set_uint32_property(env, obj, "readyPeerCount", entries[i].ready_peer_count);
        set_uint32_property(env, obj, "activePeerCount", entries[i].active_peer_count);
        set_int64_property(env, obj, "lastChangedMs",
                           static_cast<int64_t>(entries[i].last_changed_ms));
        napi_set_element(env, arr, static_cast<uint32_t>(i), obj);
    }
    return arr;
}

napi_value spot_node_pub_socket(napi_env env, napi_callback_info info)
{
    (void) info;
    return unsupported_spot_node(
      env, "SpotNode.pubSocket is not available on the aligned public API");
}

napi_value spot_node_sub_socket(napi_env env, napi_callback_info info)
{
    (void) info;
    return unsupported_spot_node(
      env, "SpotNode.subSocket is not available on the aligned public API");
}

napi_value spot_node_pub_peers(napi_env env, napi_callback_info info)
{
    (void) info;
    return unsupported_spot_node(
      env, "SpotNode.pubPeers is not available on the aligned public API");
}

napi_value spot_node_sub_peers(napi_env env, napi_callback_info info)
{
    (void) info;
    return unsupported_spot_node(
      env, "SpotNode.subPeers is not available on the aligned public API");
}

napi_value spot_new(napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *ctx = NULL;
    napi_get_value_external(env, argv[0], &ctx);
    void *spot = zlink_spot_new(ctx);
    if (!spot)
        return throw_last_error(env, "spot_new failed");
    napi_value ext;
    napi_create_external(env, spot, NULL, NULL, &ext);
    return ext;
}

napi_value spot_destroy(napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *spot = NULL;
    napi_get_value_external(env, argv[0], &spot);
    void *tmp = spot;
    int rc = zlink_spot_destroy(&tmp);
    if (rc != 0)
        return throw_last_error(env, "spot_destroy failed");
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

napi_value spot_publish(napi_env env, napi_callback_info info)
{
    napi_value argv[4];
    size_t argc = 4;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *spot = NULL;
    napi_get_value_external(env, argv[0], &spot);
    std::string topic = get_string(env, argv[1]);
    int32_t flags = 0;
    napi_get_value_int32(env, argv[3], &flags);

    bool is_buffer = false;
    napi_is_buffer(env, argv[2], &is_buffer);
    std::vector<zlink_msg_t> parts;
    if (is_buffer) {
        void *data = NULL;
        size_t len = 0;
        if (napi_get_buffer_info(env, argv[2], &data, &len) != napi_ok) {
            napi_throw_type_error(env, NULL, "payload must be Buffer");
            return NULL;
        }
        parts.resize(1);
        if (zlink_msg_init_size(&parts[0], len) != 0)
            return throw_last_error(env, "spot_publish failed");
        if (len > 0)
            memcpy(zlink_msg_data(&parts[0]), data, len);
    } else {
        if (!build_msg_vector(env, argv[2], &parts))
            return NULL;
    }

    int rc = zlink_publish(spot, topic.c_str(), parts.data(), parts.size(),
                           flags);
    if (rc != 0) {
        close_msg_vector(parts);
        return throw_last_error(env, "spot_publish failed");
    }

    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

napi_value spot_subscribe(napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *spot = NULL;
    napi_get_value_external(env, argv[0], &spot);
    std::string topic = get_string(env, argv[1]);
    int rc = zlink_set_subscription(spot, topic.c_str());
    if (rc != 0)
        return throw_last_error(env, "spot_subscribe failed");
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

napi_value spot_subscribe_pattern(napi_env env, napi_callback_info info)
{
    return spot_subscribe(env, info);
}

napi_value spot_unsubscribe(napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *spot = NULL;
    napi_get_value_external(env, argv[0], &spot);
    std::string topic = get_string(env, argv[1]);
    int rc = zlink_unset_subscription(spot, topic.c_str());
    if (rc != 0)
        return throw_last_error(env, "spot_unsubscribe failed");
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

napi_value spot_recv(napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *spot = NULL;
    napi_get_value_external(env, argv[0], &spot);
    int32_t flags = 0;
    napi_get_value_int32(env, argv[1], &flags);

    zlink_routing_id_t routing_id;
    memset(&routing_id, 0, sizeof(routing_id));
    zlink_msg_t *parts = NULL;
    size_t count = 0;
    char topic[256] = {0};
    size_t topic_len = sizeof(topic);
    int rc = zlink_subscribe(spot, &routing_id, &parts, &count, topic,
                             &topic_len, flags);
    if (rc != 0)
        return throw_last_error(env, "spot_recv failed");

    napi_value arr;
    napi_create_array_with_length(env, count, &arr);
    for (size_t i = 0; i < count; ++i) {
        size_t sz = zlink_msg_size(&parts[i]);
        void *data = zlink_msg_data(&parts[i]);
        napi_value buf;
        napi_create_buffer_copy(env, sz, data, NULL, &buf);
        napi_set_element(env, arr, static_cast<uint32_t>(i), buf);
    }
    zlink_multipart_close(parts, count);
    free(parts);

    napi_value obj;
    napi_create_object(env, &obj);
    napi_value topic_value;
    napi_create_string_utf8(env, topic, NAPI_AUTO_LENGTH, &topic_value);
    napi_set_named_property(env, obj, "topic", topic_value);
    napi_set_named_property(env, obj, "parts", arr);
    return obj;
}

napi_value spot_open_monitor(napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *spot = NULL;
    napi_get_value_external(env, argv[0], &spot);
    uint32_t events = 0;
    napi_get_value_uint32(env, argv[1], &events);

    zlink_service_monitor_open_options_t options;
    options.events = static_cast<zlink_service_monitor_event_mask_t>(events);
    void *monitor = zlink_service_monitor_open(spot, &options);
    if (!monitor)
        return throw_last_error(env, "spot_open_monitor failed");

    napi_value ext;
    napi_create_external(env, monitor, NULL, NULL, &ext);
    return ext;
}
