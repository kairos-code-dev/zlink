#include <node_api.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <string>

#include "zlink.h"

static napi_value throw_last_error(napi_env env, const char *prefix)
{
    int err = zlink_errno();
    const char *msg = zlink_strerror(err);
    char buf[256];
    snprintf(buf, sizeof(buf), "%s: %s", prefix, msg ? msg : "error");
    napi_throw_error(env, NULL, buf);
    return NULL;
}

static std::string get_string(napi_env env, napi_value val)
{
    size_t len = 0;
    napi_get_value_string_utf8(env, val, NULL, 0, &len);
    std::string out(len, '\0');
    napi_get_value_string_utf8(env, val, out.data(), len + 1, &len);
    return out;
}

static napi_value version(napi_env env, napi_callback_info info)
{
    int major = 0, minor = 0, patch = 0;
    zlink_version(&major, &minor, &patch);
    napi_value arr;
    napi_create_array_with_length(env, 3, &arr);
    napi_value v0, v1, v2;
    napi_create_int32(env, major, &v0);
    napi_create_int32(env, minor, &v1);
    napi_create_int32(env, patch, &v2);
    napi_set_element(env, arr, 0, v0);
    napi_set_element(env, arr, 1, v1);
    napi_set_element(env, arr, 2, v2);
    return arr;
}

static napi_value ctx_new(napi_env env, napi_callback_info info)
{
    void *ctx = zlink_ctx_new();
    if (!ctx)
        return throw_last_error(env, "ctx_new failed");
    napi_value ext;
    napi_create_external(env, ctx, NULL, NULL, &ext);
    return ext;
}

static napi_value ctx_term(napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *ctx = NULL;
    napi_get_value_external(env, argv[0], &ctx);
    int rc = zlink_ctx_term(ctx);
    if (rc != 0)
        return throw_last_error(env, "ctx_term failed");
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

static napi_value socket_new(napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *ctx = NULL;
    int32_t type = 0;
    napi_get_value_external(env, argv[0], &ctx);
    napi_get_value_int32(env, argv[1], &type);
    void *sock = zlink_socket(ctx, type);
    if (!sock)
        return throw_last_error(env, "socket failed");
    napi_value ext;
    napi_create_external(env, sock, NULL, NULL, &ext);
    return ext;
}

static napi_value socket_close(napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *sock = NULL;
    napi_get_value_external(env, argv[0], &sock);
    int rc = zlink_close(sock);
    if (rc != 0)
        return throw_last_error(env, "close failed");
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

static napi_value socket_bind(napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *sock = NULL;
    napi_get_value_external(env, argv[0], &sock);
    std::string addr = get_string(env, argv[1]);
    int rc = zlink_bind(sock, addr.c_str());
    if (rc != 0)
        return throw_last_error(env, "bind failed");
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

static napi_value socket_connect(napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *sock = NULL;
    napi_get_value_external(env, argv[0], &sock);
    std::string addr = get_string(env, argv[1]);
    int rc = zlink_connect(sock, addr.c_str());
    if (rc != 0)
        return throw_last_error(env, "connect failed");
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

static napi_value socket_send(napi_env env, napi_callback_info info)
{
    napi_value argv[3];
    size_t argc = 3;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *sock = NULL;
    napi_get_value_external(env, argv[0], &sock);
    void *data;
    size_t len;
    napi_get_buffer_info(env, argv[1], &data, &len);
    int32_t flags = 0;
    napi_get_value_int32(env, argv[2], &flags);
    int rc = zlink_send(sock, data, len, flags);
    if (rc != 0)
        return throw_last_error(env, "send failed");
    napi_value out;
    napi_create_int32(env, rc, &out);
    return out;
}

static napi_value socket_recv(napi_env env, napi_callback_info info)
{
    napi_value argv[3];
    size_t argc = 3;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *sock = NULL;
    napi_get_value_external(env, argv[0], &sock);
    int32_t size = 0;
    int32_t flags = 0;
    napi_get_value_int32(env, argv[1], &size);
    napi_get_value_int32(env, argv[2], &flags);
    if (size <= 0)
        size = 1;
    void *buf = NULL;
    napi_value buffer;
    napi_create_buffer(env, size, &buf, &buffer);
    int rc = zlink_recv(sock, buf, size, flags);
    if (rc < 0)
        return throw_last_error(env, "recv failed");
    if (rc == size)
        return buffer;
    napi_value out;
    napi_create_buffer_copy(env, rc, buf, NULL, &out);
    return out;
}

static napi_value registry_new(napi_env env, napi_callback_info info)
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

static napi_value registry_set_endpoints(napi_env env, napi_callback_info info)
{
    napi_value argv[3];
    size_t argc = 3;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *reg = NULL;
    napi_get_value_external(env, argv[0], &reg);
    std::string pub = get_string(env, argv[1]);
    std::string router = get_string(env, argv[2]);
    int rc = zlink_registry_set_endpoints(reg, pub.c_str(), router.c_str());
    if (rc != 0)
        return throw_last_error(env, "registry_set_endpoints failed");
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

static napi_value registry_set_id(napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *reg = NULL;
    napi_get_value_external(env, argv[0], &reg);
    uint32_t id;
    napi_get_value_uint32(env, argv[1], &id);
    int rc = zlink_registry_set_id(reg, id);
    if (rc != 0)
        return throw_last_error(env, "registry_set_id failed");
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

static napi_value registry_add_peer(napi_env env, napi_callback_info info)
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

static napi_value registry_set_heartbeat(napi_env env, napi_callback_info info)
{
    napi_value argv[3];
    size_t argc = 3;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *reg = NULL;
    napi_get_value_external(env, argv[0], &reg);
    uint32_t interval, timeout;
    napi_get_value_uint32(env, argv[1], &interval);
    napi_get_value_uint32(env, argv[2], &timeout);
    int rc = zlink_registry_set_heartbeat(reg, interval, timeout);
    if (rc != 0)
        return throw_last_error(env, "registry_set_heartbeat failed");
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

static napi_value registry_set_broadcast(napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *reg = NULL;
    napi_get_value_external(env, argv[0], &reg);
    uint32_t interval;
    napi_get_value_uint32(env, argv[1], &interval);
    int rc = zlink_registry_set_broadcast_interval(reg, interval);
    if (rc != 0)
        return throw_last_error(env, "registry_set_broadcast_interval failed");
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

static napi_value registry_start(napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *reg = NULL;
    napi_get_value_external(env, argv[0], &reg);
    int rc = zlink_registry_start(reg);
    if (rc != 0)
        return throw_last_error(env, "registry_start failed");
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

static napi_value registry_destroy(napi_env env, napi_callback_info info)
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

static napi_value discovery_new(napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *ctx = NULL;
    napi_get_value_external(env, argv[0], &ctx);
    void *disc = zlink_discovery_new(ctx);
    if (!disc)
        return throw_last_error(env, "discovery_new failed");
    napi_value ext;
    napi_create_external(env, disc, NULL, NULL, &ext);
    return ext;
}

static napi_value discovery_connect(napi_env env, napi_callback_info info)
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

static napi_value discovery_subscribe(napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *disc = NULL;
    napi_get_value_external(env, argv[0], &disc);
    std::string service = get_string(env, argv[1]);
    int rc = zlink_discovery_subscribe(disc, service.c_str());
    if (rc != 0)
        return throw_last_error(env, "discovery_subscribe failed");
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

static napi_value discovery_unsubscribe(napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *disc = NULL;
    napi_get_value_external(env, argv[0], &disc);
    std::string service = get_string(env, argv[1]);
    int rc = zlink_discovery_unsubscribe(disc, service.c_str());
    if (rc != 0)
        return throw_last_error(env, "discovery_unsubscribe failed");
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

static napi_value discovery_provider_count(napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *disc = NULL;
    napi_get_value_external(env, argv[0], &disc);
    std::string service = get_string(env, argv[1]);
    int rc = zlink_discovery_provider_count(disc, service.c_str());
    if (rc < 0)
        return throw_last_error(env, "discovery_provider_count failed");
    napi_value out;
    napi_create_int32(env, rc, &out);
    return out;
}

static napi_value discovery_service_available(napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *disc = NULL;
    napi_get_value_external(env, argv[0], &disc);
    std::string service = get_string(env, argv[1]);
    int rc = zlink_discovery_service_available(disc, service.c_str());
    if (rc < 0)
        return throw_last_error(env, "discovery_service_available failed");
    napi_value out;
    napi_get_boolean(env, rc != 0, &out);
    return out;
}

static napi_value discovery_get_providers(napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *disc = NULL;
    napi_get_value_external(env, argv[0], &disc);
    std::string service = get_string(env, argv[1]);
    int count = zlink_discovery_provider_count(disc, service.c_str());
    if (count <= 0) {
        napi_value arr;
        napi_create_array_with_length(env, 0, &arr);
        return arr;
    }
    std::vector<zlink_provider_info_t> providers(count);
    size_t n = count;
    int rc = zlink_discovery_get_providers(disc, service.c_str(), providers.data(), &n);
    if (rc != 0)
        return throw_last_error(env, "discovery_get_providers failed");
    napi_value arr;
    napi_create_array_with_length(env, n, &arr);
    for (size_t i = 0; i < n; i++) {
        napi_value obj;
        napi_create_object(env, &obj);
        napi_value svc, ep, weight, reg;
        napi_create_string_utf8(env, providers[i].service_name, NAPI_AUTO_LENGTH, &svc);
        napi_create_string_utf8(env, providers[i].endpoint, NAPI_AUTO_LENGTH, &ep);
        napi_create_uint32(env, providers[i].weight, &weight);
        napi_create_int64(env, (int64_t)providers[i].registered_at, &reg);
        napi_set_named_property(env, obj, "serviceName", svc);
        napi_set_named_property(env, obj, "endpoint", ep);
        napi_set_named_property(env, obj, "weight", weight);
        napi_set_named_property(env, obj, "registeredAt", reg);
        napi_set_element(env, arr, i, obj);
    }
    return arr;
}

static napi_value discovery_destroy(napi_env env, napi_callback_info info)
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

static napi_value gateway_new(napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *ctx = NULL;
    void *disc = NULL;
    napi_get_value_external(env, argv[0], &ctx);
    napi_get_value_external(env, argv[1], &disc);
    void *gw = zlink_gateway_new(ctx, disc);
    if (!gw)
        return throw_last_error(env, "gateway_new failed");
    napi_value ext;
    napi_create_external(env, gw, NULL, NULL, &ext);
    return ext;
}

static std::vector<zlink_msg_t> build_msg_vector(napi_env env, napi_value arr)
{
    uint32_t len = 0;
    napi_get_array_length(env, arr, &len);
    std::vector<zlink_msg_t> parts(len);
    for (uint32_t i = 0; i < len; i++) {
        napi_value val;
        napi_get_element(env, arr, i, &val);
        void *data = NULL;
        size_t sz = 0;
        napi_get_buffer_info(env, val, &data, &sz);
        zlink_msg_init_size(&parts[i], sz);
        memcpy(zlink_msg_data(&parts[i]), data, sz);
    }
    return parts;
}

static void close_msg_vector(std::vector<zlink_msg_t> &parts)
{
    for (size_t i = 0; i < parts.size(); i++)
        zlink_msg_close(&parts[i]);
}

static napi_value gateway_send(napi_env env, napi_callback_info info)
{
    napi_value argv[4];
    size_t argc = 4;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *gw = NULL;
    napi_get_value_external(env, argv[0], &gw);
    std::string service = get_string(env, argv[1]);
    auto parts = build_msg_vector(env, argv[2]);
    int32_t flags = 0;
    napi_get_value_int32(env, argv[3], &flags);
    int rc = zlink_gateway_send(gw, service.c_str(), parts.data(), parts.size(), flags);
    close_msg_vector(parts);
    if (rc != 0)
        return throw_last_error(env, "gateway_send failed");
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

static napi_value gateway_recv(napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *gw = NULL;
    napi_get_value_external(env, argv[0], &gw);
    int32_t flags = 0;
    napi_get_value_int32(env, argv[1], &flags);
    zlink_msg_t *parts = NULL;
    size_t count = 0;
    char service[256] = {0};
    int rc = zlink_gateway_recv(gw, &parts, &count, flags, service);
    if (rc != 0)
        return throw_last_error(env, "gateway_recv failed");
    napi_value arr;
    napi_create_array_with_length(env, count, &arr);
    for (size_t i = 0; i < count; i++) {
        size_t sz = zlink_msg_size(&parts[i]);
        void *data = zlink_msg_data(&parts[i]);
        napi_value buf;
        napi_create_buffer_copy(env, sz, data, NULL, &buf);
        napi_set_element(env, arr, i, buf);
    }
    zlink_msgv_close(parts, count);
    napi_value obj;
    napi_create_object(env, &obj);
    napi_value svc;
    napi_create_string_utf8(env, service, NAPI_AUTO_LENGTH, &svc);
    napi_set_named_property(env, obj, "service", svc);
    napi_set_named_property(env, obj, "parts", arr);
    return obj;
}

static napi_value gateway_set_lb(napi_env env, napi_callback_info info)
{
    napi_value argv[3];
    size_t argc = 3;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *gw = NULL;
    napi_get_value_external(env, argv[0], &gw);
    std::string service = get_string(env, argv[1]);
    int32_t strat = 0;
    napi_get_value_int32(env, argv[2], &strat);
    int rc = zlink_gateway_set_lb_strategy(gw, service.c_str(), strat);
    if (rc != 0)
        return throw_last_error(env, "gateway_set_lb_strategy failed");
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

static napi_value gateway_set_tls(napi_env env, napi_callback_info info)
{
    napi_value argv[4];
    size_t argc = 4;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *gw = NULL;
    napi_get_value_external(env, argv[0], &gw);
    std::string ca = get_string(env, argv[1]);
    std::string host = get_string(env, argv[2]);
    int32_t trust = 0;
    napi_get_value_int32(env, argv[3], &trust);
    int rc = zlink_gateway_set_tls_client(gw, ca.c_str(), host.c_str(), trust);
    if (rc != 0)
        return throw_last_error(env, "gateway_set_tls_client failed");
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

static napi_value gateway_connection_count(napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *gw = NULL;
    napi_get_value_external(env, argv[0], &gw);
    std::string service = get_string(env, argv[1]);
    int rc = zlink_gateway_connection_count(gw, service.c_str());
    if (rc < 0)
        return throw_last_error(env, "gateway_connection_count failed");
    napi_value out;
    napi_create_int32(env, rc, &out);
    return out;
}

static napi_value gateway_destroy(napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *gw = NULL;
    napi_get_value_external(env, argv[0], &gw);
    void *tmp = gw;
    int rc = zlink_gateway_destroy(&tmp);
    if (rc != 0)
        return throw_last_error(env, "gateway_destroy failed");
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

static napi_value provider_new(napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *ctx = NULL;
    napi_get_value_external(env, argv[0], &ctx);
    void *p = zlink_provider_new(ctx);
    if (!p)
        return throw_last_error(env, "provider_new failed");
    napi_value ext;
    napi_create_external(env, p, NULL, NULL, &ext);
    return ext;
}

static napi_value provider_bind(napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *p = NULL;
    napi_get_value_external(env, argv[0], &p);
    std::string ep = get_string(env, argv[1]);
    int rc = zlink_provider_bind(p, ep.c_str());
    if (rc != 0)
        return throw_last_error(env, "provider_bind failed");
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

static napi_value provider_connect_registry(napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *p = NULL;
    napi_get_value_external(env, argv[0], &p);
    std::string ep = get_string(env, argv[1]);
    int rc = zlink_provider_connect_registry(p, ep.c_str());
    if (rc != 0)
        return throw_last_error(env, "provider_connect_registry failed");
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

static napi_value provider_register(napi_env env, napi_callback_info info)
{
    napi_value argv[4];
    size_t argc = 4;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *p = NULL;
    napi_get_value_external(env, argv[0], &p);
    std::string service = get_string(env, argv[1]);
    std::string ep = get_string(env, argv[2]);
    uint32_t weight;
    napi_get_value_uint32(env, argv[3], &weight);
    int rc = zlink_provider_register(p, service.c_str(), ep.c_str(), weight);
    if (rc != 0)
        return throw_last_error(env, "provider_register failed");
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

static napi_value provider_update_weight(napi_env env, napi_callback_info info)
{
    napi_value argv[3];
    size_t argc = 3;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *p = NULL;
    napi_get_value_external(env, argv[0], &p);
    std::string service = get_string(env, argv[1]);
    uint32_t weight;
    napi_get_value_uint32(env, argv[2], &weight);
    int rc = zlink_provider_update_weight(p, service.c_str(), weight);
    if (rc != 0)
        return throw_last_error(env, "provider_update_weight failed");
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

static napi_value provider_unregister(napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *p = NULL;
    napi_get_value_external(env, argv[0], &p);
    std::string service = get_string(env, argv[1]);
    int rc = zlink_provider_unregister(p, service.c_str());
    if (rc != 0)
        return throw_last_error(env, "provider_unregister failed");
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

static napi_value provider_register_result(napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *p = NULL;
    napi_get_value_external(env, argv[0], &p);
    std::string service = get_string(env, argv[1]);
    int status = 0;
    char resolved[256] = {0};
    char error[256] = {0};
    int rc = zlink_provider_register_result(p, service.c_str(), &status, resolved, error);
    if (rc != 0)
        return throw_last_error(env, "provider_register_result failed");
    napi_value obj;
    napi_create_object(env, &obj);
    napi_value st, res, err;
    napi_create_int32(env, status, &st);
    napi_create_string_utf8(env, resolved, NAPI_AUTO_LENGTH, &res);
    napi_create_string_utf8(env, error, NAPI_AUTO_LENGTH, &err);
    napi_set_named_property(env, obj, "status", st);
    napi_set_named_property(env, obj, "resolvedEndpoint", res);
    napi_set_named_property(env, obj, "errorMessage", err);
    return obj;
}

static napi_value provider_set_tls_server(napi_env env, napi_callback_info info)
{
    napi_value argv[3];
    size_t argc = 3;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *p = NULL;
    napi_get_value_external(env, argv[0], &p);
    std::string cert = get_string(env, argv[1]);
    std::string key = get_string(env, argv[2]);
    int rc = zlink_provider_set_tls_server(p, cert.c_str(), key.c_str());
    if (rc != 0)
        return throw_last_error(env, "provider_set_tls_server failed");
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

static napi_value provider_router(napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *p = NULL;
    napi_get_value_external(env, argv[0], &p);
    void *sock = zlink_provider_router(p);
    if (!sock)
        return throw_last_error(env, "provider_router failed");
    napi_value ext;
    napi_create_external(env, sock, NULL, NULL, &ext);
    return ext;
}

static napi_value provider_destroy(napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *p = NULL;
    napi_get_value_external(env, argv[0], &p);
    void *tmp = p;
    int rc = zlink_provider_destroy(&tmp);
    if (rc != 0)
        return throw_last_error(env, "provider_destroy failed");
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

static napi_value spot_node_new(napi_env env, napi_callback_info info)
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

static napi_value spot_node_destroy(napi_env env, napi_callback_info info)
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

static napi_value spot_node_bind(napi_env env, napi_callback_info info)
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

static napi_value spot_node_connect_registry(napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *node = NULL;
    napi_get_value_external(env, argv[0], &node);
    std::string ep = get_string(env, argv[1]);
    int rc = zlink_spot_node_connect_registry(node, ep.c_str());
    if (rc != 0)
        return throw_last_error(env, "spot_node_connect_registry failed");
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

static napi_value spot_node_connect_peer(napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *node = NULL;
    napi_get_value_external(env, argv[0], &node);
    std::string ep = get_string(env, argv[1]);
    int rc = zlink_spot_node_connect_peer_pub(node, ep.c_str());
    if (rc != 0)
        return throw_last_error(env, "spot_node_connect_peer_pub failed");
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

static napi_value spot_node_disconnect_peer(napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *node = NULL;
    napi_get_value_external(env, argv[0], &node);
    std::string ep = get_string(env, argv[1]);
    int rc = zlink_spot_node_disconnect_peer_pub(node, ep.c_str());
    if (rc != 0)
        return throw_last_error(env, "spot_node_disconnect_peer_pub failed");
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

static napi_value spot_node_register(napi_env env, napi_callback_info info)
{
    napi_value argv[3];
    size_t argc = 3;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *node = NULL;
    napi_get_value_external(env, argv[0], &node);
    std::string service = get_string(env, argv[1]);
    std::string ep = get_string(env, argv[2]);
    int rc = zlink_spot_node_register(node, service.c_str(), ep.c_str());
    if (rc != 0)
        return throw_last_error(env, "spot_node_register failed");
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

static napi_value spot_node_unregister(napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *node = NULL;
    napi_get_value_external(env, argv[0], &node);
    std::string service = get_string(env, argv[1]);
    int rc = zlink_spot_node_unregister(node, service.c_str());
    if (rc != 0)
        return throw_last_error(env, "spot_node_unregister failed");
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

static napi_value spot_node_set_discovery(napi_env env, napi_callback_info info)
{
    napi_value argv[3];
    size_t argc = 3;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *node = NULL;
    void *disc = NULL;
    napi_get_value_external(env, argv[0], &node);
    napi_get_value_external(env, argv[1], &disc);
    std::string service = get_string(env, argv[2]);
    int rc = zlink_spot_node_set_discovery(node, disc, service.c_str());
    if (rc != 0)
        return throw_last_error(env, "spot_node_set_discovery failed");
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

static napi_value spot_node_set_tls_server(napi_env env, napi_callback_info info)
{
    napi_value argv[3];
    size_t argc = 3;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *node = NULL;
    napi_get_value_external(env, argv[0], &node);
    std::string cert = get_string(env, argv[1]);
    std::string key = get_string(env, argv[2]);
    int rc = zlink_spot_node_set_tls_server(node, cert.c_str(), key.c_str());
    if (rc != 0)
        return throw_last_error(env, "spot_node_set_tls_server failed");
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

static napi_value spot_node_set_tls_client(napi_env env, napi_callback_info info)
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
    int rc = zlink_spot_node_set_tls_client(node, ca.c_str(), host.c_str(), trust);
    if (rc != 0)
        return throw_last_error(env, "spot_node_set_tls_client failed");
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

static napi_value spot_node_pub_socket(napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *node = NULL;
    napi_get_value_external(env, argv[0], &node);
    void *sock = zlink_spot_node_pub_socket(node);
    if (!sock)
        return throw_last_error(env, "spot_node_pub_socket failed");
    napi_value ext;
    napi_create_external(env, sock, NULL, NULL, &ext);
    return ext;
}

static napi_value spot_node_sub_socket(napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *node = NULL;
    napi_get_value_external(env, argv[0], &node);
    void *sock = zlink_spot_node_sub_socket(node);
    if (!sock)
        return throw_last_error(env, "spot_node_sub_socket failed");
    napi_value ext;
    napi_create_external(env, sock, NULL, NULL, &ext);
    return ext;
}

static napi_value spot_new(napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *node = NULL;
    napi_get_value_external(env, argv[0], &node);
    void *spot = zlink_spot_new(node);
    if (!spot)
        return throw_last_error(env, "spot_new failed");
    napi_value ext;
    napi_create_external(env, spot, NULL, NULL, &ext);
    return ext;
}

static napi_value spot_destroy(napi_env env, napi_callback_info info)
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

static napi_value spot_topic_create(napi_env env, napi_callback_info info)
{
    napi_value argv[3];
    size_t argc = 3;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *spot = NULL;
    napi_get_value_external(env, argv[0], &spot);
    std::string topic = get_string(env, argv[1]);
    int32_t mode = 0;
    napi_get_value_int32(env, argv[2], &mode);
    int rc = zlink_spot_topic_create(spot, topic.c_str(), mode);
    if (rc != 0)
        return throw_last_error(env, "spot_topic_create failed");
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

static napi_value spot_topic_destroy(napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *spot = NULL;
    napi_get_value_external(env, argv[0], &spot);
    std::string topic = get_string(env, argv[1]);
    int rc = zlink_spot_topic_destroy(spot, topic.c_str());
    if (rc != 0)
        return throw_last_error(env, "spot_topic_destroy failed");
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

static napi_value spot_publish(napi_env env, napi_callback_info info)
{
    napi_value argv[4];
    size_t argc = 4;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *spot = NULL;
    napi_get_value_external(env, argv[0], &spot);
    std::string topic = get_string(env, argv[1]);
    auto parts = build_msg_vector(env, argv[2]);
    int32_t flags = 0;
    napi_get_value_int32(env, argv[3], &flags);
    int rc = zlink_spot_publish(spot, topic.c_str(), parts.data(), parts.size(), flags);
    close_msg_vector(parts);
    if (rc != 0)
        return throw_last_error(env, "spot_publish failed");
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

static napi_value spot_subscribe(napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *spot = NULL;
    napi_get_value_external(env, argv[0], &spot);
    std::string topic = get_string(env, argv[1]);
    int rc = zlink_spot_subscribe(spot, topic.c_str());
    if (rc != 0)
        return throw_last_error(env, "spot_subscribe failed");
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

static napi_value spot_subscribe_pattern(napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *spot = NULL;
    napi_get_value_external(env, argv[0], &spot);
    std::string pat = get_string(env, argv[1]);
    int rc = zlink_spot_subscribe_pattern(spot, pat.c_str());
    if (rc != 0)
        return throw_last_error(env, "spot_subscribe_pattern failed");
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

static napi_value spot_unsubscribe(napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *spot = NULL;
    napi_get_value_external(env, argv[0], &spot);
    std::string topic = get_string(env, argv[1]);
    int rc = zlink_spot_unsubscribe(spot, topic.c_str());
    if (rc != 0)
        return throw_last_error(env, "spot_unsubscribe failed");
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

static napi_value spot_recv(napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *spot = NULL;
    napi_get_value_external(env, argv[0], &spot);
    int32_t flags = 0;
    napi_get_value_int32(env, argv[1], &flags);
    zlink_msg_t *parts = NULL;
    size_t count = 0;
    char topic[256] = {0};
    size_t topic_len = 256;
    int rc = zlink_spot_recv(spot, &parts, &count, flags, topic, &topic_len);
    if (rc != 0)
        return throw_last_error(env, "spot_recv failed");
    napi_value arr;
    napi_create_array_with_length(env, count, &arr);
    for (size_t i = 0; i < count; i++) {
        size_t sz = zlink_msg_size(&parts[i]);
        void *data = zlink_msg_data(&parts[i]);
        napi_value buf;
        napi_create_buffer_copy(env, sz, data, NULL, &buf);
        napi_set_element(env, arr, i, buf);
    }
    zlink_msgv_close(parts, count);
    napi_value obj;
    napi_create_object(env, &obj);
    napi_value t;
    napi_create_string_utf8(env, topic, NAPI_AUTO_LENGTH, &t);
    napi_set_named_property(env, obj, "topic", t);
    napi_set_named_property(env, obj, "parts", arr);
    return obj;
}

static napi_value spot_pub_socket(napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *spot = NULL;
    napi_get_value_external(env, argv[0], &spot);
    void *sock = zlink_spot_pub_socket(spot);
    if (!sock)
        return throw_last_error(env, "spot_pub_socket failed");
    napi_value ext;
    napi_create_external(env, sock, NULL, NULL, &ext);
    return ext;
}

static napi_value spot_sub_socket(napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *spot = NULL;
    napi_get_value_external(env, argv[0], &spot);
    void *sock = zlink_spot_sub_socket(spot);
    if (!sock)
        return throw_last_error(env, "spot_sub_socket failed");
    napi_value ext;
    napi_create_external(env, sock, NULL, NULL, &ext);
    return ext;
}

static napi_value monitor_open(napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *sock = NULL;
    napi_get_value_external(env, argv[0], &sock);
    int32_t events = 0;
    napi_get_value_int32(env, argv[1], &events);
    void *mon = zlink_socket_monitor_open(sock, events);
    if (!mon)
        return throw_last_error(env, "monitor_open failed");
    napi_value ext;
    napi_create_external(env, mon, NULL, NULL, &ext);
    return ext;
}

static napi_value monitor_recv(napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *mon = NULL;
    napi_get_value_external(env, argv[0], &mon);
    int32_t flags = 0;
    napi_get_value_int32(env, argv[1], &flags);
    zlink_monitor_event_t evt;
    int rc = zlink_monitor_recv(mon, &evt, flags);
    if (rc != 0)
        return throw_last_error(env, "monitor_recv failed");
    napi_value obj;
    napi_create_object(env, &obj);
    napi_value event, value, local, remote;
    napi_create_int64(env, (int64_t)evt.event, &event);
    napi_create_int64(env, (int64_t)evt.value, &value);
    napi_create_string_utf8(env, evt.local_addr, NAPI_AUTO_LENGTH, &local);
    napi_create_string_utf8(env, evt.remote_addr, NAPI_AUTO_LENGTH, &remote);
    napi_set_named_property(env, obj, "event", event);
    napi_set_named_property(env, obj, "value", value);
    napi_set_named_property(env, obj, "local", local);
    napi_set_named_property(env, obj, "remote", remote);
    return obj;
}

static napi_value poll(napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    napi_value arr = argv[0];
    int32_t timeout = 0;
    napi_get_value_int32(env, argv[1], &timeout);
    uint32_t len = 0;
    napi_get_array_length(env, arr, &len);
    if (len == 0) {
        napi_value out;
        napi_create_array_with_length(env, 0, &out);
        return out;
    }
    std::vector<zlink_pollitem_t> items(len);
    for (uint32_t i = 0; i < len; i++) {
        napi_value obj;
        napi_get_element(env, arr, i, &obj);
        napi_value sockVal, fdVal, evVal;
        napi_get_named_property(env, obj, "socket", &sockVal);
        napi_get_named_property(env, obj, "fd", &fdVal);
        napi_get_named_property(env, obj, "events", &evVal);
        void *sock = NULL;
        napi_get_value_external(env, sockVal, &sock);
        int32_t fd = 0;
        napi_get_value_int32(env, fdVal, &fd);
        int32_t ev = 0;
        napi_get_value_int32(env, evVal, &ev);
        items[i].socket = sock;
        items[i].fd = fd;
        items[i].events = (short)ev;
        items[i].revents = 0;
    }
    int rc = zlink_poll(items.data(), items.size(), timeout);
    if (rc < 0)
        return throw_last_error(env, "poll failed");
    napi_value out;
    napi_create_array_with_length(env, len, &out);
    for (uint32_t i = 0; i < len; i++) {
        napi_value v;
        napi_create_int32(env, items[i].revents, &v);
        napi_set_element(env, out, i, v);
    }
    return out;
}

static napi_value init(napi_env env, napi_value exports)
{
    napi_property_descriptor descs[] = {
        {"version", 0, version, 0, 0, 0, napi_default, 0},
        {"ctxNew", 0, ctx_new, 0, 0, 0, napi_default, 0},
        {"ctxTerm", 0, ctx_term, 0, 0, 0, napi_default, 0},
        {"socketNew", 0, socket_new, 0, 0, 0, napi_default, 0},
        {"socketClose", 0, socket_close, 0, 0, 0, napi_default, 0},
        {"socketBind", 0, socket_bind, 0, 0, 0, napi_default, 0},
        {"socketConnect", 0, socket_connect, 0, 0, 0, napi_default, 0},
        {"socketSend", 0, socket_send, 0, 0, 0, napi_default, 0},
        {"socketRecv", 0, socket_recv, 0, 0, 0, napi_default, 0},

        {"registryNew", 0, registry_new, 0, 0, 0, napi_default, 0},
        {"registrySetEndpoints", 0, registry_set_endpoints, 0, 0, 0, napi_default, 0},
        {"registrySetId", 0, registry_set_id, 0, 0, 0, napi_default, 0},
        {"registryAddPeer", 0, registry_add_peer, 0, 0, 0, napi_default, 0},
        {"registrySetHeartbeat", 0, registry_set_heartbeat, 0, 0, 0, napi_default, 0},
        {"registrySetBroadcastInterval", 0, registry_set_broadcast, 0, 0, 0, napi_default, 0},
        {"registryStart", 0, registry_start, 0, 0, 0, napi_default, 0},
        {"registryDestroy", 0, registry_destroy, 0, 0, 0, napi_default, 0},

        {"discoveryNew", 0, discovery_new, 0, 0, 0, napi_default, 0},
        {"discoveryConnectRegistry", 0, discovery_connect, 0, 0, 0, napi_default, 0},
        {"discoverySubscribe", 0, discovery_subscribe, 0, 0, 0, napi_default, 0},
        {"discoveryUnsubscribe", 0, discovery_unsubscribe, 0, 0, 0, napi_default, 0},
        {"discoveryProviderCount", 0, discovery_provider_count, 0, 0, 0, napi_default, 0},
        {"discoveryServiceAvailable", 0, discovery_service_available, 0, 0, 0, napi_default, 0},
        {"discoveryGetProviders", 0, discovery_get_providers, 0, 0, 0, napi_default, 0},
        {"discoveryDestroy", 0, discovery_destroy, 0, 0, 0, napi_default, 0},

        {"gatewayNew", 0, gateway_new, 0, 0, 0, napi_default, 0},
        {"gatewaySend", 0, gateway_send, 0, 0, 0, napi_default, 0},
        {"gatewayRecv", 0, gateway_recv, 0, 0, 0, napi_default, 0},
        {"gatewaySetLbStrategy", 0, gateway_set_lb, 0, 0, 0, napi_default, 0},
        {"gatewaySetTlsClient", 0, gateway_set_tls, 0, 0, 0, napi_default, 0},
        {"gatewayConnectionCount", 0, gateway_connection_count, 0, 0, 0, napi_default, 0},
        {"gatewayDestroy", 0, gateway_destroy, 0, 0, 0, napi_default, 0},

        {"providerNew", 0, provider_new, 0, 0, 0, napi_default, 0},
        {"providerBind", 0, provider_bind, 0, 0, 0, napi_default, 0},
        {"providerConnectRegistry", 0, provider_connect_registry, 0, 0, 0, napi_default, 0},
        {"providerRegister", 0, provider_register, 0, 0, 0, napi_default, 0},
        {"providerUpdateWeight", 0, provider_update_weight, 0, 0, 0, napi_default, 0},
        {"providerUnregister", 0, provider_unregister, 0, 0, 0, napi_default, 0},
        {"providerRegisterResult", 0, provider_register_result, 0, 0, 0, napi_default, 0},
        {"providerSetTlsServer", 0, provider_set_tls_server, 0, 0, 0, napi_default, 0},
        {"providerRouter", 0, provider_router, 0, 0, 0, napi_default, 0},
        {"providerDestroy", 0, provider_destroy, 0, 0, 0, napi_default, 0},

        {"spotNodeNew", 0, spot_node_new, 0, 0, 0, napi_default, 0},
        {"spotNodeDestroy", 0, spot_node_destroy, 0, 0, 0, napi_default, 0},
        {"spotNodeBind", 0, spot_node_bind, 0, 0, 0, napi_default, 0},
        {"spotNodeConnectRegistry", 0, spot_node_connect_registry, 0, 0, 0, napi_default, 0},
        {"spotNodeConnectPeerPub", 0, spot_node_connect_peer, 0, 0, 0, napi_default, 0},
        {"spotNodeDisconnectPeerPub", 0, spot_node_disconnect_peer, 0, 0, 0, napi_default, 0},
        {"spotNodeRegister", 0, spot_node_register, 0, 0, 0, napi_default, 0},
        {"spotNodeUnregister", 0, spot_node_unregister, 0, 0, 0, napi_default, 0},
        {"spotNodeSetDiscovery", 0, spot_node_set_discovery, 0, 0, 0, napi_default, 0},
        {"spotNodeSetTlsServer", 0, spot_node_set_tls_server, 0, 0, 0, napi_default, 0},
        {"spotNodeSetTlsClient", 0, spot_node_set_tls_client, 0, 0, 0, napi_default, 0},
        {"spotNodePubSocket", 0, spot_node_pub_socket, 0, 0, 0, napi_default, 0},
        {"spotNodeSubSocket", 0, spot_node_sub_socket, 0, 0, 0, napi_default, 0},

        {"spotNew", 0, spot_new, 0, 0, 0, napi_default, 0},
        {"spotDestroy", 0, spot_destroy, 0, 0, 0, napi_default, 0},
        {"spotTopicCreate", 0, spot_topic_create, 0, 0, 0, napi_default, 0},
        {"spotTopicDestroy", 0, spot_topic_destroy, 0, 0, 0, napi_default, 0},
        {"spotPublish", 0, spot_publish, 0, 0, 0, napi_default, 0},
        {"spotSubscribe", 0, spot_subscribe, 0, 0, 0, napi_default, 0},
        {"spotSubscribePattern", 0, spot_subscribe_pattern, 0, 0, 0, napi_default, 0},
        {"spotUnsubscribe", 0, spot_unsubscribe, 0, 0, 0, napi_default, 0},
        {"spotRecv", 0, spot_recv, 0, 0, 0, napi_default, 0},
        {"spotPubSocket", 0, spot_pub_socket, 0, 0, 0, napi_default, 0},
        {"spotSubSocket", 0, spot_sub_socket, 0, 0, 0, napi_default, 0},

        {"monitorOpen", 0, monitor_open, 0, 0, 0, napi_default, 0},
        {"monitorRecv", 0, monitor_recv, 0, 0, 0, napi_default, 0},
        {"poll", 0, poll, 0, 0, 0, napi_default, 0}
    };
    napi_define_properties(env, exports, sizeof(descs) / sizeof(descs[0]), descs);
    return exports;
}

NAPI_MODULE(NODE_GYP_MODULE_NAME, init)
