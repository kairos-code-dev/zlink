/* SPDX-License-Identifier: MPL-2.0 */

#include "addon_api.h"

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

napi_value spot_node_connect_registry(napi_env env, napi_callback_info info)
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

napi_value spot_node_connect_peer(napi_env env, napi_callback_info info)
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

napi_value spot_node_disconnect_peer(napi_env env, napi_callback_info info)
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

napi_value spot_node_register(napi_env env, napi_callback_info info)
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

napi_value spot_node_unregister(napi_env env, napi_callback_info info)
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

napi_value spot_node_set_discovery(napi_env env, napi_callback_info info)
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

napi_value spot_node_set_tls_server(napi_env env, napi_callback_info info)
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
    int rc = zlink_spot_node_set_tls_client(node, ca.c_str(), host.c_str(), trust);
    if (rc != 0)
        return throw_last_error(env, "spot_node_set_tls_client failed");
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

napi_value spot_node_pub_socket(napi_env env, napi_callback_info info)
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

napi_value spot_node_sub_socket(napi_env env, napi_callback_info info)
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

struct spot_handle_t
{
    void *node;
    void *pub;
    void *sub;
};

static spot_handle_t *get_spot_handle(napi_env env, napi_value val)
{
    void *raw = NULL;
    napi_get_value_external(env, val, &raw);
    return static_cast<spot_handle_t *> (raw);
}

napi_value spot_new(napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *node = NULL;
    napi_get_value_external(env, argv[0], &node);

    spot_handle_t *spot = static_cast<spot_handle_t *> (malloc (sizeof (spot_handle_t)));
    if (!spot)
        return throw_last_error(env, "spot_new failed");
    spot->node = node;
    spot->pub = zlink_spot_pub_new(node);
    spot->sub = zlink_spot_sub_new(node);
    if (!spot->pub || !spot->sub) {
        if (spot->pub) {
            void *tmp = spot->pub;
            zlink_spot_pub_destroy(&tmp);
        }
        if (spot->sub) {
            void *tmp = spot->sub;
            zlink_spot_sub_destroy(&tmp);
        }
        free(spot);
        return throw_last_error(env, "spot_new failed");
    }

    napi_value ext;
    napi_create_external(env, spot, NULL, NULL, &ext);
    return ext;
}

napi_value spot_destroy(napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    spot_handle_t *spot = get_spot_handle(env, argv[0]);
    if (!spot)
        return throw_last_error(env, "spot_destroy failed");
    int rc = 0;
    if (spot->pub) {
        void *tmp = spot->pub;
        spot->pub = NULL;
        if (zlink_spot_pub_destroy(&tmp) != 0)
            rc = -1;
    }
    if (spot->sub) {
        void *tmp = spot->sub;
        spot->sub = NULL;
        if (zlink_spot_sub_destroy(&tmp) != 0)
            rc = -1;
    }
    free(spot);
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
    spot_handle_t *spot = get_spot_handle(env, argv[0]);
    std::string topic = get_string(env, argv[1]);
    std::vector<zlink_msg_t> parts;
    if (!build_msg_vector(env, argv[2], &parts))
        return NULL;
    int32_t flags = 0;
    napi_get_value_int32(env, argv[3], &flags);
    int rc = zlink_spot_pub_publish(spot ? spot->pub : NULL, topic.c_str(), parts.data(), parts.size(), flags);
    if (rc != 0)
        return throw_last_error(env, "spot_publish failed");
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

napi_value spot_subscribe(napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    spot_handle_t *spot = get_spot_handle(env, argv[0]);
    std::string topic = get_string(env, argv[1]);
    int rc = zlink_spot_sub_subscribe(spot ? spot->sub : NULL, topic.c_str());
    if (rc != 0)
        return throw_last_error(env, "spot_subscribe failed");
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

napi_value spot_subscribe_pattern(napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    spot_handle_t *spot = get_spot_handle(env, argv[0]);
    std::string pat = get_string(env, argv[1]);
    int rc = zlink_spot_sub_subscribe_pattern(spot ? spot->sub : NULL, pat.c_str());
    if (rc != 0)
        return throw_last_error(env, "spot_subscribe_pattern failed");
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

napi_value spot_unsubscribe(napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    spot_handle_t *spot = get_spot_handle(env, argv[0]);
    std::string topic = get_string(env, argv[1]);
    int rc = zlink_spot_sub_unsubscribe(spot ? spot->sub : NULL, topic.c_str());
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
    spot_handle_t *spot = get_spot_handle(env, argv[0]);
    int32_t flags = 0;
    napi_get_value_int32(env, argv[1], &flags);
    zlink_msg_t *parts = NULL;
    size_t count = 0;
    char topic[256] = {0};
    size_t topic_len = 256;
    int rc = zlink_spot_sub_recv(spot ? spot->sub : NULL, &parts, &count, flags, topic, &topic_len);
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

napi_value spot_pub_socket(napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    spot_handle_t *spot = get_spot_handle(env, argv[0]);
    void *sock = (spot && spot->node) ? zlink_spot_node_pub_socket(spot->node) : NULL;
    if (!sock)
        return throw_last_error(env, "spot_pub_socket failed");
    napi_value ext;
    napi_create_external(env, sock, NULL, NULL, &ext);
    return ext;
}

napi_value spot_sub_socket(napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    spot_handle_t *spot = get_spot_handle(env, argv[0]);
    void *sock = zlink_spot_sub_socket(spot ? spot->sub : NULL);
    if (!sock)
        return throw_last_error(env, "spot_sub_socket failed");
    napi_value ext;
    napi_create_external(env, sock, NULL, NULL, &ext);
    return ext;
}
