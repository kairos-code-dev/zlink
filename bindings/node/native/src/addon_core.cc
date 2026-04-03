/* SPDX-License-Identifier: MPL-2.0 */

#include "addon_api.h"
#include <algorithm>
#include <errno.h>
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace {

static const size_t k_stream_slot_count = 8;
static const size_t k_recv_handler_slot_count = 8;
static const size_t k_subscribe_handler_slot_count = 8;
static const int32_t k_stream_dispatch_none = 0;
static const int32_t k_stream_dispatch_len32be = 1;
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

int classify_try_send_errno()
{
    switch (zlink_errno()) {
    case EAGAIN:
        return ZLINK_SEND_RESULT_BACKPRESSURED;
#ifdef ENOTCONN
    case ENOTCONN:
#endif
#ifdef EHOSTUNREACH
    case EHOSTUNREACH:
#endif
#ifdef ETIMEDOUT
    case ETIMEDOUT:
#endif
        return ZLINK_SEND_RESULT_NOT_READY;
    default:
        return -1;
    }
}

struct stream_js_payload_t
{
    std::vector<unsigned char> routing_id;
    std::vector<std::vector<unsigned char> > packets;
};

struct recv_handler_js_payload_t
{
    std::vector<unsigned char> routing_id;
    std::vector<std::vector<unsigned char> > parts;
};

struct subscribe_handler_js_payload_t
{
    std::vector<unsigned char> routing_id;
    std::string topic;
    std::vector<std::vector<unsigned char> > parts;
};

struct stream_js_state_t
{
    stream_js_state_t () :
        used (false),
        socket (NULL),
        env (NULL),
        tsfn (NULL),
        stop_requested (0),
        dispatch_mode (k_stream_dispatch_none)
    {
    }

    bool used;
    void *socket;
    napi_env env;
    napi_threadsafe_function tsfn;
    std::atomic<int> stop_requested;
    int32_t dispatch_mode;
    std::map<std::string, std::vector<unsigned char> > len32be_pending;
    std::vector<std::vector<unsigned char> > peer_routing_ids;
};

static std::mutex g_stream_slots_mu;
static stream_js_state_t g_stream_slots[k_stream_slot_count];

struct recv_handler_js_state_t
{
    recv_handler_js_state_t () : used (false), socket (NULL), env (NULL), tsfn (NULL) {}

    bool used;
    void *socket;
    napi_env env;
    napi_threadsafe_function tsfn;
};

struct subscribe_handler_js_state_t
{
    subscribe_handler_js_state_t () : used (false), socket (NULL), env (NULL), tsfn (NULL) {}

    bool used;
    void *socket;
    napi_env env;
    napi_threadsafe_function tsfn;
};

static std::mutex g_recv_handler_slots_mu;
static recv_handler_js_state_t g_recv_handler_slots[k_recv_handler_slot_count];
static std::mutex g_subscribe_handler_slots_mu;
static subscribe_handler_js_state_t
  g_subscribe_handler_slots[k_subscribe_handler_slot_count];

stream_js_state_t *find_stream_slot_by_socket_unsafe(void *socket)
{
    for (size_t i = 0; i < k_stream_slot_count; ++i) {
        if (g_stream_slots[i].used && g_stream_slots[i].socket == socket)
            return &g_stream_slots[i];
    }
    return NULL;
}

recv_handler_js_state_t *find_recv_handler_slot_by_socket_unsafe(void *socket)
{
    for (size_t i = 0; i < k_recv_handler_slot_count; ++i) {
        if (g_recv_handler_slots[i].used
            && g_recv_handler_slots[i].socket == socket) {
            return &g_recv_handler_slots[i];
        }
    }
    return NULL;
}

recv_handler_js_state_t *find_free_recv_handler_slot_unsafe()
{
    for (size_t i = 0; i < k_recv_handler_slot_count; ++i) {
        if (!g_recv_handler_slots[i].used)
            return &g_recv_handler_slots[i];
    }
    return NULL;
}

subscribe_handler_js_state_t *find_subscribe_handler_slot_by_socket_unsafe(
  void *socket)
{
    for (size_t i = 0; i < k_subscribe_handler_slot_count; ++i) {
        if (g_subscribe_handler_slots[i].used
            && g_subscribe_handler_slots[i].socket == socket) {
            return &g_subscribe_handler_slots[i];
        }
    }
    return NULL;
}

subscribe_handler_js_state_t *find_free_subscribe_handler_slot_unsafe()
{
    for (size_t i = 0; i < k_subscribe_handler_slot_count; ++i) {
        if (!g_subscribe_handler_slots[i].used)
            return &g_subscribe_handler_slots[i];
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
    state->dispatch_mode = k_stream_dispatch_none;
    state->len32be_pending.clear();
    state->peer_routing_ids.clear();
}

void reset_recv_handler_slot_unsafe(recv_handler_js_state_t *state)
{
    if (!state)
        return;
    state->used = false;
    state->socket = NULL;
    state->env = NULL;
    state->tsfn = NULL;
}

void reset_subscribe_handler_slot_unsafe(subscribe_handler_js_state_t *state)
{
    if (!state)
        return;
    state->used = false;
    state->socket = NULL;
    state->env = NULL;
    state->tsfn = NULL;
}

typedef int (*zlink_stream_on_raw_fn) (const zlink_routing_id_t *, zlink_msg_t *);

extern "C" {
int zlink_stream_attach_raw (void *s_, zlink_stream_on_raw_fn on_raw_);
int zlink_stream_detach (void *s_);
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
}

void copy_recv_parts_to_vectors(zlink_msg_t *parts,
                                size_t part_count,
                                std::vector<std::vector<unsigned char> > *out)
{
    if (!out)
        return;
    out->clear();
    out->reserve(part_count);
    for (size_t i = 0; i < part_count; ++i) {
        const unsigned char *part_data =
          static_cast<const unsigned char *>(zlink_msg_data(&parts[i]));
        size_t part_size = zlink_msg_size(&parts[i]);
        std::vector<unsigned char> copy;
        if (part_data && part_size > 0)
            copy.assign(part_data, part_data + part_size);
        out->push_back(copy);
    }
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

napi_value create_subscription_event_value(napi_env env,
                                           const zlink_routing_id_t &routing_id,
                                           int subscribed,
                                           const char *topic,
                                           size_t topic_len)
{
    napi_value obj;
    napi_create_object(env, &obj);

    napi_value rid = create_routing_id_value(env, routing_id);
    napi_value topic_value;
    napi_create_string_utf8(
      env, topic ? topic : "", topic ? topic_len : 0, &topic_value);
    napi_value subscribed_value;
    napi_get_boolean(env, subscribed != 0, &subscribed_value);

    napi_set_named_property(env, obj, "routingId", rid);
    napi_set_named_property(env, obj, "topic", topic_value);
    napi_set_named_property(env, obj, "subscribed", subscribed_value);
    return obj;
}

napi_value create_subscribed_value(napi_env env,
                                   const zlink_routing_id_t &routing_id,
                                   const char *topic,
                                   size_t topic_len,
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
    napi_value topic_value;
    napi_create_string_utf8(
      env, topic ? topic : "", topic ? topic_len : 0, &topic_value);

    napi_set_named_property(env, obj, "routingId", rid);
    napi_set_named_property(env, obj, "topic", topic_value);
    napi_set_named_property(env, obj, "parts", parts_array);
    return obj;
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
    case ZLINK_DEALER_OPT_PROBE:
        return zlink_set_dealer_option(sock, ZLINK_DEALER_OPT_PROBE, data, len);
    case ZLINK_STREAM_OPT_NOTIFY:
        return zlink_set_stream_option(sock, ZLINK_STREAM_OPT_NOTIFY, data, len);
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
    case ZLINK_DEALER_OPT_PROBE:
        return zlink_get_router_option(sock, ZLINK_ROUTER_OPT_PROBE, data, len);
    case ZLINK_STREAM_OPT_NOTIFY:
        return zlink_get_stream_option(sock, ZLINK_STREAM_OPT_NOTIFY, data, len);
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

size_t initial_getopt_buffer_len(int32_t opt)
{
    switch (opt) {
    case k_legacy_opt_routing_id:
    case ZLINK_ROUTER_OPT_CONNECT_ROUTING_ID:
        return 255;
    case ZLINK_OPT_LAST_ENDPOINT:
    case ZLINK_OPT_TLS_CERT:
    case ZLINK_OPT_TLS_KEY:
    case ZLINK_OPT_TLS_CA:
    case ZLINK_OPT_TLS_HOSTNAME:
    case ZLINK_OPT_TLS_PASSWORD:
    case ZLINK_OPT_BINDTODEVICE:
    case ZLINK_OPT_ZMP_METADATA:
        return 256;
    case ZLINK_OPT_MAXMSGSIZE:
        return sizeof(int64_t);
    default:
        return sizeof(int);
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

void recv_handler_tsfn_finalize(napi_env env,
                                void *finalize_data,
                                void *finalize_hint)
{
    (void) env;
    (void) finalize_hint;
    recv_handler_js_state_t *state =
      static_cast<recv_handler_js_state_t *>(finalize_data);
    if (!state)
        return;
    std::lock_guard<std::mutex> lock(g_recv_handler_slots_mu);
    reset_recv_handler_slot_unsafe(state);
}

void subscribe_handler_tsfn_finalize(napi_env env,
                                     void *finalize_data,
                                     void *finalize_hint)
{
    (void) env;
    (void) finalize_hint;
    subscribe_handler_js_state_t *state =
      static_cast<subscribe_handler_js_state_t *>(finalize_data);
    if (!state)
        return;
    std::lock_guard<std::mutex> lock(g_subscribe_handler_slots_mu);
    reset_subscribe_handler_slot_unsafe(state);
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

void recv_handler_tsfn_call_js(napi_env env,
                               napi_value js_cb,
                               void *context,
                               void *data)
{
    std::unique_ptr<recv_handler_js_payload_t> payload(
      static_cast<recv_handler_js_payload_t *>(data));
    (void) context;
    if (!env || !js_cb || !payload)
        return;

    napi_value argv[2];
    if (!payload->routing_id.empty()) {
        if (napi_create_buffer_copy(
              env, payload->routing_id.size(), payload->routing_id.data(), NULL,
              &argv[0])
            != napi_ok) {
            return;
        }
    } else {
        napi_get_null(env, &argv[0]);
    }
    if (napi_create_array_with_length(env, payload->parts.size(), &argv[1])
        != napi_ok) {
        return;
    }
    for (size_t i = 0; i < payload->parts.size(); ++i) {
        const std::vector<unsigned char> &part = payload->parts[i];
        napi_value part_buf;
        if (napi_create_buffer_copy(
              env, part.size(), part.empty() ? NULL : part.data(), NULL,
              &part_buf)
            != napi_ok) {
            return;
        }
        napi_set_element(env, argv[1], static_cast<uint32_t>(i), part_buf);
    }

    napi_value recv;
    napi_value this_arg;
    napi_get_undefined(env, &this_arg);
    (void) napi_call_function(env, this_arg, js_cb, 2, argv, &recv);
}

void subscribe_handler_tsfn_call_js(napi_env env,
                                    napi_value js_cb,
                                    void *context,
                                    void *data)
{
    std::unique_ptr<subscribe_handler_js_payload_t> payload(
      static_cast<subscribe_handler_js_payload_t *>(data));
    (void) context;
    if (!env || !js_cb || !payload)
        return;

    napi_value argv[3];
    if (!payload->routing_id.empty()) {
        if (napi_create_buffer_copy(
              env, payload->routing_id.size(), payload->routing_id.data(), NULL,
              &argv[0])
            != napi_ok) {
            return;
        }
    } else {
        napi_get_null(env, &argv[0]);
    }
    napi_create_string_utf8(
      env, payload->topic.c_str(), payload->topic.size(), &argv[1]);
    if (napi_create_array_with_length(env, payload->parts.size(), &argv[2])
        != napi_ok) {
        return;
    }
    for (size_t i = 0; i < payload->parts.size(); ++i) {
        const std::vector<unsigned char> &part = payload->parts[i];
        napi_value part_buf;
        if (napi_create_buffer_copy(
              env, part.size(), part.empty() ? NULL : part.data(), NULL,
              &part_buf)
            != napi_ok) {
            return;
        }
        napi_set_element(env, argv[2], static_cast<uint32_t>(i), part_buf);
    }

    napi_value recv;
    napi_value this_arg;
    napi_get_undefined(env, &this_arg);
    (void) napi_call_function(env, this_arg, js_cb, 3, argv, &recv);
}

std::string make_routing_id_key(const zlink_routing_id_t *rid)
{
    if (!rid || rid->size == 0)
        return std::string();
    return std::string(reinterpret_cast<const char *>(rid->data), rid->size);
}

void remember_stream_peer_unsafe(stream_js_state_t *state,
                                 const zlink_routing_id_t *rid)
{
    if (!state || !rid || rid->size == 0)
        return;

    for (size_t i = 0; i < state->peer_routing_ids.size(); ++i) {
        const std::vector<unsigned char> &known = state->peer_routing_ids[i];
        if (known.size() != rid->size)
            continue;
        if (memcmp(known.data(), rid->data, rid->size) == 0)
            return;
    }

    std::vector<unsigned char> peer(rid->data, rid->data + rid->size);
    state->peer_routing_ids.push_back(peer);
}

void collect_stream_payload_unsafe(stream_js_state_t *state,
                                   const zlink_routing_id_t *rid,
                                   zlink_msg_t *msg,
                                   stream_js_payload_t *payload)
{
    if (!state || !msg || !payload)
        return;

    if (rid && rid->size > 0)
        payload->routing_id.assign(rid->data, rid->data + rid->size);

    const unsigned char *packet_data =
      static_cast<const unsigned char *>(zlink_msg_data(msg));
    const size_t packet_size = zlink_msg_size(msg);

    if (state->dispatch_mode != k_stream_dispatch_len32be) {
        std::vector<unsigned char> packet;
        if (packet_data && packet_size > 0)
            packet.assign(packet_data, packet_data + packet_size);
        payload->packets.push_back(packet);
        return;
    }

    const std::string peer_key = make_routing_id_key(rid);
    std::vector<unsigned char> &pending = state->len32be_pending[peer_key];
    if (packet_data && packet_size > 0)
        pending.insert(pending.end(), packet_data, packet_data + packet_size);

    while (pending.size() >= 4) {
        const size_t frame_size =
          (static_cast<size_t>(pending[0]) << 24)
          | (static_cast<size_t>(pending[1]) << 16)
          | (static_cast<size_t>(pending[2]) << 8)
          | static_cast<size_t>(pending[3]);
        if (pending.size() < frame_size + 4)
            break;

        std::vector<unsigned char> packet;
        if (frame_size > 0) {
            packet.assign(pending.begin() + 4, pending.begin() + 4 + frame_size);
        }
        payload->packets.push_back(packet);
        pending.erase(pending.begin(), pending.begin() + 4 + frame_size);
    }
}

template <size_t Slot>
int stream_on_raw_slot(const zlink_routing_id_t *rid_, zlink_msg_t *msg_, void *userdata_)
{
    if (!rid_ || !msg_)
        return 0;

    const auto close_msg = [msg_]() { (void) zlink_msg_close(msg_); };

    stream_js_state_t *state = &g_stream_slots[Slot];
    napi_threadsafe_function tsfn = NULL;
    std::unique_ptr<stream_js_payload_t> payload;
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
        remember_stream_peer_unsafe(state, rid_);
        payload.reset(new stream_js_payload_t());
        collect_stream_payload_unsafe(state, rid_, msg_, payload.get());
        if (payload->packets.empty()) {
            close_msg();
            return 0;
        }
    }

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

template <size_t Slot>
int stream_on_raw_legacy_slot(const zlink_routing_id_t *rid_, zlink_msg_t *msg_)
{
    return stream_on_raw_slot<Slot>(rid_, msg_, NULL);
}

typedef int (*stream_slot_raw_legacy_callback_t)(const zlink_routing_id_t *,
                                                 zlink_msg_t *);

#define STREAM_SLOT_RAW_LEGACY_CALLBACK(N) &stream_on_raw_legacy_slot<N>
static stream_slot_raw_legacy_callback_t
  g_stream_slot_raw_legacy_callbacks[k_stream_slot_count] = {
    STREAM_SLOT_RAW_LEGACY_CALLBACK(0),
    STREAM_SLOT_RAW_LEGACY_CALLBACK(1),
    STREAM_SLOT_RAW_LEGACY_CALLBACK(2),
    STREAM_SLOT_RAW_LEGACY_CALLBACK(3),
    STREAM_SLOT_RAW_LEGACY_CALLBACK(4),
    STREAM_SLOT_RAW_LEGACY_CALLBACK(5),
    STREAM_SLOT_RAW_LEGACY_CALLBACK(6),
    STREAM_SLOT_RAW_LEGACY_CALLBACK(7),
};
#undef STREAM_SLOT_RAW_LEGACY_CALLBACK

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

void recv_handler_release_slot(void *socket)
{
    napi_threadsafe_function tsfn = NULL;
    {
        std::lock_guard<std::mutex> lock(g_recv_handler_slots_mu);
        recv_handler_js_state_t *state =
          find_recv_handler_slot_by_socket_unsafe(socket);
        if (!state)
            return;
        tsfn = state->tsfn;
        reset_recv_handler_slot_unsafe(state);
    }
    if (tsfn)
        (void) napi_release_threadsafe_function(tsfn, napi_tsfn_abort);
}

void subscribe_handler_release_slot(void *socket)
{
    napi_threadsafe_function tsfn = NULL;
    {
        std::lock_guard<std::mutex> lock(g_subscribe_handler_slots_mu);
        subscribe_handler_js_state_t *state =
          find_subscribe_handler_slot_by_socket_unsafe(socket);
        if (!state)
            return;
        tsfn = state->tsfn;
        reset_subscribe_handler_slot_unsafe(state);
    }
    if (tsfn)
        (void) napi_release_threadsafe_function(tsfn, napi_tsfn_abort);
}

template <size_t Slot>
void recv_handler_slot_callback(const zlink_routing_id_t *source_rid_,
                                zlink_msg_t *parts_,
                                size_t part_count_,
                                void *userdata_)
{
    (void) userdata_;
    std::unique_ptr<recv_handler_js_payload_t> payload(
      new recv_handler_js_payload_t());
    recv_handler_js_state_t *state = &g_recv_handler_slots[Slot];
    napi_threadsafe_function tsfn = NULL;
    {
        std::lock_guard<std::mutex> lock(g_recv_handler_slots_mu);
        if (!state->used || !state->tsfn) {
            close_recv_parts(parts_, part_count_);
            return;
        }
        tsfn = state->tsfn;
        if (source_rid_ && source_rid_->size > 0) {
            payload->routing_id.assign(
              source_rid_->data, source_rid_->data + source_rid_->size);
        }
        copy_recv_parts_to_vectors(parts_, part_count_, &payload->parts);
    }
    close_recv_parts(parts_, part_count_);
    if (napi_call_threadsafe_function(tsfn, payload.get(), napi_tsfn_nonblocking)
        == napi_ok) {
        payload.release();
    }
}

template <size_t Slot>
void subscribe_handler_slot_callback(const zlink_routing_id_t *source_rid_,
                                     const char *topic_,
                                     size_t topic_len_,
                                     zlink_msg_t *parts_,
                                     size_t part_count_,
                                     void *userdata_)
{
    (void) userdata_;
    std::unique_ptr<subscribe_handler_js_payload_t> payload(
      new subscribe_handler_js_payload_t());
    subscribe_handler_js_state_t *state = &g_subscribe_handler_slots[Slot];
    napi_threadsafe_function tsfn = NULL;
    {
        std::lock_guard<std::mutex> lock(g_subscribe_handler_slots_mu);
        if (!state->used || !state->tsfn) {
            close_recv_parts(parts_, part_count_);
            return;
        }
        tsfn = state->tsfn;
        if (source_rid_ && source_rid_->size > 0) {
            payload->routing_id.assign(
              source_rid_->data, source_rid_->data + source_rid_->size);
        }
        payload->topic.assign(topic_ ? topic_ : "", topic_ ? topic_len_ : 0);
        copy_recv_parts_to_vectors(parts_, part_count_, &payload->parts);
    }
    close_recv_parts(parts_, part_count_);
    if (napi_call_threadsafe_function(tsfn, payload.get(), napi_tsfn_nonblocking)
        == napi_ok) {
        payload.release();
    }
}

typedef void (*recv_handler_slot_callback_t)(const zlink_routing_id_t *,
                                             zlink_msg_t *,
                                             size_t,
                                             void *);
typedef void (*subscribe_handler_slot_callback_t)(const zlink_routing_id_t *,
                                                  const char *,
                                                  size_t,
                                                  zlink_msg_t *,
                                                  size_t,
                                                  void *);

#define RECV_HANDLER_SLOT_CALLBACK(N) &recv_handler_slot_callback<N>
static recv_handler_slot_callback_t
  g_recv_handler_slot_callbacks[k_recv_handler_slot_count] = {
    RECV_HANDLER_SLOT_CALLBACK(0),
    RECV_HANDLER_SLOT_CALLBACK(1),
    RECV_HANDLER_SLOT_CALLBACK(2),
    RECV_HANDLER_SLOT_CALLBACK(3),
    RECV_HANDLER_SLOT_CALLBACK(4),
    RECV_HANDLER_SLOT_CALLBACK(5),
    RECV_HANDLER_SLOT_CALLBACK(6),
    RECV_HANDLER_SLOT_CALLBACK(7),
};
#undef RECV_HANDLER_SLOT_CALLBACK

#define SUBSCRIBE_HANDLER_SLOT_CALLBACK(N) &subscribe_handler_slot_callback<N>
static subscribe_handler_slot_callback_t
  g_subscribe_handler_slot_callbacks[k_subscribe_handler_slot_count] = {
    SUBSCRIBE_HANDLER_SLOT_CALLBACK(0),
    SUBSCRIBE_HANDLER_SLOT_CALLBACK(1),
    SUBSCRIBE_HANDLER_SLOT_CALLBACK(2),
    SUBSCRIBE_HANDLER_SLOT_CALLBACK(3),
    SUBSCRIBE_HANDLER_SLOT_CALLBACK(4),
    SUBSCRIBE_HANDLER_SLOT_CALLBACK(5),
    SUBSCRIBE_HANDLER_SLOT_CALLBACK(6),
    SUBSCRIBE_HANDLER_SLOT_CALLBACK(7),
};
#undef SUBSCRIBE_HANDLER_SLOT_CALLBACK

bool attach_recv_handler(napi_env env, void *socket, napi_value handler)
{
    recv_handler_js_state_t *slot = NULL;
    size_t slot_index = 0;
    {
        std::lock_guard<std::mutex> lock(g_recv_handler_slots_mu);
        if (find_recv_handler_slot_by_socket_unsafe(socket)) {
            napi_throw_error(env, NULL, "recvHandler already attached");
            return false;
        }
        slot = find_free_recv_handler_slot_unsafe();
        if (!slot) {
            napi_throw_error(env, NULL, "no free recvHandler slot");
            return false;
        }
        slot_index = static_cast<size_t>(slot - g_recv_handler_slots);
    }

    napi_value resource_name;
    napi_create_string_utf8(env, "zlink-recv-handler", NAPI_AUTO_LENGTH,
                            &resource_name);
    napi_threadsafe_function tsfn = NULL;
    napi_status tsfn_status = napi_create_threadsafe_function(
      env, handler, NULL, resource_name, 0, 1, slot,
      recv_handler_tsfn_finalize, slot, recv_handler_tsfn_call_js, &tsfn);
    if (tsfn_status != napi_ok) {
        napi_throw_error(env, NULL, "recvHandler failed to create callback queue");
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(g_recv_handler_slots_mu);
        slot->used = true;
        slot->socket = socket;
        slot->env = env;
        slot->tsfn = tsfn;
    }

    int rc = zlink_recv_handler(socket, g_recv_handler_slot_callbacks[slot_index], slot);
    if (rc != 0) {
        recv_handler_release_slot(socket);
        throw_last_error(env, "recvHandler failed");
        return false;
    }
    return true;
}

bool attach_subscribe_handler(napi_env env, void *socket, napi_value handler)
{
    subscribe_handler_js_state_t *slot = NULL;
    size_t slot_index = 0;
    {
        std::lock_guard<std::mutex> lock(g_subscribe_handler_slots_mu);
        if (find_subscribe_handler_slot_by_socket_unsafe(socket)) {
            napi_throw_error(env, NULL, "subscribeHandler already attached");
            return false;
        }
        slot = find_free_subscribe_handler_slot_unsafe();
        if (!slot) {
            napi_throw_error(env, NULL, "no free subscribeHandler slot");
            return false;
        }
        slot_index = static_cast<size_t>(slot - g_subscribe_handler_slots);
    }

    napi_value resource_name;
    napi_create_string_utf8(env, "zlink-subscribe-handler", NAPI_AUTO_LENGTH,
                            &resource_name);
    napi_threadsafe_function tsfn = NULL;
    napi_status tsfn_status = napi_create_threadsafe_function(
      env, handler, NULL, resource_name, 0, 1, slot,
      subscribe_handler_tsfn_finalize, slot, subscribe_handler_tsfn_call_js,
      &tsfn);
    if (tsfn_status != napi_ok) {
        napi_throw_error(
          env, NULL, "subscribeHandler failed to create callback queue");
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(g_subscribe_handler_slots_mu);
        slot->used = true;
        slot->socket = socket;
        slot->env = env;
        slot->tsfn = tsfn;
    }

    int rc = zlink_subscribe_handler(
      socket, g_subscribe_handler_slot_callbacks[slot_index], slot);
    if (rc != 0) {
        subscribe_handler_release_slot(socket);
        throw_last_error(env, "subscribeHandler failed");
        return false;
    }
    return true;
}

} // namespace

void release_socket_recv_handler_slot(void *socket)
{
    recv_handler_release_slot(socket);
}

void release_socket_subscribe_handler_slot(void *socket)
{
    subscribe_handler_release_slot(socket);
}

bool attach_socket_subscribe_handler(napi_env env, void *socket, napi_value handler)
{
    return attach_subscribe_handler(env, socket, handler);
}

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
    recv_handler_release_slot(sock);
    subscribe_handler_release_slot(sock);
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

napi_value socket_unbind(napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *sock = NULL;
    napi_get_value_external(env, argv[0], &sock);
    std::string addr = get_string(env, argv[1]);
    int rc = zlink_unbind(sock, addr.c_str());
    if (rc != 0)
        return throw_last_error(env, "unbind failed");
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

napi_value socket_disconnect(napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *sock = NULL;
    napi_get_value_external(env, argv[0], &sock);
    std::string addr = get_string(env, argv[1]);
    int rc = zlink_disconnect(sock, addr.c_str());
    if (rc != 0)
        return throw_last_error(env, "disconnect failed");
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

napi_value socket_attach_discovery(napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *sock = NULL;
    void *discovery = NULL;
    napi_get_value_external(env, argv[0], &sock);
    napi_get_value_external(env, argv[1], &discovery);
    int rc = zlink_socket_attach_discovery(sock, discovery);
    if (rc != 0)
        return throw_last_error(env, "socket_attach_discovery failed");
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

napi_value socket_publish(napi_env env, napi_callback_info info)
{
    napi_value argv[4];
    size_t argc = 4;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *sock = NULL;
    napi_get_value_external(env, argv[0], &sock);
    std::string topic = get_string(env, argv[1]);

    std::vector<zlink_msg_t> parts;
    bool is_buf = false;
    if (napi_is_buffer(env, argv[2], &is_buf) == napi_ok && is_buf) {
        void *data = NULL;
        size_t len = 0;
        if (napi_get_buffer_info(env, argv[2], &data, &len) != napi_ok) {
            napi_throw_type_error(env, NULL, "publish payload invalid");
            return NULL;
        }
        parts.resize(1);
        if (!init_msg_from_bytes(&parts[0], data, len))
            return throw_last_error(env, "publish failed");
    } else if (!build_msg_vector(env, argv[2], &parts)) {
        return NULL;
    }

    int32_t flags = 0;
    napi_get_value_int32(env, argv[3], &flags);
    int rc = zlink_publish(sock, topic.c_str(), parts.data(), parts.size(), flags);
    if (rc != 0) {
        close_msg_vector(parts);
        return throw_last_error(env, "publish failed");
    }

    size_t total = 0;
    for (size_t i = 0; i < parts.size(); ++i)
        total += zlink_msg_size(&parts[i]);
    napi_value out;
    napi_create_int32(env, static_cast<int32_t>(total), &out);
    return out;
}

napi_value socket_try_publish(napi_env env, napi_callback_info info)
{
    napi_value argv[3];
    size_t argc = 3;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *sock = NULL;
    napi_get_value_external(env, argv[0], &sock);
    std::string topic = get_string(env, argv[1]);

    std::vector<zlink_msg_t> parts;
    bool is_buf = false;
    if (napi_is_buffer(env, argv[2], &is_buf) == napi_ok && is_buf) {
        void *data = NULL;
        size_t len = 0;
        if (napi_get_buffer_info(env, argv[2], &data, &len) != napi_ok) {
            napi_throw_type_error(env, NULL, "publish payload invalid");
            return NULL;
        }
        parts.resize(1);
        if (!init_msg_from_bytes(&parts[0], data, len))
            return throw_last_error(env, "tryPublish failed");
    } else if (!build_msg_vector(env, argv[2], &parts)) {
        return NULL;
    }

    int rc = zlink_publish(sock, topic.c_str(), parts.data(), parts.size(),
                           ZLINK_DONTWAIT);
    if (rc == 0) {
        rc = ZLINK_SEND_RESULT_SENT;
    } else {
        rc = classify_try_send_errno();
    }
    if (rc < 0) {
        close_msg_vector(parts);
        return throw_last_error(env, "tryPublish failed");
    }
    napi_value out;
    napi_create_int32(env, rc, &out);
    return out;
}

napi_value socket_try_send(napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *sock = NULL;
    napi_get_value_external(env, argv[0], &sock);
    void *data = NULL;
    size_t len = 0;
    if (napi_get_buffer_info(env, argv[1], &data, &len) != napi_ok) {
        napi_throw_type_error(env, NULL, "send buffer invalid");
        return NULL;
    }
    zlink_msg_t msg;
    if (!init_msg_from_bytes(&msg, data, len))
        return throw_last_error(env, "trySend failed");
    int rc = zlink_send(sock, &msg, 1, ZLINK_DONTWAIT);
    if (rc == 0)
        rc = ZLINK_SEND_RESULT_SENT;
    else
        rc = classify_try_send_errno();
    if (rc < 0)
        return throw_last_error(env, "trySend failed");
    napi_value out;
    napi_create_int32(env, rc, &out);
    return out;
}

napi_value socket_try_send_parts(napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *sock = NULL;
    napi_get_value_external(env, argv[0], &sock);

    std::vector<zlink_msg_t> parts;
    if (!build_msg_vector(env, argv[1], &parts))
        return NULL;

    int rc = zlink_send(sock, parts.data(), parts.size(), ZLINK_DONTWAIT);
    if (rc == 0)
        rc = ZLINK_SEND_RESULT_SENT;
    else
        rc = classify_try_send_errno();
    if (rc < 0) {
        close_msg_vector(parts);
        return throw_last_error(env, "trySendParts failed");
    }
    napi_value out;
    napi_create_int32(env, rc, &out);
    return out;
}

napi_value socket_try_send_routing(napi_env env, napi_callback_info info)
{
    napi_value argv[3];
    size_t argc = 3;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *sock = NULL;
    napi_get_value_external(env, argv[0], &sock);

    zlink_routing_id_t routing_id;
    if (!parse_routing_id(env, argv[1], &routing_id))
        return NULL;

    void *data = NULL;
    size_t len = 0;
    if (napi_get_buffer_info(env, argv[2], &data, &len) != napi_ok) {
        napi_throw_type_error(env, NULL, "send buffer invalid");
        return NULL;
    }
    zlink_msg_t msg;
    if (!init_msg_from_bytes(&msg, data, len))
        return throw_last_error(env, "trySendTo failed");
    int rc = zlink_send_rid(sock, &routing_id, &msg, 1, ZLINK_DONTWAIT);
    if (rc == 0)
        rc = ZLINK_SEND_RESULT_SENT;
    else
        rc = classify_try_send_errno();
    if (rc < 0)
        return throw_last_error(env, "trySendTo failed");
    napi_value out;
    napi_create_int32(env, rc, &out);
    return out;
}

napi_value socket_try_send_routing_parts(napi_env env, napi_callback_info info)
{
    napi_value argv[3];
    size_t argc = 3;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *sock = NULL;
    napi_get_value_external(env, argv[0], &sock);

    zlink_routing_id_t routing_id;
    if (!parse_routing_id(env, argv[1], &routing_id))
        return NULL;

    std::vector<zlink_msg_t> parts;
    if (!build_msg_vector(env, argv[2], &parts))
        return NULL;

    int rc = zlink_send_rid(sock, &routing_id, parts.data(), parts.size(),
                            ZLINK_DONTWAIT);
    if (rc == 0)
        rc = ZLINK_SEND_RESULT_SENT;
    else
        rc = classify_try_send_errno();
    if (rc < 0) {
        close_msg_vector(parts);
        return throw_last_error(env, "trySendPartsTo failed");
    }
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

napi_value socket_try_recv_message(napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *sock = NULL;
    napi_get_value_external(env, argv[0], &sock);

    zlink_routing_id_t routing_id;
    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    int rc = recv_parts(sock, &routing_id, &parts, &part_count, ZLINK_DONTWAIT);
    if (rc != 0) {
        if (zlink_errno() == EAGAIN) {
            napi_value none;
            napi_get_null(env, &none);
            return none;
        }
        return throw_last_error(env, "tryReceive failed");
    }
    napi_value out = create_recv_message_value(env, routing_id, parts, part_count);
    close_recv_parts(parts, part_count);
    return out;
}

napi_value socket_recv_handler(napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    if (argc < 2) {
        napi_throw_type_error(env, NULL, "recvHandler requires (socket, handler)");
        return NULL;
    }
    void *sock = NULL;
    napi_get_value_external(env, argv[0], &sock);
    napi_valuetype handler_type = napi_undefined;
    napi_typeof(env, argv[1], &handler_type);
    if (handler_type != napi_function) {
        napi_throw_type_error(env, NULL, "recvHandler handler must be a function");
        return NULL;
    }
    if (!attach_recv_handler(env, sock, argv[1]))
        return NULL;
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

napi_value socket_subscribe_message(napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *sock = NULL;
    napi_get_value_external(env, argv[0], &sock);

    std::vector<char> topic(256, '\0');
    zlink_routing_id_t routing_id;
    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    size_t topic_len = topic.size();

    for (;;) {
        memset(&routing_id, 0, sizeof(routing_id));
        int rc = zlink_subscribe(
          sock, &routing_id, &parts, &part_count, topic.data(), &topic_len, 0);
        if (rc == 0) {
          napi_value out = create_subscribed_value(
            env, routing_id, topic.data(), topic_len, parts, part_count);
          close_recv_parts(parts, part_count);
          return out;
        }
        if (zlink_errno() != EMSGSIZE)
            return throw_last_error(env, "subscribe failed");
        topic.assign(topic_len > 0 ? topic_len : 1, '\0');
    }
}

napi_value socket_try_subscribe_message(napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *sock = NULL;
    napi_get_value_external(env, argv[0], &sock);

    std::vector<char> topic(256, '\0');
    zlink_routing_id_t routing_id;
    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    size_t topic_len = topic.size();

    for (;;) {
        memset(&routing_id, 0, sizeof(routing_id));
        int rc = zlink_subscribe(
          sock, &routing_id, &parts, &part_count, topic.data(), &topic_len, ZLINK_DONTWAIT);
        if (rc == 0) {
          napi_value out = create_subscribed_value(
            env, routing_id, topic.data(), topic_len, parts, part_count);
          close_recv_parts(parts, part_count);
          return out;
        }
        const int err = zlink_errno();
        if (err == EAGAIN) {
            napi_value none;
            napi_get_null(env, &none);
            return none;
        }
        if (err != EMSGSIZE)
            return throw_last_error(env, "trySubscribe failed");
        topic.assign(topic_len > 0 ? topic_len : 1, '\0');
    }
}

napi_value socket_subscribe_handler(napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    if (argc < 2) {
        napi_throw_type_error(
          env, NULL, "subscribeHandler requires (socket, handler)");
        return NULL;
    }
    void *sock = NULL;
    napi_get_value_external(env, argv[0], &sock);
    napi_valuetype handler_type = napi_undefined;
    napi_typeof(env, argv[1], &handler_type);
    if (handler_type != napi_function) {
        napi_throw_type_error(
          env, NULL, "subscribeHandler handler must be a function");
        return NULL;
    }
    if (!attach_subscribe_handler(env, sock, argv[1]))
        return NULL;
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

napi_value socket_subscription_event(napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *sock = NULL;
    napi_get_value_external(env, argv[0], &sock);

    std::vector<char> topic(256, '\0');
    zlink_routing_id_t routing_id;
    int subscribed = 0;
    size_t topic_len = topic.size();

    for (;;) {
        memset(&routing_id, 0, sizeof(routing_id));
        int rc = zlink_subscription_event(
          sock, &routing_id, &subscribed, topic.data(), &topic_len, 0);
        if (rc == 0)
            return create_subscription_event_value(
              env, routing_id, subscribed, topic.data(), topic_len);
        if (zlink_errno() != EMSGSIZE)
            return throw_last_error(env, "receiveSubscriptionEvent failed");
        topic.assign(topic_len > 0 ? topic_len : 1, '\0');
    }
}

napi_value socket_try_subscription_event(napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *sock = NULL;
    napi_get_value_external(env, argv[0], &sock);

    std::vector<char> topic(256, '\0');
    zlink_routing_id_t routing_id;
    int subscribed = 0;
    size_t topic_len = topic.size();

    for (;;) {
        memset(&routing_id, 0, sizeof(routing_id));
        int rc = zlink_subscription_event(
          sock, &routing_id, &subscribed, topic.data(), &topic_len, ZLINK_DONTWAIT);
        if (rc == 0)
            return create_subscription_event_value(
              env, routing_id, subscribed, topic.data(), topic_len);
        const int err = zlink_errno();
        if (err == EAGAIN) {
            napi_value none;
            napi_get_null(env, &none);
            return none;
        }
        if (err != EMSGSIZE)
            return throw_last_error(env, "tryReceiveSubscriptionEvent failed");
        topic.assign(topic_len > 0 ? topic_len : 1, '\0');
    }
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
    if (mode != k_stream_dispatch_none && mode != k_stream_dispatch_len32be) {
        napi_throw_range_error(env, NULL,
                               "streamAttach mode must be NONE(0) or LEN32BE(1)");
        return NULL;
    }

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
        slot->dispatch_mode = mode;
        slot->len32be_pending.clear();
        slot->peer_routing_ids.clear();
    }

    int rc = zlink_stream_attach_raw(
      sock, g_stream_slot_raw_legacy_callbacks[slot_index]);
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

    if (index < 0) {
        napi_value none;
        napi_get_null(env, &none);
        return none;
    }

    std::lock_guard<std::mutex> lock(g_stream_slots_mu);
    stream_js_state_t *state = find_stream_slot_by_socket_unsafe(sock);
    if (!state || static_cast<size_t>(index) >= state->peer_routing_ids.size()) {
        napi_value none;
        napi_get_null(env, &none);
        return none;
    }

    const std::vector<unsigned char> &rid = state->peer_routing_ids[index];
    napi_value out;
    napi_create_buffer_copy(
      env, rid.size(), rid.empty() ? NULL : rid.data(), NULL, &out);
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
    const size_t public_payload_len = payload_len;

    int32_t flags = 0;
    if (argc >= 4)
        napi_get_value_int32(env, argv[3], &flags);

    std::vector<unsigned char> framed_payload;
    {
        std::lock_guard<std::mutex> lock(g_stream_slots_mu);
        stream_js_state_t *state = find_stream_slot_by_socket_unsafe(sock);
        if (state && state->dispatch_mode == k_stream_dispatch_len32be) {
            framed_payload.resize(payload_len + 4);
            framed_payload[0] = static_cast<unsigned char>((payload_len >> 24) & 0xFF);
            framed_payload[1] = static_cast<unsigned char>((payload_len >> 16) & 0xFF);
            framed_payload[2] = static_cast<unsigned char>((payload_len >> 8) & 0xFF);
            framed_payload[3] = static_cast<unsigned char>(payload_len & 0xFF);
            if (payload_len > 0) {
                memcpy(
                  framed_payload.data() + 4, payload_data, payload_len);
            }
            payload_data = framed_payload.data();
            payload_len = framed_payload.size();
        }
    }

    zlink_routing_id_t rid;
    memset(&rid, 0, sizeof(rid));
    rid.size = static_cast<uint8_t>(rid_len);
    memcpy(rid.data, rid_data, rid_len);

    zlink_msg_t msg;
    if (!init_msg_from_bytes(&msg, payload_data, payload_len))
        return throw_last_error(env, "streamSend failed");

    int rc = zlink_send_rid(sock, &rid, &msg, 1, flags);
    if (rc != 0)
        (void) zlink_msg_close(&msg);
    if (rc < 0)
        return throw_last_error(env, "streamSend failed");

    napi_value out;
    napi_create_int32(env, static_cast<int32_t>(public_payload_len), &out);
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
    size_t len = initial_getopt_buffer_len(opt);
    void *data = NULL;
    napi_value buf;
    napi_create_buffer(env, len, &data, &buf);
    int rc = get_socket_option(sock, opt, data, &len);
    if (rc != 0)
        return throw_last_error(env, "getsockopt failed");
    if (len == initial_getopt_buffer_len(opt))
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
    int rc = zlink_socket_monitor_recv(mon, &evt, 0);
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

napi_value monitor_try_recv(napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *mon = NULL;
    napi_get_value_external(env, argv[0], &mon);

    zlink_monitor_event_t evt;
    int rc = zlink_socket_monitor_recv(mon, &evt, ZLINK_DONTWAIT);
    if (rc != 0) {
        if (zlink_errno() == EAGAIN) {
            napi_value none;
            napi_get_null(env, &none);
            return none;
        }
        return throw_last_error(env, "monitor_try_recv failed");
    }

    napi_value obj;
    napi_create_object(env, &obj);
    napi_value event, value, local, remote;
    napi_create_int64(env, (int64_t) evt.event, &event);
    napi_create_int64(env, (int64_t) evt.value, &value);
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
    napi_value snd_pending, rcv_pending;
    napi_create_uint32(env, static_cast<uint32_t>(snapshot.source_kind),
                       &source_kind);
    napi_create_uint32(env, snapshot.state_flags, &state_flags);
    napi_create_uint32(env, snapshot.detail_flags, &detail_flags);
    napi_create_int64(env, static_cast<int64_t>(snapshot.snd_pending_msgs),
                      &snd_pending);
    napi_create_int64(env, static_cast<int64_t>(snapshot.rcv_pending_msgs),
                      &rcv_pending);
    napi_set_named_property(env, obj, "sourceKind", source_kind);
    napi_set_named_property(env, obj, "stateFlags", state_flags);
    napi_set_named_property(env, obj, "detailFlags", detail_flags);
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
