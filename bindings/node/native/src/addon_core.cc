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
static const int32_t k_legacy_socket_pair = 0;
static const int32_t k_legacy_socket_pub = 1;
static const int32_t k_legacy_socket_sub = 2;
static const int32_t k_legacy_socket_dealer = 5;
static const int32_t k_legacy_socket_router = 6;
static const int32_t k_legacy_socket_xpub = 9;
static const int32_t k_legacy_socket_xsub = 10;
static const int32_t k_legacy_socket_stream = 11;
static const int32_t k_legacy_opt_routing_id = 5;
static const int32_t k_legacy_opt_subscribe = 6;
static const int32_t k_legacy_opt_unsubscribe = 7;
static const int32_t k_legacy_opt_xpub_verbose = 40;

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

zlink_socket_type_t translate_socket_type(int32_t type)
{
    switch (type) {
    case k_legacy_socket_pair:
    case ZLINK_SOCKET_PAIR:
        return ZLINK_SOCKET_PAIR;
    case k_legacy_socket_pub:
    case ZLINK_SOCKET_PUB:
        return ZLINK_SOCKET_PUB;
    case k_legacy_socket_sub:
    case ZLINK_SOCKET_SUB:
        return ZLINK_SOCKET_SUB;
    case k_legacy_socket_dealer:
    case ZLINK_SOCKET_DEALER:
        return ZLINK_SOCKET_DEALER;
    case k_legacy_socket_router:
    case ZLINK_SOCKET_ROUTER:
        return ZLINK_SOCKET_ROUTER;
    case k_legacy_socket_xpub:
    case ZLINK_SOCKET_XPUB:
        return ZLINK_SOCKET_XPUB;
    case k_legacy_socket_xsub:
    case ZLINK_SOCKET_XSUB:
        return ZLINK_SOCKET_XSUB;
    case k_legacy_socket_stream:
    case ZLINK_SOCKET_STREAM:
        return ZLINK_SOCKET_STREAM;
    default:
        return static_cast<zlink_socket_type_t>(type);
    }
}

bool init_msg_from_bytes(zlink_msg_t *msg, const void *data, size_t len)
{
    if (zlink_msg_init_size(msg, len) != 0)
        return false;
    if (len > 0 && data)
        memcpy(zlink_msg_data(msg), data, len);
    return true;
}

int recv_parts(void *sock,
               zlink_routing_id_t *routing_id,
               zlink_msg_t **parts,
               size_t *part_count,
               int32_t flags)
{
    if (routing_id)
        memset(routing_id, 0, sizeof(*routing_id));
    *parts = NULL;
    *part_count = 0;
    return zlink_recv(sock, routing_id, parts, part_count, flags);
}

size_t recv_parts_size(zlink_msg_t *parts, size_t part_count)
{
    size_t total = 0;
    for (size_t i = 0; i < part_count; ++i)
        total += zlink_msg_size(&parts[i]);
    return total;
}

void copy_recv_parts(zlink_msg_t *parts,
                     size_t part_count,
                     unsigned char *dest,
                     size_t dest_len)
{
    size_t offset = 0;
    for (size_t i = 0; i < part_count && offset < dest_len; ++i) {
        size_t part_size = zlink_msg_size(&parts[i]);
        size_t copy_len = std::min(part_size, dest_len - offset);
        if (copy_len > 0)
            memcpy(dest + offset, zlink_msg_data(&parts[i]), copy_len);
        offset += copy_len;
    }
}

void close_recv_parts(zlink_msg_t *parts, size_t part_count)
{
    if (!parts)
        return;
    zlink_multipart_close(parts, part_count);
    free(parts);
}

napi_value create_buffer_copy_or_empty(napi_env env, const void *data, size_t len)
{
    napi_value out;
    napi_create_buffer_copy(env, len, len == 0 ? NULL : data, NULL, &out);
    return out;
}

napi_value create_routing_id_value(napi_env env, const zlink_routing_id_t &rid)
{
    if (rid.size == 0) {
        napi_value none;
        napi_get_null(env, &none);
        return none;
    }
    return create_buffer_copy_or_empty(env, rid.data, rid.size);
}

napi_value create_recv_message_value(napi_env env,
                                     const zlink_routing_id_t &routing_id,
                                     zlink_msg_t *parts,
                                     size_t part_count)
{
    napi_value obj;
    napi_create_object(env, &obj);

    napi_value parts_array;
    napi_create_array_with_length(env, part_count, &parts_array);
    for (size_t i = 0; i < part_count; ++i) {
        napi_value part = create_buffer_copy_or_empty(
          env, zlink_msg_data(&parts[i]), zlink_msg_size(&parts[i]));
        napi_set_element(env, parts_array, static_cast<uint32_t>(i), part);
    }

    napi_value rid = create_routing_id_value(env, routing_id);
    napi_value has_more;
    napi_get_boolean(env, false, &has_more);

    napi_set_named_property(env, obj, "parts", parts_array);
    napi_set_named_property(env, obj, "routingId", rid);
    napi_set_named_property(env, obj, "hasMore", has_more);
    return obj;
}

int set_socket_option(void *sock, int32_t opt, const void *data, size_t len)
{
    switch (opt) {
    case k_legacy_opt_routing_id:
        return zlink_set_routing_id(sock, data, len);
    case k_legacy_opt_subscribe: {
        std::string filter(static_cast<const char *>(data), len);
        return zlink_set_subscription(sock, filter.c_str());
    }
    case k_legacy_opt_unsubscribe: {
        std::string filter(static_cast<const char *>(data), len);
        return zlink_unset_subscription(sock, filter.c_str());
    }
    case k_legacy_opt_xpub_verbose: {
        int value = 0;
        if (len >= sizeof(int))
            memcpy(&value, data, sizeof(int));
        return zlink_set_pub_option(sock, ZLINK_PUB_OPT_VERBOSE, &value,
                                    sizeof(value));
    }
    case ZLINK_ROUTER_OPT_MANDATORY:
        return zlink_set_router_option(sock, ZLINK_ROUTER_OPT_MANDATORY, data, len);
    case ZLINK_ROUTER_OPT_HANDOVER:
        return zlink_set_router_option(sock, ZLINK_ROUTER_OPT_HANDOVER, data, len);
    case ZLINK_ROUTER_OPT_PROBE:
        return zlink_set_router_option(sock, ZLINK_ROUTER_OPT_PROBE, data, len);
    case ZLINK_ROUTER_OPT_CONNECT_ROUTING_ID:
        return zlink_set_router_option(sock, ZLINK_ROUTER_OPT_CONNECT_ROUTING_ID,
                                       data, len);
    case ZLINK_PUB_OPT_VERBOSE:
        return zlink_set_pub_option(sock, ZLINK_PUB_OPT_VERBOSE, data, len);
    case ZLINK_PUB_OPT_VERBOSER:
        return zlink_set_pub_option(sock, ZLINK_PUB_OPT_VERBOSER, data, len);
    case ZLINK_PUB_OPT_MANUAL:
        return zlink_set_pub_option(sock, ZLINK_PUB_OPT_MANUAL, data, len);
    case ZLINK_PUB_OPT_MANUAL_LAST_VALUE:
        return zlink_set_pub_option(sock, ZLINK_PUB_OPT_MANUAL_LAST_VALUE, data, len);
    case ZLINK_PUB_OPT_NODROP:
        return zlink_set_pub_option(sock, ZLINK_PUB_OPT_NODROP, data, len);
    case ZLINK_PUB_OPT_WELCOME_MSG:
        return zlink_set_pub_option(sock, ZLINK_PUB_OPT_WELCOME_MSG, data, len);
    case ZLINK_PUB_OPT_TOPICS_COUNT:
        return zlink_set_pub_option(sock, ZLINK_PUB_OPT_TOPICS_COUNT, data, len);
    case ZLINK_PUB_OPT_APPROVE_SUBSCRIBE:
        return zlink_set_pub_option(sock, ZLINK_PUB_OPT_APPROVE_SUBSCRIBE, data, len);
    case ZLINK_PUB_OPT_REJECT_SUBSCRIBE:
        return zlink_set_pub_option(sock, ZLINK_PUB_OPT_REJECT_SUBSCRIBE, data, len);
    case ZLINK_SUB_OPT_TOPICS_COUNT:
        return zlink_set_sub_option(sock, ZLINK_SUB_OPT_TOPICS_COUNT, data, len);
    default:
        return zlink_set_option(sock, static_cast<zlink_option_t>(opt), data,
                                len);
    }
}

int get_socket_option(void *sock, int32_t opt, void *data, size_t *len)
{
    switch (opt) {
    case k_legacy_opt_routing_id: {
        zlink_routing_id_t rid;
        memset(&rid, 0, sizeof(rid));
        int rc = zlink_get_routing_id(sock, &rid);
        if (rc != 0)
            return rc;
        size_t copy_len = std::min(*len, static_cast<size_t>(rid.size));
        if (copy_len > 0)
            memcpy(data, rid.data, copy_len);
        *len = rid.size;
        return 0;
    }
    case ZLINK_ROUTER_OPT_MANDATORY:
        return zlink_get_router_option(sock, ZLINK_ROUTER_OPT_MANDATORY, data, len);
    case ZLINK_ROUTER_OPT_HANDOVER:
        return zlink_get_router_option(sock, ZLINK_ROUTER_OPT_HANDOVER, data, len);
    case ZLINK_ROUTER_OPT_PROBE:
        return zlink_get_router_option(sock, ZLINK_ROUTER_OPT_PROBE, data, len);
    case ZLINK_ROUTER_OPT_CONNECT_ROUTING_ID:
        return zlink_get_router_option(sock, ZLINK_ROUTER_OPT_CONNECT_ROUTING_ID,
                                       data, len);
    case ZLINK_PUB_OPT_VERBOSE:
        return zlink_get_pub_option(sock, ZLINK_PUB_OPT_VERBOSE, data, len);
    case ZLINK_PUB_OPT_VERBOSER:
        return zlink_get_pub_option(sock, ZLINK_PUB_OPT_VERBOSER, data, len);
    case ZLINK_PUB_OPT_MANUAL:
        return zlink_get_pub_option(sock, ZLINK_PUB_OPT_MANUAL, data, len);
    case ZLINK_PUB_OPT_MANUAL_LAST_VALUE:
        return zlink_get_pub_option(sock, ZLINK_PUB_OPT_MANUAL_LAST_VALUE, data, len);
    case ZLINK_PUB_OPT_NODROP:
        return zlink_get_pub_option(sock, ZLINK_PUB_OPT_NODROP, data, len);
    case ZLINK_PUB_OPT_WELCOME_MSG:
        return zlink_get_pub_option(sock, ZLINK_PUB_OPT_WELCOME_MSG, data, len);
    case ZLINK_PUB_OPT_TOPICS_COUNT:
        return zlink_get_pub_option(sock, ZLINK_PUB_OPT_TOPICS_COUNT, data, len);
    case ZLINK_SUB_OPT_TOPICS_COUNT:
        return zlink_get_sub_option(sock, ZLINK_SUB_OPT_TOPICS_COUNT, data, len);
    default:
        return zlink_get_option(sock, static_cast<zlink_option_t>(opt), data,
                                len);
    }
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
        return;
    }

    int32_t ret = 0;
    if (napi_get_value_int32(env, recv, &ret) == napi_ok && ret != 0) {
        state->stop_requested.store(1, std::memory_order_release);
    }
}

template <size_t Slot>
int stream_on_packets_slot(const zlink_routing_id_t *rid_,
                           zlink_msg_t *msgs_,
                           size_t msg_count_,
                           void *userdata_)
{
    if (!rid_ || !msgs_ || msg_count_ == 0)
        return 0;

    const auto close_msgs = [msgs_, msg_count_]() {
        for (size_t i = 0; i < msg_count_; ++i)
            (void) zlink_msg_close(&msgs_[i]);
    };

    stream_js_state_t *state = &g_stream_slots[Slot];
    napi_threadsafe_function tsfn = NULL;
    {
        std::lock_guard<std::mutex> lock(g_stream_slots_mu);
        if (!state->used || !state->tsfn) {
            close_msgs();
            return 0;
        }
        if (state->stop_requested.load(std::memory_order_acquire) != 0) {
            close_msgs();
            return 1;
        }
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
        (void) zlink_msg_close(msg);
    }

    napi_status call_status =
      napi_call_threadsafe_function(tsfn, payload.get(), napi_tsfn_blocking);
    if (call_status != napi_ok)
        return 1;
    payload.release();

    if (state->stop_requested.load(std::memory_order_acquire) != 0)
        return 1;
    return 0;
}

template <size_t Slot>
int stream_on_raw_slot(const zlink_routing_id_t *rid_, zlink_msg_t *msg_, void *userdata_)
{
    if (!rid_ || !msg_)
        return 0;

    const auto close_msg = [msg_]() { (void) zlink_msg_close(msg_); };

    stream_js_state_t *state = &g_stream_slots[Slot];
    napi_threadsafe_function tsfn = NULL;
    {
        std::lock_guard<std::mutex> lock(g_stream_slots_mu);
        if (!state->used || !state->tsfn) {
            close_msg();
            return 0;
        }
        if (state->stop_requested.load(std::memory_order_acquire) != 0) {
            close_msg();
            return 1;
        }
        tsfn = state->tsfn;
    }

    std::unique_ptr<stream_js_payload_t> payload(new stream_js_payload_t());
    payload->routing_id.assign(rid_->data, rid_->data + rid_->size);
    payload->packets.reserve(1);

    const unsigned char *packet_data =
      static_cast<const unsigned char *>(zlink_msg_data(msg_));
    const size_t packet_size = zlink_msg_size(msg_);
    std::vector<unsigned char> packet;
    if (packet_data && packet_size > 0)
        packet.assign(packet_data, packet_data + packet_size);
    payload->packets.push_back(packet);
    (void) zlink_msg_close(msg_);

    napi_status call_status =
      napi_call_threadsafe_function(tsfn, payload.get(), napi_tsfn_blocking);
    if (call_status != napi_ok)
        return 1;
    payload.release();

    if (state->stop_requested.load(std::memory_order_acquire) != 0)
        return 1;
    return 0;
}

typedef int (*stream_slot_packets_callback_t)(const zlink_routing_id_t *,
                                              zlink_msg_t *,
                                              size_t,
                                              void *);
typedef int (*stream_slot_raw_callback_t)(const zlink_routing_id_t *,
                                          zlink_msg_t *,
                                          void *);

#define STREAM_SLOT_PACKETS_CALLBACK(N) &stream_on_packets_slot<N>
static stream_slot_packets_callback_t
  g_stream_slot_packet_callbacks[k_stream_slot_count] = {
    STREAM_SLOT_PACKETS_CALLBACK(0),
    STREAM_SLOT_PACKETS_CALLBACK(1),
    STREAM_SLOT_PACKETS_CALLBACK(2),
    STREAM_SLOT_PACKETS_CALLBACK(3),
    STREAM_SLOT_PACKETS_CALLBACK(4),
    STREAM_SLOT_PACKETS_CALLBACK(5),
    STREAM_SLOT_PACKETS_CALLBACK(6),
    STREAM_SLOT_PACKETS_CALLBACK(7),
};
#undef STREAM_SLOT_PACKETS_CALLBACK

#define STREAM_SLOT_RAW_CALLBACK(N) &stream_on_raw_slot<N>
static stream_slot_raw_callback_t g_stream_slot_raw_callbacks[k_stream_slot_count] = {
    STREAM_SLOT_RAW_CALLBACK(0),
    STREAM_SLOT_RAW_CALLBACK(1),
    STREAM_SLOT_RAW_CALLBACK(2),
    STREAM_SLOT_RAW_CALLBACK(3),
    STREAM_SLOT_RAW_CALLBACK(4),
    STREAM_SLOT_RAW_CALLBACK(5),
    STREAM_SLOT_RAW_CALLBACK(6),
    STREAM_SLOT_RAW_CALLBACK(7),
};
#undef STREAM_SLOT_RAW_CALLBACK

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
    void *sock = zlink_socket(ctx, translate_socket_type(type));
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
    zlink_msg_t msg;
    if (!init_msg_from_bytes(&msg, data, len))
        return throw_last_error(env, "send failed");
    int rc = zlink_send(sock, &msg, 1, flags);
    if (rc != 0)
        (void) zlink_msg_close(&msg);
    if (rc < 0)
        return throw_last_error(env, "send failed");
    napi_value out;
    napi_create_int32(env, static_cast<int32_t>(len), &out);
    return out;
}

napi_value socket_send_parts(napi_env env, napi_callback_info info)
{
    napi_value argv[3];
    size_t argc = 3;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *sock = NULL;
    napi_get_value_external(env, argv[0], &sock);

    std::vector<zlink_msg_t> parts;
    if (!build_msg_vector(env, argv[1], &parts))
        return NULL;

    int32_t flags = 0;
    napi_get_value_int32(env, argv[2], &flags);
    int rc = zlink_send(sock, parts.data(), parts.size(), flags);
    if (rc != 0) {
        close_msg_vector(parts);
        return throw_last_error(env, "sendParts failed");
    }

    size_t total = 0;
    for (size_t i = 0; i < parts.size(); ++i)
        total += zlink_msg_size(&parts[i]);
    napi_value out;
    napi_create_int32(env, static_cast<int32_t>(total), &out);
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
    zlink_msg_t msg;
    if (!init_msg_from_bytes(&msg, data, static_cast<size_t>(len)))
        return throw_last_error(env, "sendFrom failed");
    int rc = zlink_send(sock, &msg, 1, flags);
    if (rc != 0)
        (void) zlink_msg_close(&msg);
    if (rc < 0)
        return throw_last_error(env, "sendFrom failed");
    napi_value out;
    napi_create_int32(env, len, &out);
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
    zlink_routing_id_t routing_id;
    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    int rc = recv_parts(sock, &routing_id, &parts, &part_count, flags);
    if (rc != 0)
        return throw_last_error(env, "recv failed");
    size_t total = recv_parts_size(parts, part_count);
    std::vector<unsigned char> bytes(total);
    if (total > 0)
        copy_recv_parts(parts, part_count, bytes.data(), total);
    close_recv_parts(parts, part_count);
    napi_value out;
    napi_create_buffer_copy(env, total, total == 0 ? NULL : bytes.data(), NULL,
                            &out);
    return out;
}

napi_value socket_recv_message(napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *sock = NULL;
    napi_get_value_external(env, argv[0], &sock);
    int32_t flags = 0;
    if (argc >= 2)
        napi_get_value_int32(env, argv[1], &flags);

    zlink_routing_id_t routing_id;
    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    int rc = recv_parts(sock, &routing_id, &parts, &part_count, flags);
    if (rc != 0)
        return throw_last_error(env, "recv failed");

    napi_value out = create_recv_message_value(env, routing_id, parts, part_count);
    close_recv_parts(parts, part_count);
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
    zlink_routing_id_t routing_id;
    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    int rc = recv_parts(sock, &routing_id, &parts, &part_count, flags);
    if (rc != 0)
        return throw_last_error(env, "recvInto failed");
    size_t total = recv_parts_size(parts, part_count);
    if (total > 0)
        copy_recv_parts(parts, part_count,
                        static_cast<unsigned char *>(data), len);
    close_recv_parts(parts, part_count);
    napi_value out;
    napi_create_int32(env, static_cast<int32_t>(total), &out);
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

    zlink_routing_id_t routing_id;
    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    int rc = recv_parts(sock, &routing_id, &parts, &part_count, flags);
    if (rc != 0)
        return throw_last_error(env, "recvMsgInto failed");
    size_t total = recv_parts_size(parts, part_count);
    if (total > 0)
        copy_recv_parts(parts, part_count,
                        static_cast<unsigned char *>(data), len);
    close_recv_parts(parts, part_count);

    napi_value out;
    napi_create_int32(env, static_cast<int32_t>(total), &out);
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
    (void) sock;
    (void) mode;
    napi_throw_error(env, NULL,
                     "streamAttach is not available on the aligned public API");
    return NULL;
}

napi_value socket_stream_detach(napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *sock = NULL;
    napi_get_value_external(env, argv[0], &sock);

    (void) sock;
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

    (void) sock;
    (void) index;
    napi_value none;
    napi_get_null(env, &none);
    return none;
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

    (void) sock;
    (void) rid_data;
    (void) rid_len;
    (void) payload_data;
    (void) payload_len;
    (void) flags;
    napi_throw_error(env, NULL,
                     "streamSend is not available on the aligned public API");
    return NULL;
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
    int rc = set_socket_option(sock, opt, data, len);
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
    int rc = get_socket_option(sock, opt, data, &len);
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
    zlink_socket_monitor_open_options_t options;
    options.events = static_cast<zlink_socket_monitor_event_mask_t>(events);
    void *mon = zlink_socket_monitor_open(sock, &options);
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
    (void) argv;
    zlink_monitor_event_t evt;
    int rc = zlink_socket_monitor_recv(mon, &evt);
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

napi_value monitor_snapshot(napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *monitor = NULL;
    napi_get_value_external(env, argv[0], &monitor);

    zlink_monitor_snapshot_t snapshot;
    memset(&snapshot, 0, sizeof(snapshot));
    int rc = zlink_monitor_snapshot(monitor, &snapshot);
    if (rc != 0)
        return throw_last_error(env, "monitor_snapshot failed");

    napi_value obj;
    napi_create_object(env, &obj);
    napi_value source_kind, state_flags, detail_flags;
    napi_value ready_count, snd_pending, rcv_pending;
    napi_create_uint32(env, static_cast<uint32_t>(snapshot.source_kind),
                       &source_kind);
    napi_create_uint32(env, snapshot.state_flags, &state_flags);
    napi_create_uint32(env, snapshot.detail_flags, &detail_flags);
    napi_create_uint32(env, snapshot.ready_count, &ready_count);
    napi_create_int64(env, static_cast<int64_t>(snapshot.snd_pending_msgs),
                      &snd_pending);
    napi_create_int64(env, static_cast<int64_t>(snapshot.rcv_pending_msgs),
                      &rcv_pending);
    napi_set_named_property(env, obj, "sourceKind", source_kind);
    napi_set_named_property(env, obj, "stateFlags", state_flags);
    napi_set_named_property(env, obj, "detailFlags", detail_flags);
    napi_set_named_property(env, obj, "readyCount", ready_count);
    napi_set_named_property(env, obj, "sndPendingMsgs", snd_pending);
    napi_set_named_property(env, obj, "rcvPendingMsgs", rcv_pending);
    return obj;
}

napi_value monitor_close(napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *monitor = NULL;
    napi_get_value_external(env, argv[0], &monitor);
    void *tmp = monitor;
    int rc = zlink_monitor_close(&tmp);
    if (rc != 0)
        return throw_last_error(env, "monitor_close failed");
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
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
