/* SPDX-License-Identifier: MPL-2.0 */

#include "addon_api.h"
#include <errno.h>

napi_value throw_last_error(napi_env env, const char *prefix)
{
    int err = zlink_errno();
    const char *msg = zlink_strerror(err);
    char buf[256];
    snprintf(buf, sizeof(buf), "%s: %s", prefix, msg ? msg : "error");
    napi_throw_error(env, NULL, buf);
    return NULL;
}

std::string get_string(napi_env env, napi_value val)
{
    size_t len = 0;
    napi_get_value_string_utf8(env, val, NULL, 0, &len);
    std::string out(len, '\0');
    napi_get_value_string_utf8(env, val, out.data(), len + 1, &len);
    return out;
}

bool build_msg_vector(napi_env env, napi_value arr,
                      std::vector<zlink_msg_t> *out)
{
    uint32_t len = 0;
    if (napi_get_array_length(env, arr, &len) != napi_ok) {
        napi_throw_type_error(env, NULL, "parts must be an array");
        return false;
    }
    out->clear();
    out->resize(len);
    size_t built = 0;
    for (uint32_t i = 0; i < len; i++) {
        napi_value val;
        if (napi_get_element(env, arr, i, &val) != napi_ok) {
            napi_throw_type_error(env, NULL, "parts element read failed");
            return false;
        }
        bool is_buf = false;
        if (napi_is_buffer(env, val, &is_buf) != napi_ok || !is_buf) {
            napi_throw_type_error(env, NULL, "parts must be Buffers");
            return false;
        }
        void *data = NULL;
        size_t sz = 0;
        if (napi_get_buffer_info(env, val, &data, &sz) != napi_ok) {
            napi_throw_type_error(env, NULL, "buffer info failed");
            return false;
        }
        if (zlink_msg_init_size(&(*out)[i], sz) != 0) {
            for (size_t j = 0; j < built; j++)
                zlink_msg_close(&(*out)[j]);
            return false;
        }
        if (sz > 0 && data)
            memcpy(zlink_msg_data(&(*out)[i]), data, sz);
        built++;
    }
    return true;
}

void close_msg_vector(std::vector<zlink_msg_t> &parts)
{
    for (size_t i = 0; i < parts.size(); i++)
        zlink_msg_close(&parts[i]);
}

napi_value version(napi_env env, napi_callback_info info)
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

napi_value ctx_new(napi_env env, napi_callback_info info)
{
    void *ctx = zlink_ctx_new();
    if (!ctx)
        return throw_last_error(env, "ctx_new failed");
    napi_value ext;
    napi_create_external(env, ctx, NULL, NULL, &ext);
    return ext;
}

napi_value ctx_term(napi_env env, napi_callback_info info)
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

napi_value socket_new(napi_env env, napi_callback_info info)
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

napi_value socket_close(napi_env env, napi_callback_info info)
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

napi_value socket_bind(napi_env env, napi_callback_info info)
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

napi_value socket_connect(napi_env env, napi_callback_info info)
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

napi_value socket_send(napi_env env, napi_callback_info info)
{
    napi_value argv[3];
    size_t argc = 3;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *sock = NULL;
    napi_get_value_external(env, argv[0], &sock);
    void *data;
    size_t len;
    if (napi_get_buffer_info(env, argv[1], &data, &len) != napi_ok) {
        napi_throw_type_error(env, NULL, "send buffer invalid");
        return NULL;
    }
    int32_t flags = 0;
    napi_get_value_int32(env, argv[2], &flags);
    int rc = zlink_send(sock, data, len, flags);
    if (rc < 0)
        return throw_last_error(env, "send failed");
    napi_value out;
    napi_create_int32(env, rc, &out);
    return out;
}

napi_value socket_recv(napi_env env, napi_callback_info info)
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

napi_value socket_setopt(napi_env env, napi_callback_info info)
{
    napi_value argv[3];
    size_t argc = 3;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *sock = NULL;
    napi_get_value_external(env, argv[0], &sock);
    int32_t opt = 0;
    napi_get_value_int32(env, argv[1], &opt);
    void *data = NULL;
    size_t len = 0;
    if (napi_get_buffer_info(env, argv[2], &data, &len) != napi_ok) {
        napi_throw_type_error(env, NULL, "option value must be Buffer");
        return NULL;
    }
    int rc = zlink_setsockopt(sock, opt, data, len);
    if (rc != 0)
        return throw_last_error(env, "setsockopt failed");
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

napi_value socket_getopt(napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *sock = NULL;
    napi_get_value_external(env, argv[0], &sock);
    int32_t opt = 0;
    napi_get_value_int32(env, argv[1], &opt);
    size_t len = 256;
    void *data = NULL;
    napi_value buf;
    napi_create_buffer(env, len, &data, &buf);
    int rc = zlink_getsockopt(sock, opt, data, &len);
    if (rc != 0) {
        int err = zlink_errno();
        if (err == EINVAL) {
            len = sizeof(int);
            napi_create_buffer(env, len, &data, &buf);
            rc = zlink_getsockopt(sock, opt, data, &len);
        }
    }
    if (rc != 0)
        return throw_last_error(env, "getsockopt failed");
    if (len == 256 || len == sizeof(int))
        return buf;
    napi_value out;
    napi_create_buffer_copy(env, len, data, NULL, &out);
    return out;
}

napi_value monitor_open(napi_env env, napi_callback_info info)
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

napi_value monitor_recv(napi_env env, napi_callback_info info)
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

napi_value poll(napi_env env, napi_callback_info info)
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
