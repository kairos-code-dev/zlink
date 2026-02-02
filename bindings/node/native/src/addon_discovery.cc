#include "addon_api.h"

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
    int rc = zlink_registry_set_endpoints(reg, pub.c_str(), router.c_str());
    if (rc != 0)
        return throw_last_error(env, "registry_set_endpoints failed");
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
    uint32_t id;
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

napi_value registry_set_broadcast(napi_env env, napi_callback_info info)
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

napi_value registry_start(napi_env env, napi_callback_info info)
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

napi_value discovery_new(napi_env env, napi_callback_info info)
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

napi_value discovery_subscribe(napi_env env, napi_callback_info info)
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

napi_value discovery_unsubscribe(napi_env env, napi_callback_info info)
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

napi_value discovery_provider_count(napi_env env, napi_callback_info info)
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

napi_value discovery_service_available(napi_env env, napi_callback_info info)
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

napi_value discovery_get_providers(napi_env env, napi_callback_info info)
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

napi_value gateway_new(napi_env env, napi_callback_info info)
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

napi_value gateway_send(napi_env env, napi_callback_info info)
{
    napi_value argv[4];
    size_t argc = 4;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *gw = NULL;
    napi_get_value_external(env, argv[0], &gw);
    std::string service = get_string(env, argv[1]);
    std::vector<zlink_msg_t> parts;
    if (!build_msg_vector(env, argv[2], &parts))
        return NULL;
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

napi_value gateway_recv(napi_env env, napi_callback_info info)
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

napi_value gateway_set_lb(napi_env env, napi_callback_info info)
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

napi_value gateway_set_tls(napi_env env, napi_callback_info info)
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

napi_value gateway_connection_count(napi_env env, napi_callback_info info)
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

napi_value gateway_destroy(napi_env env, napi_callback_info info)
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

napi_value provider_new(napi_env env, napi_callback_info info)
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

napi_value provider_bind(napi_env env, napi_callback_info info)
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

napi_value provider_connect_registry(napi_env env, napi_callback_info info)
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

napi_value provider_register(napi_env env, napi_callback_info info)
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

napi_value provider_update_weight(napi_env env, napi_callback_info info)
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

napi_value provider_unregister(napi_env env, napi_callback_info info)
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

napi_value provider_register_result(napi_env env, napi_callback_info info)
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

napi_value provider_set_tls_server(napi_env env, napi_callback_info info)
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

napi_value provider_router(napi_env env, napi_callback_info info)
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

napi_value provider_destroy(napi_env env, napi_callback_info info)
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
