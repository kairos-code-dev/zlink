/* SPDX-License-Identifier: MPL-2.0 */

#include "addon_api.h"
#include <algorithm>
#include <errno.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

namespace {

static const size_t k_stream_slot_count = 8;

struct stream_js_payload_t
{
    std::vector<unsigned char> routing_id;
    std::vector<std::vector<unsigned char> > packets;
};

struct stream_js_state_t
{
    stream_js_state_t () :
        used (false),
        socket (NULL),
        env (NULL),
        tsfn (NULL),
        stop_requested (0)
    {
    }

    bool used;
    void *socket;
    napi_env env;
    napi_threadsafe_function tsfn;
    std::atomic<int> stop_requested;
};

static std::mutex g_stream_slots_mu;
static stream_js_state_t g_stream_slots[k_stream_slot_count];

stream_js_state_t *find_stream_slot_by_socket_unsafe(void *socket)
{
    for (size_t i = 0; i < k_stream_slot_count; ++i) {
        if (g_stream_slots[i].used && g_stream_slots[i].socket == socket)
            return &g_stream_slots[i];
    }
    return NULL;
}

stream_js_state_t *find_free_stream_slot_unsafe()
{
    for (size_t i = 0; i < k_stream_slot_count; ++i) {
        if (!g_stream_slots[i].used)
            return &g_stream_slots[i];
    }
    return NULL;
}

void reset_stream_slot_unsafe(stream_js_state_t *state)
{
    if (!state)
        return;
    state->used = false;
    state->socket = NULL;
    state->env = NULL;
    state->tsfn = NULL;
    state->stop_requested.store(0, std::memory_order_release);
}

void stream_tsfn_finalize(napi_env env, void *finalize_data, void *finalize_hint)
{
    (void) env;
    (void) finalize_hint;
    stream_js_state_t *state = static_cast<stream_js_state_t *>(finalize_data);
    if (!state)
        return;
    std::lock_guard<std::mutex> lock(g_stream_slots_mu);
    reset_stream_slot_unsafe(state);
}

void stream_tsfn_call_js(napi_env env,
                         napi_value js_cb,
                         void *context,
                         void *data)
{
    std::unique_ptr<stream_js_payload_t> payload(
      static_cast<stream_js_payload_t *>(data));
    stream_js_state_t *state = static_cast<stream_js_state_t *>(context);
    if (!env || !js_cb || !state || !payload)
        return;

    napi_value argv[2];
    if (napi_create_buffer_copy(
          env, payload->routing_id.size (),
          payload->routing_id.empty () ? NULL : payload->routing_id.data (),
          NULL, &argv[0])
        != napi_ok) {
        return;
    }
    if (napi_create_array_with_length(env, payload->packets.size(), &argv[1])
        != napi_ok) {
        return;
    }
    for (size_t i = 0; i < payload->packets.size(); ++i) {
        const std::vector<unsigned char> &packet = payload->packets[i];
        napi_value packet_buf;
        if (napi_create_buffer_copy(
              env, packet.size(), packet.empty() ? NULL : packet.data(), NULL,
              &packet_buf)
            != napi_ok) {
            return;
        }
        napi_set_element(env, argv[1], static_cast<uint32_t>(i), packet_buf);
    }

    napi_value recv;
    napi_value this_arg;
    napi_get_undefined(env, &this_arg);
    napi_status call_status =
      napi_call_function(env, this_arg, js_cb, 2, argv, &recv);
    if (call_status != napi_ok) {
        state->stop_requested.store(1, std::memory_order_release);
        if (state->socket)
            (void) zlink_stream_detach(state->socket);
        return;
    }

    int32_t ret = 0;
    if (napi_get_value_int32(env, recv, &ret) == napi_ok && ret != 0) {
        state->stop_requested.store(1, std::memory_order_release);
        if (state->socket)
            (void) zlink_stream_detach(state->socket);
    }
}

template <size_t Slot>
int stream_on_packets_slot(const zlink_routing_id_t *rid_,
                           zlink_msg_t *msgs_,
                           size_t msg_count_)
{
    if (!rid_ || !msgs_ || msg_count_ == 0)
        return 0;

    stream_js_state_t *state = &g_stream_slots[Slot];
    napi_threadsafe_function tsfn = NULL;
    {
        std::lock_guard<std::mutex> lock(g_stream_slots_mu);
        if (!state->used || !state->tsfn)
            return 0;
        if (state->stop_requested.load(std::memory_order_acquire) != 0)
            return 1;
        tsfn = state->tsfn;
    }

    std::unique_ptr<stream_js_payload_t> payload(new stream_js_payload_t());
    payload->routing_id.assign(rid_->data, rid_->data + rid_->size);
    payload->packets.reserve(msg_count_);

    for (size_t i = 0; i < msg_count_; ++i) {
        zlink_msg_t *msg = &msgs_[i];
        const unsigned char *packet_data =
          static_cast<const unsigned char *>(zlink_msg_data(msg));
        const size_t packet_size = zlink_msg_size(msg);
        std::vector<unsigned char> packet;
        if (packet_data && packet_size > 0)
            packet.assign(packet_data, packet_data + packet_size);
        payload->packets.push_back(packet);
    }

    napi_status call_status =
      napi_call_threadsafe_function(tsfn, payload.get(), napi_tsfn_nonblocking);
    if (call_status != napi_ok)
        return 1;
    payload.release();

    if (state->stop_requested.load(std::memory_order_acquire) != 0)
        return 1;
    return 0;
}

typedef int (*stream_slot_callback_t)(const zlink_routing_id_t *,
                                      zlink_msg_t *,
                                      size_t);

#define STREAM_SLOT_CALLBACK(N) &stream_on_packets_slot<N>
static stream_slot_callback_t g_stream_slot_callbacks[k_stream_slot_count] = {
    STREAM_SLOT_CALLBACK(0),
    STREAM_SLOT_CALLBACK(1),
    STREAM_SLOT_CALLBACK(2),
    STREAM_SLOT_CALLBACK(3),
    STREAM_SLOT_CALLBACK(4),
    STREAM_SLOT_CALLBACK(5),
    STREAM_SLOT_CALLBACK(6),
    STREAM_SLOT_CALLBACK(7),
};
#undef STREAM_SLOT_CALLBACK

void stream_release_slot(void *socket)
{
    napi_threadsafe_function tsfn = NULL;
    {
        std::lock_guard<std::mutex> lock(g_stream_slots_mu);
        stream_js_state_t *state = find_stream_slot_by_socket_unsafe(socket);
        if (!state)
            return;
        tsfn = state->tsfn;
        reset_stream_slot_unsafe(state);
    }
    if (tsfn)
        (void) napi_release_threadsafe_function(tsfn, napi_tsfn_abort);
}

} // namespace

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
            for (size_t j = 0; j < built; j++)
                zlink_msg_close(&(*out)[j]);
            napi_throw_type_error(env, NULL, "parts element read failed");
            return false;
        }
        bool is_buf = false;
        if (napi_is_buffer(env, val, &is_buf) != napi_ok || !is_buf) {
            for (size_t j = 0; j < built; j++)
                zlink_msg_close(&(*out)[j]);
            napi_throw_type_error(env, NULL, "parts must be Buffers");
            return false;
        }
        void *data = NULL;
        size_t sz = 0;
        if (napi_get_buffer_info(env, val, &data, &sz) != napi_ok) {
            for (size_t j = 0; j < built; j++)
                zlink_msg_close(&(*out)[j]);
            napi_throw_type_error(env, NULL, "buffer info failed");
            return false;
        }
        if (zlink_msg_init_size(&(*out)[i], sz) != 0) {
            for (size_t j = 0; j < built; j++)
                zlink_msg_close(&(*out)[j]);
            throw_last_error(env, "msg_init_size failed");
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
    (void) zlink_stream_detach(sock);
    stream_release_slot(sock);
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

napi_value socket_send_from(napi_env env, napi_callback_info info)
{
    napi_value argv[4];
    size_t argc = 4;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *sock = NULL;
    napi_get_value_external(env, argv[0], &sock);
    void *data = NULL;
    size_t cap = 0;
    if (napi_get_buffer_info(env, argv[1], &data, &cap) != napi_ok) {
        napi_throw_type_error(env, NULL, "sendFrom buffer invalid");
        return NULL;
    }
    int32_t len = 0;
    napi_get_value_int32(env, argv[2], &len);
    int32_t flags = 0;
    napi_get_value_int32(env, argv[3], &flags);
    if (len < 0 || static_cast<size_t>(len) > cap) {
        napi_throw_range_error(env, NULL, "sendFrom length out of range");
        return NULL;
    }
    int rc = zlink_send(sock, data, static_cast<size_t>(len), flags);
    if (rc < 0)
        return throw_last_error(env, "sendFrom failed");
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

napi_value socket_recv_into(napi_env env, napi_callback_info info)
{
    napi_value argv[3];
    size_t argc = 3;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *sock = NULL;
    napi_get_value_external(env, argv[0], &sock);
    void *data = NULL;
    size_t len = 0;
    if (napi_get_buffer_info(env, argv[1], &data, &len) != napi_ok) {
        napi_throw_type_error(env, NULL, "recvInto buffer invalid");
        return NULL;
    }
    int32_t flags = 0;
    napi_get_value_int32(env, argv[2], &flags);
    if (len == 0) {
        napi_throw_range_error(env, NULL, "recvInto buffer must not be empty");
        return NULL;
    }
    int rc = zlink_recv(sock, data, len, flags);
    if (rc < 0)
        return throw_last_error(env, "recvInto failed");
    napi_value out;
    napi_create_int32(env, rc, &out);
    return out;
}

napi_value socket_recv_msg_into(napi_env env, napi_callback_info info)
{
    napi_value argv[3];
    size_t argc = 3;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *sock = NULL;
    napi_get_value_external(env, argv[0], &sock);
    void *data = NULL;
    size_t len = 0;
    if (napi_get_buffer_info(env, argv[1], &data, &len) != napi_ok) {
        napi_throw_type_error(env, NULL, "recvMsgInto buffer invalid");
        return NULL;
    }
    int32_t flags = 0;
    napi_get_value_int32(env, argv[2], &flags);
    if (len == 0) {
        napi_throw_range_error(env, NULL, "recvMsgInto buffer must not be empty");
        return NULL;
    }

    zlink_msg_t msg;
    int rc = zlink_msg_init(&msg);
    if (rc != 0)
        return throw_last_error(env, "recvMsgInto msg_init failed");

    rc = zlink_msg_recv(&msg, sock, flags);
    if (rc < 0) {
        const int err = zlink_errno();
        (void) zlink_msg_close(&msg);
        errno = err;
        return throw_last_error(env, "recvMsgInto failed");
    }

    const int msg_size = static_cast<int>(zlink_msg_size(&msg));
    if (msg_size > 0) {
        void *src = zlink_msg_data(&msg);
        if (src && data) {
            const size_t copy_len =
              std::min(static_cast<size_t>(msg_size), len);
            if (copy_len > 0)
                memcpy(data, src, copy_len);
        }
    }

    if (zlink_msg_close(&msg) != 0)
        return throw_last_error(env, "recvMsgInto msg_close failed");

    napi_value out;
    napi_create_int32(env, msg_size, &out);
    return out;
}

napi_value socket_stream_attach(napi_env env, napi_callback_info info)
{
    napi_value argv[3];
    size_t argc = 3;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    if (argc < 2) {
        napi_throw_type_error(env, NULL,
                              "streamAttach requires (socket, handler[, mode])");
        return NULL;
    }

    void *sock = NULL;
    napi_get_value_external(env, argv[0], &sock);

    napi_valuetype handler_type = napi_undefined;
    napi_typeof(env, argv[1], &handler_type);
    if (handler_type != napi_function) {
        napi_throw_type_error(env, NULL, "streamAttach handler must be a function");
        return NULL;
    }

    int32_t mode = 0;
    if (argc >= 3)
        napi_get_value_int32(env, argv[2], &mode);

    {
        std::lock_guard<std::mutex> lock(g_stream_slots_mu);
        if (find_stream_slot_by_socket_unsafe(sock)) {
            napi_throw_error(env, NULL, "STREAM callback already attached");
            return NULL;
        }
    }

    stream_js_state_t *slot = NULL;
    size_t slot_index = 0;
    {
        std::lock_guard<std::mutex> lock(g_stream_slots_mu);
        slot = find_free_stream_slot_unsafe();
        if (!slot) {
            napi_throw_error(env, NULL,
                             "no free STREAM callback slot (max 8 attached sockets)");
            return NULL;
        }
        slot_index = static_cast<size_t>(slot - g_stream_slots);
    }

    napi_value resource_name;
    napi_create_string_utf8(env, "zlink-stream", NAPI_AUTO_LENGTH,
                            &resource_name);
    napi_threadsafe_function tsfn = NULL;
    napi_status tsfn_status = napi_create_threadsafe_function(
      env, argv[1], NULL, resource_name, 0, 1, slot, stream_tsfn_finalize, slot,
      stream_tsfn_call_js, &tsfn);
    if (tsfn_status != napi_ok) {
        napi_throw_error(env, NULL, "streamAttach failed to create callback queue");
        return NULL;
    }

    {
        std::lock_guard<std::mutex> lock(g_stream_slots_mu);
        slot->used = true;
        slot->socket = sock;
        slot->env = env;
        slot->tsfn = tsfn;
        slot->stop_requested.store(0, std::memory_order_release);
    }

    const int rc = zlink_stream_attach(sock, g_stream_slot_callbacks[slot_index],
                                       mode);
    if (rc != 0) {
        stream_release_slot(sock);
        return throw_last_error(env, "streamAttach failed");
    }

    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

napi_value socket_stream_detach(napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *sock = NULL;
    napi_get_value_external(env, argv[0], &sock);

    int rc = zlink_stream_detach(sock);
    if (rc != 0) {
        int err = zlink_errno();
        if (err != EINVAL)
            return throw_last_error(env, "streamDetach failed");
    }
    stream_release_slot(sock);

    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

napi_value socket_stream_peer_routing_id(napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *sock = NULL;
    napi_get_value_external(env, argv[0], &sock);
    int32_t index = 0;
    if (argc >= 2)
        napi_get_value_int32(env, argv[1], &index);

    zlink_routing_id_t rid;
    memset(&rid, 0, sizeof(rid));
    const int rc = zlink_socket_peer_routing_id(sock, index, &rid);
    if (rc != 0 || rid.size == 0) {
        napi_value none;
        napi_get_null(env, &none);
        return none;
    }

    napi_value out;
    napi_create_buffer_copy(env, rid.size, rid.data, NULL, &out);
    return out;
}

napi_value socket_stream_send(napi_env env, napi_callback_info info)
{
    napi_value argv[4];
    size_t argc = 4;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    if (argc < 3) {
        napi_throw_type_error(env, NULL,
                              "streamSend requires (socket, routingId, payload[, flags])");
        return NULL;
    }
    void *sock = NULL;
    napi_get_value_external(env, argv[0], &sock);

    void *rid_data = NULL;
    size_t rid_len = 0;
    if (napi_get_buffer_info(env, argv[1], &rid_data, &rid_len) != napi_ok) {
        napi_throw_type_error(env, NULL, "streamSend routingId must be Buffer");
        return NULL;
    }
    if (rid_len == 0 || rid_len > 255) {
        napi_throw_range_error(env, NULL,
                               "streamSend routingId length must be 1..255 bytes");
        return NULL;
    }

    void *payload_data = NULL;
    size_t payload_len = 0;
    if (napi_get_buffer_info(env, argv[2], &payload_data, &payload_len)
        != napi_ok) {
        napi_throw_type_error(env, NULL, "streamSend payload must be Buffer");
        return NULL;
    }

    int32_t flags = 0;
    if (argc >= 4)
        napi_get_value_int32(env, argv[3], &flags);

    zlink_routing_id_t rid;
    memset(&rid, 0, sizeof(rid));
    rid.size = static_cast<uint8_t>(rid_len);
    memcpy(rid.data, rid_data, rid_len);

    const int rc = zlink_stream_send(sock, &rid, payload_data, payload_len, flags);
    if (rc < 0)
        return throw_last_error(env, "streamSend failed");

    napi_value out;
    napi_create_int32(env, rc, &out);
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
