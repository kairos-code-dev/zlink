/* SPDX-License-Identifier: MPL-2.0 */

#include "addon_api.h"
#include <errno.h>

namespace {

static const int32_t k_legacy_service_spot = 2;
static const int32_t k_legacy_service_socket = 3;
static const int k_snapshot_retry_limit = 4;

zlink_service_type_t translate_service_type(uint32_t service_type)
{
    switch (service_type) {
    case k_legacy_service_spot:
    case ZLINK_SERVICE_TYPE_SPOT:
        return ZLINK_SERVICE_TYPE_SPOT;
    case k_legacy_service_socket:
    case ZLINK_SERVICE_TYPE_SOCKET:
        return ZLINK_SERVICE_TYPE_SOCKET;
    default:
        return static_cast<zlink_service_type_t>(service_type);
    }
}

napi_value unsupported_receiver(napi_env env, const char *method)
{
    napi_throw_error(env, NULL, method);
    return NULL;
}

int discovery_member_peer_count(void *discovery)
{
    size_t count = 0;
    int rc = zlink_discovery_member_peers(discovery, NULL, &count);
    if (rc != 0)
        return -1;
    return static_cast<int>(count);
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

bool parse_routing_id(napi_env env,
                      napi_value value,
                      zlink_routing_id_t *routing_id)
{
    void *data = NULL;
    size_t len = 0;
    if (napi_get_buffer_info(env, value, &data, &len) != napi_ok) {
        napi_throw_type_error(env, NULL, "routingId must be Buffer");
        return false;
    }
    if (len == 0 || len > 255) {
        napi_throw_range_error(env, NULL, "routingId length must be 1..255 bytes");
        return false;
    }
    memset(routing_id, 0, sizeof(*routing_id));
    routing_id->size = static_cast<uint8_t>(len);
    memcpy(routing_id->data, data, len);
    return true;
}

napi_value create_member_peer_array(napi_env env,
                                    zlink_member_peer_entry_t *entries,
                                    size_t count)
{
    napi_value arr;
    napi_create_array_with_length(env, count, &arr);
    for (size_t i = 0; i < count; ++i) {
        napi_value obj;
        napi_create_object(env, &obj);
        set_uint32_property(env, obj, "serviceType",
                            static_cast<uint32_t>(entries[i].service_type));
        set_uint32_property(env, obj, "serviceRole", entries[i].service_role);
        set_string_property(env, obj, "serviceName", entries[i].service_name);
        set_string_property(env, obj, "endpoint", entries[i].endpoint);
        napi_value rid = create_routing_id_value(env, entries[i].routing_id);
        napi_set_named_property(env, obj, "routingId", rid);
        set_uint32_property(env, obj, "weight",
                            static_cast<uint32_t>(entries[i].weight));
        set_int64_property(env, obj, "value", entries[i].value);
        napi_set_element(env, arr, static_cast<uint32_t>(i), obj);
    }
    return arr;
}

napi_value create_registry_topology_array(napi_env env,
                                          zlink_registry_topology_entry_t *entries,
                                          size_t count)
{
    napi_value arr;
    napi_create_array_with_length(env, count, &arr);
    for (size_t i = 0; i < count; ++i) {
        napi_value obj;
        napi_create_object(env, &obj);
        napi_value rid = create_routing_id_value(env, entries[i].routing_id);
        napi_set_named_property(env, obj, "routingId", rid);
        set_uint32_property(env, obj, "serviceKind",
                            static_cast<uint32_t>(entries[i].service_kind));
        set_uint32_property(env, obj, "serviceRole", entries[i].service_role);
        set_string_property(env, obj, "serviceName", entries[i].service_name);
        set_string_property(env, obj, "endpoint", entries[i].endpoint);
        set_uint32_property(env, obj, "source",
                            static_cast<uint32_t>(entries[i].source));
        set_uint32_property(env, obj, "state",
                            static_cast<uint32_t>(entries[i].state));
        set_uint32_property(env, obj, "desiredCount", entries[i].desired_count);
        set_uint32_property(env, obj, "readyCount", entries[i].ready_count);
        set_uint32_property(env, obj, "errorCode", entries[i].error_code);
        set_int64_property(env, obj, "lastReportedMs",
                           static_cast<int64_t>(entries[i].last_reported_ms));
        napi_set_element(env, arr, static_cast<uint32_t>(i), obj);
    }
    return arr;
}

bool build_topology_filter(napi_env env,
                           napi_value value,
                           zlink_registry_topology_filter_t *out)
{
    memset(out, 0, sizeof(*out));
    if (value == NULL)
        return false;
    napi_valuetype type = napi_undefined;
    if (napi_typeof(env, value, &type) != napi_ok
        || type == napi_undefined || type == napi_null) {
        return false;
    }

    napi_value prop;
    bool has_prop = false;
    if (napi_has_named_property(env, value, "serviceKind", &has_prop) == napi_ok
        && has_prop
        && napi_get_named_property(env, value, "serviceKind", &prop) == napi_ok) {
        uint32_t raw = 0;
        napi_get_value_uint32(env, prop, &raw);
        out->service_kind = static_cast<zlink_service_kind_t>(raw);
    }
    if (napi_has_named_property(env, value, "serviceRole", &has_prop) == napi_ok
        && has_prop
        && napi_get_named_property(env, value, "serviceRole", &prop) == napi_ok) {
        uint32_t raw = 0;
        napi_get_value_uint32(env, prop, &raw);
        out->service_role = static_cast<zlink_service_role_t>(raw);
    }
    if (napi_has_named_property(env, value, "serviceName", &has_prop) == napi_ok
        && has_prop
        && napi_get_named_property(env, value, "serviceName", &prop) == napi_ok) {
        std::string service_name = get_string(env, prop);
        strncpy(out->service_name, service_name.c_str(),
                sizeof(out->service_name) - 1);
    }
    if (napi_has_named_property(env, value, "state", &has_prop) == napi_ok
        && has_prop
        && napi_get_named_property(env, value, "state", &prop) == napi_ok) {
        uint32_t raw = 0;
        napi_get_value_uint32(env, prop, &raw);
        out->state = static_cast<zlink_topology_state_t>(raw);
    }
    if (napi_has_named_property(env, value, "source", &has_prop) == napi_ok
        && has_prop
        && napi_get_named_property(env, value, "source", &prop) == napi_ok) {
        uint32_t raw = 0;
        napi_get_value_uint32(env, prop, &raw);
        out->source = static_cast<zlink_topology_source_t>(raw);
    }
    return true;
}

bool build_registry_service_summary_filter(napi_env env,
                                          napi_value value,
                                          zlink_registry_service_summary_filter_t *out)
{
    memset(out, 0, sizeof(*out));
    if (value == NULL)
        return false;
    napi_valuetype type = napi_undefined;
    if (napi_typeof(env, value, &type) != napi_ok
        || type == napi_undefined || type == napi_null) {
        return false;
    }

    napi_value prop;
    bool has_prop = false;
    if (napi_has_named_property(env, value, "serviceKind", &has_prop) == napi_ok
        && has_prop
        && napi_get_named_property(env, value, "serviceKind", &prop) == napi_ok) {
        uint32_t raw = 0;
        napi_get_value_uint32(env, prop, &raw);
        out->service_kind = static_cast<zlink_service_kind_t>(raw);
    }
    if (napi_has_named_property(env, value, "serviceRole", &has_prop) == napi_ok
        && has_prop
        && napi_get_named_property(env, value, "serviceRole", &prop) == napi_ok) {
        uint32_t raw = 0;
        napi_get_value_uint32(env, prop, &raw);
        out->service_role = static_cast<zlink_service_role_t>(raw);
    }
    if (napi_has_named_property(env, value, "serviceName", &has_prop) == napi_ok
        && has_prop
        && napi_get_named_property(env, value, "serviceName", &prop) == napi_ok) {
        std::string service_name = get_string(env, prop);
        strncpy(out->service_name, service_name.c_str(),
                sizeof(out->service_name) - 1);
    }
    return true;
}

} // namespace

napi_value registry_new(napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *ctx = NULL;
    napi_get_value_external(env, argv[0], &ctx);
    void *reg = zlink_registry_new(ctx);
    if (!reg)
        return throw_last_error(env, "registry_new failed");
    napi_value ext;
    napi_create_external(env, reg, NULL, NULL, &ext);
    return ext;
}

napi_value registry_set_endpoints(napi_env env, napi_callback_info info)
{
    napi_value argv[3];
    size_t argc = 3;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *reg = NULL;
    napi_get_value_external(env, argv[0], &reg);
    std::string pub = get_string(env, argv[1]);
    std::string router = get_string(env, argv[2]);
    int rc = zlink_registry_bind(reg, pub.c_str(), router.c_str());
    if (rc != 0)
        return throw_last_error(env, "registry_bind failed");
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

napi_value registry_set_id(napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *reg = NULL;
    napi_get_value_external(env, argv[0], &reg);
    uint32_t id = 0;
    napi_get_value_uint32(env, argv[1], &id);
    int rc = zlink_registry_set_id(reg, id);
    if (rc != 0)
        return throw_last_error(env, "registry_set_id failed");
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

napi_value registry_add_peer(napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *reg = NULL;
    napi_get_value_external(env, argv[0], &reg);
    std::string pub = get_string(env, argv[1]);
    int rc = zlink_registry_add_peer(reg, pub.c_str());
    if (rc != 0)
        return throw_last_error(env, "registry_add_peer failed");
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

napi_value registry_set_heartbeat(napi_env env, napi_callback_info info)
{
    napi_value argv[3];
    size_t argc = 3;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *reg = NULL;
    napi_get_value_external(env, argv[0], &reg);
    uint32_t interval = 0;
    uint32_t timeout = 0;
    napi_get_value_uint32(env, argv[1], &interval);
    napi_get_value_uint32(env, argv[2], &timeout);
    int rc = zlink_registry_set_heartbeat(reg, interval, timeout);
    if (rc != 0)
        return throw_last_error(env, "registry_set_heartbeat failed");
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

napi_value registry_set_broadcast(napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *reg = NULL;
    napi_get_value_external(env, argv[0], &reg);
    uint32_t interval = 0;
    napi_get_value_uint32(env, argv[1], &interval);
    int rc = zlink_registry_set_broadcast_interval(reg, interval);
    if (rc != 0)
        return throw_last_error(env, "registry_set_broadcast_interval failed");
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

napi_value registry_start(napi_env env, napi_callback_info info)
{
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

napi_value registry_destroy(napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *reg = NULL;
    napi_get_value_external(env, argv[0], &reg);
    void *tmp = reg;
    int rc = zlink_registry_destroy(&tmp);
    if (rc != 0)
        return throw_last_error(env, "registry_destroy failed");
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

napi_value registry_status_snapshot(napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *registry = NULL;
    napi_get_value_external(env, argv[0], &registry);

    zlink_registry_status_t status;
    memset(&status, 0, sizeof(status));
    int rc = zlink_registry_status_snapshot(registry, &status);
    if (rc != 0)
        return throw_last_error(env, "registry_status_snapshot failed");

    napi_value obj;
    napi_create_object(env, &obj);
    set_uint32_property(env, obj, "registryId", status.registry_id);
    set_string_property(env, obj, "bindEndpoint", status.bind_endpoint);
    set_uint32_property(env, obj, "state", static_cast<uint32_t>(status.state));
    set_uint32_property(env, obj, "topologyEntryCount", status.topology_entry_count);
    set_uint32_property(env, obj, "peerRegistryCount", status.peer_registry_count);
    set_uint32_property(env, obj, "connectedPeerRegistryCount",
                        status.connected_peer_registry_count);
    set_int64_property(env, obj, "listSeq", static_cast<int64_t>(status.list_seq));
    set_int64_property(env, obj, "lastError", status.last_error);
    set_int64_property(env, obj, "lastChangedMs",
                       static_cast<int64_t>(status.last_changed_ms));
    return obj;
}

napi_value registry_service_summary_snapshot(napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *registry = NULL;
    napi_get_value_external(env, argv[0], &registry);
    zlink_registry_service_summary_filter_t filter;
    zlink_registry_service_summary_filter_t *filter_ptr =
      build_registry_service_summary_filter(env, argc >= 2 ? argv[1] : NULL, &filter)
        ? &filter
        : NULL;

    for (int attempt = 0; attempt < k_snapshot_retry_limit; ++attempt) {
        size_t count = 0;
        int rc = zlink_registry_service_summary_snapshot(registry, filter_ptr, NULL, &count);
        if (rc != 0) {
            if (zlink_errno() == ENOBUFS && attempt + 1 < k_snapshot_retry_limit)
                continue;
            return throw_last_error(env, "registry_service_summary_snapshot failed");
        }

        napi_value arr;
        napi_create_array_with_length(env, count, &arr);
        if (count == 0)
            return arr;

        std::vector<zlink_registry_service_summary_entry_t> entries(count);
        rc = zlink_registry_service_summary_snapshot(registry, filter_ptr, entries.data(),
                                                    &count);
        if (rc != 0) {
            if (zlink_errno() == ENOBUFS && attempt + 1 < k_snapshot_retry_limit)
                continue;
            return throw_last_error(env, "registry_service_summary_snapshot failed");
        }

        for (size_t i = 0; i < count; ++i) {
            napi_value obj;
            napi_create_object(env, &obj);
            set_uint32_property(env, obj, "serviceKind",
                                static_cast<uint32_t>(entries[i].service_kind));
            set_uint32_property(env, obj, "serviceRole", entries[i].service_role);
            set_string_property(env, obj, "serviceName", entries[i].service_name);
            set_uint32_property(env, obj, "totalCount", entries[i].total_count);
            set_uint32_property(env, obj, "connectingCount", entries[i].connecting_count);
            set_uint32_property(env, obj, "readyCount", entries[i].ready_count);
            set_uint32_property(env, obj, "errorCount", entries[i].error_count);
            set_uint32_property(env, obj, "stoppedCount", entries[i].stopped_count);
            set_int64_property(env, obj, "lastReportedMs",
                               static_cast<int64_t>(entries[i].last_reported_ms));
            napi_set_element(env, arr, static_cast<uint32_t>(i), obj);
        }

        return arr;
    }

    return throw_last_error(env, "registry_service_summary_snapshot failed");
}

napi_value registry_topology_snapshot(napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *registry = NULL;
    napi_get_value_external(env, argv[0], &registry);

    for (int attempt = 0; attempt < k_snapshot_retry_limit; ++attempt) {
        size_t count = 0;
        int rc = zlink_registry_topology_snapshot(registry, NULL, &count);
        if (rc != 0) {
            if (zlink_errno() == ENOBUFS && attempt + 1 < k_snapshot_retry_limit)
                continue;
            return throw_last_error(env, "registry_topology_snapshot failed");
        }
        if (count == 0) {
            napi_value arr;
            napi_create_array_with_length(env, 0, &arr);
            return arr;
        }

        std::vector<zlink_registry_topology_entry_t> entries(count);
        rc = zlink_registry_topology_snapshot(registry, entries.data(), &count);
        if (rc != 0) {
            if (zlink_errno() == ENOBUFS && attempt + 1 < k_snapshot_retry_limit)
                continue;
            return throw_last_error(env, "registry_topology_snapshot failed");
        }
        return create_registry_topology_array(env, entries.data(), count);
    }

    return throw_last_error(env, "registry_topology_snapshot failed");
}

napi_value registry_topology_query(napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *registry = NULL;
    napi_get_value_external(env, argv[0], &registry);

    zlink_registry_topology_filter_t filter;
    zlink_registry_topology_filter_t *filter_ptr =
      build_topology_filter(env, argc >= 2 ? argv[1] : NULL, &filter) ? &filter
                                                                      : NULL;
    for (int attempt = 0; attempt < k_snapshot_retry_limit; ++attempt) {
        size_t count = 0;
        int rc = zlink_registry_topology_query(registry, filter_ptr, NULL, &count);
        if (rc != 0) {
            if (zlink_errno() == ENOBUFS && attempt + 1 < k_snapshot_retry_limit)
                continue;
            return throw_last_error(env, "registry_topology_query failed");
        }
        if (count == 0) {
            napi_value arr;
            napi_create_array_with_length(env, 0, &arr);
            return arr;
        }

        std::vector<zlink_registry_topology_entry_t> entries(count);
        rc = zlink_registry_topology_query(registry, filter_ptr, entries.data(), &count);
        if (rc != 0) {
            if (zlink_errno() == ENOBUFS && attempt + 1 < k_snapshot_retry_limit)
                continue;
            return throw_last_error(env, "registry_topology_query failed");
        }
        return create_registry_topology_array(env, entries.data(), count);
    }

    return throw_last_error(env, "registry_topology_query failed");
}

napi_value registry_member_peers(napi_env env, napi_callback_info info)
{
    napi_value argv[3];
    size_t argc = 3;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *registry = NULL;
    napi_get_value_external(env, argv[0], &registry);
    uint32_t service_type = 0;
    napi_get_value_uint32(env, argv[1], &service_type);
    std::string service_name;
    if (argc >= 3)
        service_name = get_string(env, argv[2]);

    for (int attempt = 0; attempt < k_snapshot_retry_limit; ++attempt) {
        size_t count = 0;
        int rc = zlink_registry_member_peers(registry, translate_service_type(service_type),
                                             service_name.c_str(), NULL, &count);
        if (rc != 0) {
            if (zlink_errno() == ENOBUFS && attempt + 1 < k_snapshot_retry_limit)
                continue;
            return throw_last_error(env, "registry_member_peers failed");
        }
        if (count == 0) {
            napi_value arr;
            napi_create_array_with_length(env, 0, &arr);
            return arr;
        }

        std::vector<zlink_member_peer_entry_t> entries(count);
        rc = zlink_registry_member_peers(registry, translate_service_type(service_type),
                                         service_name.c_str(), entries.data(), &count);
        if (rc != 0) {
            if (zlink_errno() == ENOBUFS && attempt + 1 < k_snapshot_retry_limit)
                continue;
            return throw_last_error(env, "registry_member_peers failed");
        }
        return create_member_peer_array(env, entries.data(), count);
    }

    return throw_last_error(env, "registry_member_peers failed");
}

napi_value registry_member_peer_metadata(napi_env env, napi_callback_info info)
{
    napi_value argv[5];
    size_t argc = 5;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *registry = NULL;
    napi_get_value_external(env, argv[0], &registry);
    uint32_t service_type = 0;
    uint32_t service_role = 0;
    napi_get_value_uint32(env, argv[1], &service_type);
    std::string service_name = get_string(env, argv[2]);
    napi_get_value_uint32(env, argv[3], &service_role);
    std::string endpoint = get_string(env, argv[4]);

    zlink_msg_t metadata;
    int rc = zlink_msg_init(&metadata);
    if (rc != 0)
        return throw_last_error(env, "zlink_msg_init failed");
    rc = zlink_registry_member_peer_metadata(
      registry, translate_service_type(service_type), service_name.c_str(),
      static_cast<uint16_t>(service_role), endpoint.c_str(), &metadata);
    if (rc != 0) {
        (void) zlink_msg_close(&metadata);
        return throw_last_error(env, "registry_member_peer_metadata failed");
    }

    napi_value out;
    napi_create_buffer_copy(env, zlink_msg_size(&metadata), zlink_msg_data(&metadata),
                            NULL, &out);
    (void) zlink_msg_close(&metadata);
    return out;
}

napi_value registry_query_client_new(napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *ctx = NULL;
    napi_get_value_external(env, argv[0], &ctx);
    void *client = zlink_registry_query_client_new(ctx);
    if (!client)
        return throw_last_error(env, "registry_query_client_new failed");
    napi_value ext;
    napi_create_external(env, client, NULL, NULL, &ext);
    return ext;
}

napi_value registry_query_client_connect(napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *client = NULL;
    napi_get_value_external(env, argv[0], &client);
    std::string endpoint = get_string(env, argv[1]);
    int rc = zlink_registry_query_client_connect(client, endpoint.c_str());
    if (rc != 0)
        return throw_last_error(env, "registry_query_client_connect failed");
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

napi_value registry_query_snapshot(napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *client = NULL;
    napi_get_value_external(env, argv[0], &client);

    zlink_registry_topology_filter_t filter;
    zlink_registry_topology_filter_t *filter_ptr =
      build_topology_filter(env, argc >= 2 ? argv[1] : NULL, &filter) ? &filter
                                                                      : NULL;

    for (int attempt = 0; attempt < k_snapshot_retry_limit; ++attempt) {
        size_t count = 0;
        int rc = zlink_registry_query_snapshot(client, filter_ptr, NULL, &count);
        if (rc != 0) {
            if (zlink_errno() == ENOBUFS && attempt + 1 < k_snapshot_retry_limit)
                continue;
            return throw_last_error(env, "registry_query_snapshot failed");
        }
        if (count == 0) {
            napi_value arr;
            napi_create_array_with_length(env, 0, &arr);
            return arr;
        }

        std::vector<zlink_registry_topology_entry_t> entries(count);
        rc = zlink_registry_query_snapshot(client, filter_ptr, entries.data(), &count);
        if (rc != 0) {
            if (zlink_errno() == ENOBUFS && attempt + 1 < k_snapshot_retry_limit)
                continue;
            return throw_last_error(env, "registry_query_snapshot failed");
        }
        return create_registry_topology_array(env, entries.data(), count);
    }

    return throw_last_error(env, "registry_query_snapshot failed");
}

napi_value registry_query_destroy(napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *client = NULL;
    napi_get_value_external(env, argv[0], &client);
    void *tmp = client;
    int rc = zlink_registry_query_destroy(&tmp);
    if (rc != 0)
        return throw_last_error(env, "registry_query_destroy failed");
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

napi_value discovery_new(napi_env env, napi_callback_info info)
{
    napi_value argv[3];
    size_t argc = 3;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *ctx = NULL;
    napi_get_value_external(env, argv[0], &ctx);
    uint32_t service_type = 0;
    napi_get_value_uint32(env, argv[1], &service_type);
    std::string service_name;
    if (argc >= 3) {
        napi_valuetype type = napi_undefined;
        napi_typeof(env, argv[2], &type);
        if (type != napi_undefined && type != napi_null)
            service_name = get_string(env, argv[2]);
    }
    void *disc = zlink_discovery_new(ctx, translate_service_type(service_type),
                                     service_name.c_str());
    if (!disc)
        return throw_last_error(env, "discovery_new failed");
    napi_value ext;
    napi_create_external(env, disc, NULL, NULL, &ext);
    return ext;
}

napi_value discovery_connect(napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *disc = NULL;
    napi_get_value_external(env, argv[0], &disc);
    std::string ep = get_string(env, argv[1]);
    int rc = zlink_discovery_connect_registry(disc, ep.c_str());
    if (rc != 0)
        return throw_last_error(env, "discovery_connect failed");
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

napi_value discovery_provider_count(napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *disc = NULL;
    napi_get_value_external(env, argv[0], &disc);
    int count = discovery_member_peer_count(disc);
    if (count < 0)
        return throw_last_error(env, "discovery_member_peers failed");
    napi_value out;
    napi_create_int32(env, count, &out);
    return out;
}

napi_value discovery_service_available(napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *disc = NULL;
    napi_get_value_external(env, argv[0], &disc);
    int count = discovery_member_peer_count(disc);
    if (count < 0)
        return throw_last_error(env, "discovery_member_peers failed");
    napi_value out;
    napi_get_boolean(env, count > 0, &out);
    return out;
}

napi_value discovery_get_providers(napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *disc = NULL;
    napi_get_value_external(env, argv[0], &disc);

    for (int attempt = 0; attempt < k_snapshot_retry_limit; ++attempt) {
        size_t count = 0;
        int rc = zlink_discovery_member_peers(disc, NULL, &count);
        if (rc != 0) {
            if (zlink_errno() == ENOBUFS && attempt + 1 < k_snapshot_retry_limit)
                continue;
            return throw_last_error(env, "discovery_member_peers failed");
        }

        napi_value arr;
        napi_create_array_with_length(env, count, &arr);
        if (count == 0)
            return arr;

        std::vector<zlink_member_peer_entry_t> entries(count);
        rc = zlink_discovery_member_peers(disc, entries.data(), &count);
        if (rc != 0) {
            if (zlink_errno() == ENOBUFS && attempt + 1 < k_snapshot_retry_limit)
                continue;
            return throw_last_error(env, "discovery_member_peers failed");
        }

        return create_member_peer_array(env, entries.data(), count);
    }

    return throw_last_error(env, "discovery_member_peers failed");
}

napi_value discovery_member_peer_metadata(napi_env env, napi_callback_info info)
{
    napi_value argv[3];
    size_t argc = 3;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *discovery = NULL;
    napi_get_value_external(env, argv[0], &discovery);
    uint32_t service_role = 0;
    napi_get_value_uint32(env, argv[1], &service_role);
    std::string endpoint = get_string(env, argv[2]);

    zlink_msg_t metadata;
    int rc = zlink_msg_init(&metadata);
    if (rc != 0)
        return throw_last_error(env, "zlink_msg_init failed");
    rc = zlink_discovery_member_peer_metadata(
      discovery, static_cast<uint16_t>(service_role), endpoint.c_str(), &metadata);
    if (rc != 0) {
        (void) zlink_msg_close(&metadata);
        return throw_last_error(env, "discovery_member_peer_metadata failed");
    }

    napi_value out;
    napi_create_buffer_copy(env, zlink_msg_size(&metadata), zlink_msg_data(&metadata),
                            NULL, &out);
    (void) zlink_msg_close(&metadata);
    return out;
}

napi_value discovery_set_value(napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *discovery = NULL;
    napi_get_value_external(env, argv[0], &discovery);
    int64_t value = 0;
    napi_get_value_int64(env, argv[1], &value);
    int rc = zlink_discovery_set_value(discovery, value);
    if (rc != 0)
        return throw_last_error(env, "discovery_set_value failed");
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

napi_value discovery_get_value(napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *discovery = NULL;
    napi_get_value_external(env, argv[0], &discovery);
    int64_t value = 0;
    int rc = zlink_discovery_get_value(discovery, &value);
    if (rc != 0)
        return throw_last_error(env, "discovery_get_value failed");
    napi_value out;
    napi_create_int64(env, value, &out);
    return out;
}

napi_value discovery_set_metadata(napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *discovery = NULL;
    napi_get_value_external(env, argv[0], &discovery);
    void *data = NULL;
    size_t len = 0;
    if (napi_get_buffer_info(env, argv[1], &data, &len) != napi_ok) {
      napi_throw_type_error(env, NULL, "metadata must be Buffer");
      return NULL;
    }
    int rc = zlink_discovery_set_metadata(discovery, data, len);
    if (rc != 0)
        return throw_last_error(env, "discovery_set_metadata failed");
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

napi_value discovery_get_metadata(napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *discovery = NULL;
    napi_get_value_external(env, argv[0], &discovery);

    zlink_msg_t metadata;
    int rc = zlink_msg_init(&metadata);
    if (rc != 0)
        return throw_last_error(env, "zlink_msg_init failed");
    rc = zlink_discovery_get_metadata(discovery, &metadata);
    if (rc != 0) {
        (void) zlink_msg_close(&metadata);
        return throw_last_error(env, "discovery_get_metadata failed");
    }

    napi_value out;
    napi_create_buffer_copy(env, zlink_msg_size(&metadata), zlink_msg_data(&metadata),
                            NULL, &out);
    (void) zlink_msg_close(&metadata);
    return out;
}

napi_value discovery_resolve_spot(napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *discovery = NULL;
    napi_get_value_external(env, argv[0], &discovery);
    zlink_routing_id_t spot_rid;
    if (!parse_routing_id(env, argv[1], &spot_rid))
        return NULL;
    zlink_routing_id_t owner_node_rid;
    memset(&owner_node_rid, 0, sizeof(owner_node_rid));
    int rc = zlink_discovery_resolve_spot(discovery, &spot_rid, &owner_node_rid);
    if (rc != 0)
        return throw_last_error(env, "discovery_resolve_spot failed");
    return create_routing_id_value(env, owner_node_rid);
}

napi_value discovery_set_dealer_peer_mode(napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *discovery = NULL;
    napi_get_value_external(env, argv[0], &discovery);
    uint32_t mode = 0;
    napi_get_value_uint32(env, argv[1], &mode);
    int rc = zlink_discovery_set_dealer_peer_mode(
      discovery, static_cast<zlink_discovery_dealer_peer_mode_t>(mode));
    if (rc != 0)
        return throw_last_error(env, "discovery_set_dealer_peer_mode failed");
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

napi_value discovery_destroy(napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *disc = NULL;
    napi_get_value_external(env, argv[0], &disc);
    void *tmp = disc;
    int rc = zlink_discovery_destroy(&tmp);
    if (rc != 0)
        return throw_last_error(env, "discovery_destroy failed");
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

napi_value provider_new(napi_env env, napi_callback_info info)
{
    (void) info;
    return unsupported_receiver(
      env, "Receiver is not available on the aligned public API");
}

napi_value provider_bind(napi_env env, napi_callback_info info)
{
    (void) info;
    return unsupported_receiver(
      env, "Receiver.bind is not available on the aligned public API");
}

napi_value provider_connect_registry(napi_env env, napi_callback_info info)
{
    (void) info;
    return unsupported_receiver(
      env,
      "Receiver.connectRegistry is not available on the aligned public API");
}

napi_value provider_register(napi_env env, napi_callback_info info)
{
    (void) info;
    return unsupported_receiver(
      env, "Receiver.register is not available on the aligned public API");
}

napi_value provider_update_weight(napi_env env, napi_callback_info info)
{
    (void) info;
    return unsupported_receiver(
      env,
      "Receiver.updateWeight is not available on the aligned public API");
}

napi_value provider_unregister(napi_env env, napi_callback_info info)
{
    (void) info;
    return unsupported_receiver(
      env, "Receiver.unregister is not available on the aligned public API");
}

napi_value provider_register_result(napi_env env, napi_callback_info info)
{
    (void) info;
    return unsupported_receiver(
      env,
      "Receiver.registerResult is not available on the aligned public API");
}

napi_value provider_set_tls_server(napi_env env, napi_callback_info info)
{
    (void) info;
    return unsupported_receiver(
      env,
      "Receiver.setTlsServer is not available on the aligned public API");
}

napi_value provider_router(napi_env env, napi_callback_info info)
{
    (void) info;
    return unsupported_receiver(
      env, "Receiver.routerSocket is not available on the aligned public API");
}

napi_value provider_router_peers(napi_env env, napi_callback_info info)
{
    (void) info;
    return unsupported_receiver(
      env, "Receiver.routerPeers is not available on the aligned public API");
}

napi_value provider_setsockopt(napi_env env, napi_callback_info info)
{
    (void) info;
    return unsupported_receiver(
      env, "Receiver.setSockOpt is not available on the aligned public API");
}

napi_value registry_setsockopt(napi_env env, napi_callback_info info)
{
    (void) info;
    napi_throw_error(env, NULL,
                     "Registry.setSockOpt is not available on the aligned public API");
    return NULL;
}

napi_value registry_set_tls_server(napi_env env, napi_callback_info info)
{
    napi_value argv[4];
    size_t argc = 4;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *registry = NULL;
    napi_get_value_external(env, argv[0], &registry);
    std::string cert = get_string(env, argv[1]);
    std::string key = get_string(env, argv[2]);
    int32_t require_client = 0;
    if (argc >= 4)
        napi_get_value_int32(env, argv[3], &require_client);
    int rc = zlink_set_tls_server(
      registry, cert.c_str(), key.c_str(), require_client);
    if (rc != 0)
        return throw_last_error(env, "registry_set_tls_server failed");
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

napi_value registry_set_tls_client(napi_env env, napi_callback_info info)
{
    napi_value argv[4];
    size_t argc = 4;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *registry = NULL;
    napi_get_value_external(env, argv[0], &registry);
    std::string ca = get_string(env, argv[1]);
    std::string host = get_string(env, argv[2]);
    int32_t trust_system = 0;
    if (argc >= 4)
        napi_get_value_int32(env, argv[3], &trust_system);
    int rc = zlink_set_tls_client(
      registry, ca.c_str(), host.c_str(), trust_system);
    if (rc != 0)
        return throw_last_error(env, "registry_set_tls_client failed");
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

napi_value discovery_set_tls_client(napi_env env, napi_callback_info info)
{
    napi_value argv[4];
    size_t argc = 4;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *discovery = NULL;
    napi_get_value_external(env, argv[0], &discovery);
    std::string ca = get_string(env, argv[1]);
    std::string host = get_string(env, argv[2]);
    int32_t trust_system = 0;
    if (argc >= 4)
        napi_get_value_int32(env, argv[3], &trust_system);
    int rc =
      zlink_set_tls_client(discovery, ca.c_str(), host.c_str(), trust_system);
    if (rc != 0)
        return throw_last_error(env, "discovery_set_tls_client failed");
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

napi_value discovery_set_tls_server(napi_env env, napi_callback_info info)
{
    napi_value argv[4];
    size_t argc = 4;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *discovery = NULL;
    napi_get_value_external(env, argv[0], &discovery);
    std::string cert = get_string(env, argv[1]);
    std::string key = get_string(env, argv[2]);
    int32_t require_client = 0;
    if (argc >= 4)
        napi_get_value_int32(env, argv[3], &require_client);
    int rc = zlink_set_tls_server(
      discovery, cert.c_str(), key.c_str(), require_client);
    if (rc != 0)
        return throw_last_error(env, "discovery_set_tls_server failed");
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

napi_value provider_destroy(napi_env env, napi_callback_info info)
{
    (void) info;
    return unsupported_receiver(
      env, "Receiver.close is not available on the aligned public API");
}
