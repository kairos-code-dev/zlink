/* SPDX-License-Identifier: MPL-2.0 */

#include "addon_core_api.h"
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
static const size_t k_router_handler_slot_count = 8;
static const size_t k_subscribe_handler_slot_count = 8;
static const size_t k_send_ready_handler_slot_count = 8;
static const size_t k_socket_monitor_handler_slot_count = 8;
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
        return ZLINK_SUBMIT_BACKPRESSURED;
#ifdef ENOTCONN
    case ENOTCONN:
#endif
#ifdef EHOSTUNREACH
    case EHOSTUNREACH:
#endif
#ifdef ETIMEDOUT
    case ETIMEDOUT:
#endif
        return ZLINK_SUBMIT_NOT_CONNECTED;
    default:
        return -1;
    }
}

struct stream_js_payload_t
{
    std::vector<unsigned char> routing_id;
    std::vector<std::vector<unsigned char> > packets;
};

void close_recv_parts(zlink_msg_t *parts, size_t part_count);

struct recv_handler_js_payload_t
{
    recv_handler_js_payload_t() : part_count(0) {}
    ~recv_handler_js_payload_t()
    {
        if (part_count > 0)
            close_recv_parts(parts.data(), part_count);
    }

    std::vector<unsigned char> routing_id;
    std::vector<zlink_msg_t> parts;
    size_t part_count;
};

struct router_handler_js_payload_t
{
    std::vector<unsigned char> routing_id;
    std::vector<unsigned char> spot_routing_id;
    uint64_t request_seq;
    std::vector<std::vector<unsigned char> > parts;
};

struct subscribe_handler_js_payload_t
{
    std::vector<unsigned char> routing_id;
    std::string topic;
    std::vector<std::vector<unsigned char> > parts;
};

struct socket_monitor_handler_js_payload_t
{
    zlink_monitor_event_t event;
};

struct request_result_js_payload_t
{
    request_result_js_payload_t() : errnum(0), part_count(0) {}
    ~request_result_js_payload_t()
    {
        if (part_count > 0)
            close_recv_parts(parts.data(), part_count);
    }

    int errnum;
    std::vector<zlink_msg_t> parts;
    size_t part_count;
};

struct request_js_state_t
{
    request_js_state_t () : env (NULL), tsfn (NULL) {}

    napi_env env;
    napi_threadsafe_function tsfn;
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

struct router_handler_js_state_t
{
    router_handler_js_state_t () : used (false), socket (NULL), env (NULL), tsfn (NULL) {}

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

struct send_ready_handler_js_state_t
{
    send_ready_handler_js_state_t () : used (false), socket (NULL), env (NULL), tsfn (NULL) {}

    bool used;
    void *socket;
    napi_env env;
    napi_threadsafe_function tsfn;
};

struct socket_monitor_handler_js_state_t
{
    socket_monitor_handler_js_state_t ()
      : used (false), monitor (NULL), env (NULL), tsfn (NULL) {}

    bool used;
    void *monitor;
    napi_env env;
    napi_threadsafe_function tsfn;
};

static std::mutex g_recv_handler_slots_mu;
static recv_handler_js_state_t g_recv_handler_slots[k_recv_handler_slot_count];
static std::mutex g_router_handler_slots_mu;
static router_handler_js_state_t
  g_router_handler_slots[k_router_handler_slot_count];
static std::mutex g_subscribe_handler_slots_mu;
static subscribe_handler_js_state_t
  g_subscribe_handler_slots[k_subscribe_handler_slot_count];
static std::mutex g_send_ready_handler_slots_mu;
static send_ready_handler_js_state_t
  g_send_ready_handler_slots[k_send_ready_handler_slot_count];
static std::mutex g_socket_monitor_handler_slots_mu;
static socket_monitor_handler_js_state_t
  g_socket_monitor_handler_slots[k_socket_monitor_handler_slot_count];

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

router_handler_js_state_t *find_router_handler_slot_by_socket_unsafe(void *socket)
{
    for (size_t i = 0; i < k_router_handler_slot_count; ++i) {
        if (g_router_handler_slots[i].used
            && g_router_handler_slots[i].socket == socket) {
            return &g_router_handler_slots[i];
        }
    }
    return NULL;
}

router_handler_js_state_t *find_free_router_handler_slot_unsafe()
{
    for (size_t i = 0; i < k_router_handler_slot_count; ++i) {
        if (!g_router_handler_slots[i].used)
            return &g_router_handler_slots[i];
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

send_ready_handler_js_state_t *find_send_ready_handler_slot_by_socket_unsafe(
  void *socket)
{
    for (size_t i = 0; i < k_send_ready_handler_slot_count; ++i) {
        if (g_send_ready_handler_slots[i].used
            && g_send_ready_handler_slots[i].socket == socket) {
            return &g_send_ready_handler_slots[i];
        }
    }
    return NULL;
}

send_ready_handler_js_state_t *find_free_send_ready_handler_slot_unsafe()
{
    for (size_t i = 0; i < k_send_ready_handler_slot_count; ++i) {
        if (!g_send_ready_handler_slots[i].used)
            return &g_send_ready_handler_slots[i];
    }
    return NULL;
}

socket_monitor_handler_js_state_t *find_socket_monitor_handler_slot_by_monitor_unsafe(
  void *monitor)
{
    for (size_t i = 0; i < k_socket_monitor_handler_slot_count; ++i) {
        if (g_socket_monitor_handler_slots[i].used
            && g_socket_monitor_handler_slots[i].monitor == monitor) {
            return &g_socket_monitor_handler_slots[i];
        }
    }
    return NULL;
}

socket_monitor_handler_js_state_t *find_free_socket_monitor_handler_slot_unsafe()
{
    for (size_t i = 0; i < k_socket_monitor_handler_slot_count; ++i) {
        if (!g_socket_monitor_handler_slots[i].used)
            return &g_socket_monitor_handler_slots[i];
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

void reset_router_handler_slot_unsafe(router_handler_js_state_t *state)
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

void reset_send_ready_handler_slot_unsafe(send_ready_handler_js_state_t *state)
{
    if (!state)
        return;
    state->used = false;
    state->socket = NULL;
    state->env = NULL;
    state->tsfn = NULL;
}

void reset_socket_monitor_handler_slot_unsafe(
  socket_monitor_handler_js_state_t *state)
{
    if (!state)
        return;
    state->used = false;
    state->monitor = NULL;
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

void copy_routing_id(zlink_routing_id_t *out, const zlink_routing_id_t *in)
{
    if (!out)
        return;
    if (in)
        memcpy(out, in, sizeof(*out));
    else
        memset(out, 0, sizeof(*out));
}

bool append_msg_move(std::vector<zlink_msg_t> *parts, zlink_msg_t *part)
{
    if (!parts || !part)
        return false;

    parts->emplace_back();
    zlink_msg_t *slot = &parts->back();
    if (zlink_msg_init(slot) != 0) {
        parts->pop_back();
        return false;
    }
    if (zlink_msg_move(slot, part) != 0) {
        zlink_msg_close(slot);
        parts->pop_back();
        return false;
    }
    return true;
}

int collect_recv_parts(void *sock,
                       zlink_msg_t *first_part,
                       zlink_part_flag_t has_more,
                       std::vector<zlink_msg_t> *parts)
{
    if (!parts) {
        if (first_part)
            zlink_msg_close(first_part);
        errno = EFAULT;
        return ZLINK_RECV_INTERNAL_ERROR;
    }

    parts->clear();
    if (!append_msg_move(parts, first_part)) {
        if (first_part)
            zlink_msg_close(first_part);
        errno = ENOMEM;
        return ZLINK_RECV_INTERNAL_ERROR;
    }

    while (has_more) {
        const zlink_routing_id_t *source_rid = NULL;
        zlink_msg_t next_part;
        if (zlink_msg_init(&next_part) != 0) {
            close_msg_vector(*parts);
            parts->clear();
            return ZLINK_RECV_INTERNAL_ERROR;
        }
        zlink_part_flag_t more = ZLINK_PART_FINAL;
        int rc = zlink_recv_part(
          sock, &source_rid, &next_part, &more, ZLINK_RECV_FLAGS_DONTWAIT);
        if (rc != ZLINK_RECV_OK) {
            zlink_msg_close(&next_part);
            close_msg_vector(*parts);
            parts->clear();
            return rc;
        }
        if (!append_msg_move(parts, &next_part)) {
            zlink_msg_close(&next_part);
            close_msg_vector(*parts);
            parts->clear();
            errno = ENOMEM;
            return ZLINK_RECV_INTERNAL_ERROR;
        }
        has_more = more;
    }

    return ZLINK_RECV_OK;
}

int recv_parts(void *sock,
               zlink_routing_id_t *routing_id,
               std::vector<zlink_msg_t> *parts,
               int32_t flags)
{
    const zlink_routing_id_t *source_rid = NULL;
    zlink_msg_t first_part;
    if (zlink_msg_init(&first_part) != 0)
        return ZLINK_RECV_INTERNAL_ERROR;
    zlink_part_flag_t has_more = ZLINK_PART_FINAL;

    copy_routing_id(routing_id, NULL);
    if (parts)
        parts->clear();

    int rc = zlink_recv_part(
      sock,
      &source_rid,
      &first_part,
      &has_more,
      static_cast<zlink_recv_flags_t>(flags));
    if (rc != ZLINK_RECV_OK) {
        zlink_msg_close(&first_part);
        return rc;
    }

    copy_routing_id(routing_id, source_rid);
    return collect_recv_parts(sock, &first_part, has_more, parts);
}

int subscribe_parts(void *sock,
                    zlink_routing_id_t *routing_id,
                    char *topic,
                    size_t topic_capacity,
                    size_t *topic_len,
                    std::vector<zlink_msg_t> *parts,
                    int32_t flags)
{
    const zlink_routing_id_t *source_rid = NULL;
    zlink_msg_t first_part;
    if (zlink_msg_init(&first_part) != 0)
        return ZLINK_RECV_INTERNAL_ERROR;
    zlink_part_flag_t has_more = ZLINK_PART_FINAL;

    copy_routing_id(routing_id, NULL);
    if (parts)
        parts->clear();

    int rc = zlink_subscribe_part(
      sock,
      &source_rid,
      topic,
      topic_capacity,
      topic_len,
      &first_part,
      &has_more,
      static_cast<zlink_recv_flags_t>(flags));
    if (rc != ZLINK_RECV_OK) {
        zlink_msg_close(&first_part);
        return rc;
    }

    copy_routing_id(routing_id, source_rid);
    return collect_recv_parts(sock, &first_part, has_more, parts);
}

int router_recv_parts(void *router,
                      zlink_routing_id_t *peer_rid,
                      zlink_routing_id_t *spot_rid,
                      uint64_t *request_seq,
                      std::vector<zlink_msg_t> *parts,
                      int32_t flags)
{
    const zlink_routing_id_t *peer_rid_ptr = NULL;
    const zlink_routing_id_t *spot_rid_ptr = NULL;
    zlink_msg_t first_part;
    if (zlink_msg_init(&first_part) != 0)
        return ZLINK_RECV_INTERNAL_ERROR;
    zlink_part_flag_t has_more = ZLINK_PART_FINAL;

    copy_routing_id(peer_rid, NULL);
    copy_routing_id(spot_rid, NULL);
    if (request_seq)
        *request_seq = 0;
    if (parts)
        parts->clear();

    int rc = zlink_router_recv_part(
      router,
      &peer_rid_ptr,
      &spot_rid_ptr,
      request_seq,
      &first_part,
      &has_more,
      static_cast<zlink_recv_flags_t>(flags));
    if (rc != ZLINK_RECV_OK) {
        zlink_msg_close(&first_part);
        return rc;
    }

    copy_routing_id(peer_rid, peer_rid_ptr);
    copy_routing_id(spot_rid, spot_rid_ptr);
    return collect_recv_parts(router, &first_part, has_more, parts);
}

int send_parts(void *sock,
               zlink_msg_t *parts,
               size_t part_count,
               zlink_send_flags_t flags)
{
    if (!parts || part_count == 0) {
        errno = EFAULT;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }

    for (size_t i = 0; i < part_count; ++i) {
        zlink_part_flag_t part_flag =
          (i + 1u < part_count) ? ZLINK_PART_MORE : ZLINK_PART_FINAL;
        int rc = zlink_send_part(sock, &parts[i], flags, part_flag);
        if (rc != ZLINK_SUBMIT_OK) {
            for (size_t j = i + 1u; j < part_count; ++j)
                zlink_msg_close(&parts[j]);
            return rc;
        }
    }

    return ZLINK_SUBMIT_OK;
}

int send_parts_rid(void *sock,
                   const zlink_routing_id_t *routing_id,
                   zlink_msg_t *parts,
                   size_t part_count,
                   zlink_send_flags_t flags)
{
    if (!parts || part_count == 0) {
        errno = EFAULT;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }

    for (size_t i = 0; i < part_count; ++i) {
        zlink_part_flag_t part_flag =
          (i + 1u < part_count) ? ZLINK_PART_MORE : ZLINK_PART_FINAL;
        int rc = zlink_send_part_rid(sock, routing_id, &parts[i], flags, part_flag);
        if (rc != ZLINK_SUBMIT_OK) {
            for (size_t j = i + 1u; j < part_count; ++j)
                zlink_msg_close(&parts[j]);
            return rc;
        }
    }

    return ZLINK_SUBMIT_OK;
}

int publish_parts(void *sock,
                  const char *topic,
                  zlink_msg_t *parts,
                  size_t part_count,
                  zlink_send_flags_t flags)
{
    if (!parts || part_count == 0) {
        errno = EFAULT;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }

    for (size_t i = 0; i < part_count; ++i) {
        zlink_part_flag_t part_flag =
          (i + 1u < part_count) ? ZLINK_PART_MORE : ZLINK_PART_FINAL;
        int rc = zlink_publish_part(sock, topic, &parts[i], flags, part_flag);
        if (rc != ZLINK_SUBMIT_OK) {
            for (size_t j = i + 1u; j < part_count; ++j)
                zlink_msg_close(&parts[j]);
            return rc;
        }
    }

    return ZLINK_SUBMIT_OK;
}

int dealer_request_parts(void *dealer,
                         zlink_msg_t *parts,
                         size_t part_count,
                         zlink_reply_handler_fn handler,
                         void *userdata,
                         zlink_send_flags_t flags,
                         uint32_t timeout_ms)
{
    if (!parts || part_count == 0) {
        errno = EFAULT;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }

    for (size_t i = 0; i < part_count; ++i) {
        const bool is_final = (i + 1u == part_count);
        const zlink_part_flag_t part_flag =
          is_final ? ZLINK_PART_FINAL : ZLINK_PART_MORE;
        int rc = zlink_dealer_request_part(
          dealer,
          &parts[i],
          flags,
          part_flag,
          is_final ? timeout_ms : 0u,
          is_final ? handler : NULL,
          is_final ? userdata : NULL);
        if (rc != ZLINK_SUBMIT_OK) {
            for (size_t j = i + 1u; j < part_count; ++j)
                zlink_msg_close(&parts[j]);
            return rc;
        }
    }

    return ZLINK_SUBMIT_OK;
}

int router_request_parts(void *router,
                         const zlink_routing_id_t *peer_rid,
                         zlink_msg_t *parts,
                         size_t part_count,
                         zlink_reply_handler_fn handler,
                         void *userdata,
                         zlink_send_flags_t flags,
                         uint32_t timeout_ms)
{
    if (!parts || part_count == 0) {
        errno = EFAULT;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }

    for (size_t i = 0; i < part_count; ++i) {
        const bool is_final = (i + 1u == part_count);
        const zlink_part_flag_t part_flag =
          is_final ? ZLINK_PART_FINAL : ZLINK_PART_MORE;
        int rc = zlink_router_request_part(
          router,
          peer_rid,
          &parts[i],
          flags,
          part_flag,
          is_final ? timeout_ms : 0u,
          is_final ? handler : NULL,
          is_final ? userdata : NULL);
        if (rc != ZLINK_SUBMIT_OK) {
            for (size_t j = i + 1u; j < part_count; ++j)
                zlink_msg_close(&parts[j]);
            return rc;
        }
    }

    return ZLINK_SUBMIT_OK;
}

int router_reply_parts(void *router,
                       const zlink_routing_id_t *peer_rid,
                       uint64_t request_seq,
                       zlink_msg_t *parts,
                       size_t part_count)
{
    if (!parts || part_count == 0) {
        errno = EFAULT;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }

    for (size_t i = 0; i < part_count; ++i) {
        zlink_part_flag_t part_flag =
          (i + 1u < part_count) ? ZLINK_PART_MORE : ZLINK_PART_FINAL;
        int rc =
          zlink_router_reply_part(router, peer_rid, request_seq, &parts[i], part_flag);
        if (rc != ZLINK_SUBMIT_OK) {
            for (size_t j = i + 1u; j < part_count; ++j)
                zlink_msg_close(&parts[j]);
            return rc;
        }
    }

    return ZLINK_SUBMIT_OK;
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

void finalize_external_msg_buffer(napi_env env, void *data, void *hint)
{
    (void) env;
    (void) data;
    zlink_msg_t *msg = static_cast<zlink_msg_t *>(hint);
    if (!msg)
        return;
    zlink_msg_close(msg);
    delete msg;
}

napi_value create_message_data_buffer(napi_env env, zlink_msg_t *msg)
{
    const size_t size = zlink_msg_size(msg);
    if (size == 0)
        return create_buffer_copy_or_empty(env, NULL, 0);

    zlink_msg_t *owned = new (std::nothrow) zlink_msg_t;
    if (!owned) {
        napi_throw_error(env, NULL, "message buffer allocation failed");
        return NULL;
    }
    if (zlink_msg_init(owned) != 0) {
        delete owned;
        return throw_last_error(env, "message buffer init failed");
    }
    if (zlink_msg_move(owned, msg) != 0) {
        zlink_msg_close(owned);
        delete owned;
        return throw_last_error(env, "message buffer move failed");
    }

    napi_value data;
    napi_status status = napi_create_external_buffer(
      env,
      size,
      zlink_msg_data(owned),
      finalize_external_msg_buffer,
      owned,
      &data);
    if (status != napi_ok) {
        zlink_msg_close(owned);
        delete owned;
        napi_throw_error(env, NULL, "message buffer creation failed");
        return NULL;
    }
    return data;
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

napi_value create_message_properties_snapshot(napi_env env,
                                              const zlink_routing_id_t *routing_id,
                                              zlink_msg_t *msg);
napi_value create_message_snapshot_value(napi_env env,
                                        const zlink_routing_id_t *routing_id,
                                        zlink_msg_t *msg);

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
        napi_value part = create_message_snapshot_value(
          env, &routing_id, &parts[i]);
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

napi_value create_router_recv_message_value(napi_env env,
                                            const zlink_routing_id_t &routing_id,
                                            const zlink_routing_id_t *spot_routing_id,
                                            uint64_t request_seq,
                                            zlink_msg_t *parts,
                                            size_t part_count)
{
    napi_value obj = create_recv_message_value(env, routing_id, parts, part_count);
    napi_value request_seq_value;
    if (request_seq == 0) {
        napi_get_null(env, &request_seq_value);
    } else {
        napi_create_bigint_uint64(env, request_seq, &request_seq_value);
    }
    napi_set_named_property(env, obj, "requestSeq", request_seq_value);
    napi_value spot_rid_value;
    if (spot_routing_id && spot_routing_id->size > 0) {
        spot_rid_value = create_routing_id_value(env, *spot_routing_id);
    } else {
        napi_get_null(env, &spot_rid_value);
    }
    napi_set_named_property(env, obj, "spotRid", spot_rid_value);
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

napi_value create_socket_monitor_event_value(napi_env env,
                                             const zlink_monitor_event_t &event)
{
    napi_value obj;
    napi_create_object(env, &obj);

    napi_value value;
    napi_create_int64(env, static_cast<int64_t>(event.event), &value);
    napi_set_named_property(env, obj, "event", value);

    napi_create_int64(env, static_cast<int64_t>(event.value), &value);
    napi_set_named_property(env, obj, "value", value);

    napi_value local;
    napi_create_string_utf8(
      env, event.local_addr, NAPI_AUTO_LENGTH, &local);
    napi_set_named_property(env, obj, "local", local);

    napi_value remote;
    napi_create_string_utf8(
      env, event.remote_addr, NAPI_AUTO_LENGTH, &remote);
    napi_set_named_property(env, obj, "remote", remote);

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
        napi_value part = create_message_snapshot_value(
          env, &routing_id, &parts[i]);
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

napi_value create_message_properties_snapshot(napi_env env,
                                              const zlink_routing_id_t *routing_id,
                                              zlink_msg_t *msg)
{
    napi_value props;
    napi_create_object(env, &props);

    const auto set_property = [env, props](const char *name, const char *value) {
        if (!value)
            return;
        napi_value out;
        napi_create_string_utf8(env, value, NAPI_AUTO_LENGTH, &out);
        napi_set_named_property(env, props, name, out);
    };

    set_property("Socket-Type", zlink_msg_gets(msg, "Socket-Type"));
    set_property("User-Id", zlink_msg_gets(msg, "User-Id"));
    set_property("Peer-Address", zlink_msg_gets(msg, "Peer-Address"));

    const char *routing_id_value = zlink_msg_gets(msg, "Routing-Id");
    if (!routing_id_value && routing_id && routing_id->size > 0) {
        napi_value out;
        napi_create_string_utf8(
          env,
          reinterpret_cast<const char *>(routing_id->data),
          routing_id->size,
          &out);
        napi_set_named_property(env, props, "Routing-Id", out);
        napi_set_named_property(env, props, "Identity", out);
    } else {
        set_property("Routing-Id", routing_id_value);
        if (routing_id_value)
            set_property("Identity", routing_id_value);
    }

    if (!routing_id_value)
        set_property("Identity", zlink_msg_gets(msg, "Identity"));

    return props;
}

napi_value create_message_snapshot_value(napi_env env,
                                        const zlink_routing_id_t *routing_id,
                                        zlink_msg_t *msg)
{
    napi_value obj;
    napi_create_object(env, &obj);

    zlink_config_result_t refcnt_err = ZLINK_CONFIG_OK;
    const int refcnt = zlink_msg_refcnt(msg, &refcnt_err);
    if (refcnt_err != ZLINK_CONFIG_OK)
        return throw_last_error(env, "message refcnt failed");
    napi_value ref_count;
    napi_create_int32(env, refcnt, &ref_count);
    napi_value props = create_message_properties_snapshot(env, routing_id, msg);
    napi_value data = create_message_data_buffer(env, msg);
    if (!data)
        return NULL;

    napi_set_named_property(env, obj, "data", data);
    napi_set_named_property(env, obj, "refCount", ref_count);
    napi_set_named_property(env, obj, "properties", props);
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
    case ZLINK_ROUTER_OPT_PROBE:
        return zlink_set_router_option(sock, ZLINK_ROUTER_OPT_PROBE, data, len);
    case ZLINK_ROUTER_OPT_CONNECT_ROUTING_ID:
        return zlink_set_router_option(sock, ZLINK_ROUTER_OPT_CONNECT_ROUTING_ID,
                                       data, len);
    case ZLINK_ROUTER_OPT_REQUEST_TIMEOUT_MS:
        return zlink_set_router_option(sock, ZLINK_ROUTER_OPT_REQUEST_TIMEOUT_MS,
                                       data, len);
    case ZLINK_ROUTER_OPT_WEIGHT:
        return zlink_set_router_option(sock, ZLINK_ROUTER_OPT_WEIGHT, data, len);
    case ZLINK_DEALER_OPT_PROBE:
        return zlink_set_dealer_option(sock, ZLINK_DEALER_OPT_PROBE, data, len);
    case ZLINK_DEALER_OPT_REQUEST_TIMEOUT_MS:
        return zlink_set_dealer_option(sock, ZLINK_DEALER_OPT_REQUEST_TIMEOUT_MS,
                                       data, len);
    case ZLINK_DEALER_OPT_WEIGHT:
        return zlink_set_dealer_option(sock, ZLINK_DEALER_OPT_WEIGHT, data, len);
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
    case ZLINK_ROUTER_OPT_PROBE:
        return zlink_get_router_option(sock, ZLINK_ROUTER_OPT_PROBE, data, len);
    case ZLINK_ROUTER_OPT_CONNECT_ROUTING_ID:
        return zlink_get_router_option(sock, ZLINK_ROUTER_OPT_CONNECT_ROUTING_ID,
                                       data, len);
    case ZLINK_ROUTER_OPT_REQUEST_TIMEOUT_MS:
        return zlink_get_router_option(sock, ZLINK_ROUTER_OPT_REQUEST_TIMEOUT_MS,
                                       data, len);
    case ZLINK_ROUTER_OPT_WEIGHT:
        return zlink_get_router_option(sock, ZLINK_ROUTER_OPT_WEIGHT, data, len);
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

void router_handler_tsfn_finalize(napi_env env,
                                  void *finalize_data,
                                  void *finalize_hint)
{
    (void) env;
    (void) finalize_hint;
    router_handler_js_state_t *state =
      static_cast<router_handler_js_state_t *>(finalize_data);
    if (!state)
        return;
    std::lock_guard<std::mutex> lock(g_router_handler_slots_mu);
    reset_router_handler_slot_unsafe(state);
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

void send_ready_handler_tsfn_finalize(napi_env env,
                                      void *finalize_data,
                                      void *finalize_hint)
{
    (void) env;
    (void) finalize_hint;
    send_ready_handler_js_state_t *state =
      static_cast<send_ready_handler_js_state_t *>(finalize_data);
    if (!state)
        return;
    std::lock_guard<std::mutex> lock(g_send_ready_handler_slots_mu);
    reset_send_ready_handler_slot_unsafe(state);
}

void socket_monitor_handler_tsfn_finalize(napi_env env,
                                          void *finalize_data,
                                          void *finalize_hint)
{
    (void) env;
    (void) finalize_hint;
    socket_monitor_handler_js_state_t *state =
      static_cast<socket_monitor_handler_js_state_t *>(finalize_data);
    if (!state)
        return;
    std::lock_guard<std::mutex> lock(g_socket_monitor_handler_slots_mu);
    reset_socket_monitor_handler_slot_unsafe(state);
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
        napi_value part_buf = create_message_data_buffer(env, &payload->parts[i]);
        if (!part_buf) {
            return;
        }
        napi_set_element(env, argv[1], static_cast<uint32_t>(i), part_buf);
    }

    napi_value recv;
    napi_value this_arg;
    napi_get_undefined(env, &this_arg);
    (void) napi_call_function(env, this_arg, js_cb, 2, argv, &recv);
}

void router_handler_tsfn_call_js(napi_env env,
                                 napi_value js_cb,
                                 void *context,
                                 void *data)
{
    std::unique_ptr<router_handler_js_payload_t> payload(
      static_cast<router_handler_js_payload_t *>(data));
    (void) context;
    if (!env || !js_cb || !payload)
        return;

    napi_value argv[1];
    napi_create_object(env, &argv[0]);

    zlink_routing_id_t rid;
    memset(&rid, 0, sizeof(rid));
    if (!payload->routing_id.empty()) {
        rid.size = payload->routing_id.size();
        memcpy(rid.data, payload->routing_id.data(), rid.size);
    }

    napi_value rid_value = create_routing_id_value(env, rid);
    napi_set_named_property(env, argv[0], "routingId", rid_value);

    napi_value spot_rid_value;
    if (!payload->spot_routing_id.empty()) {
        zlink_routing_id_t spot_rid;
        memset(&spot_rid, 0, sizeof(spot_rid));
        spot_rid.size = payload->spot_routing_id.size();
        memcpy(spot_rid.data, payload->spot_routing_id.data(), spot_rid.size);
        spot_rid_value = create_routing_id_value(env, spot_rid);
    } else {
        napi_get_null(env, &spot_rid_value);
    }
    napi_set_named_property(env, argv[0], "spotRid", spot_rid_value);

    napi_value request_seq_value;
    if (payload->request_seq == 0) {
        napi_get_null(env, &request_seq_value);
    } else {
        napi_create_bigint_uint64(env, payload->request_seq, &request_seq_value);
    }
    napi_set_named_property(env, argv[0], "requestSeq", request_seq_value);

    napi_value parts_array;
    if (napi_create_array_with_length(env, payload->parts.size(), &parts_array)
        != napi_ok) {
        return;
    }
    for (size_t i = 0; i < payload->parts.size(); ++i) {
        const std::vector<unsigned char> &part = payload->parts[i];
        napi_value part_obj;
        napi_create_object(env, &part_obj);

        napi_value part_buf;
        if (napi_create_buffer_copy(
              env, part.size(), part.empty() ? NULL : part.data(), NULL,
              &part_buf)
            != napi_ok) {
            return;
        }
        napi_set_named_property(env, part_obj, "data", part_buf);
        napi_set_element(env, parts_array, static_cast<uint32_t>(i), part_obj);
    }
    napi_set_named_property(env, argv[0], "parts", parts_array);

    napi_value recv;
    napi_value this_arg;
    napi_get_undefined(env, &this_arg);
    (void) napi_call_function(env, this_arg, js_cb, 1, argv, &recv);
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

void send_ready_handler_tsfn_call_js(napi_env env,
                                     napi_value js_cb,
                                     void *context,
                                     void *data)
{
    std::unique_ptr<int> payload(static_cast<int *>(data));
    (void) context;
    if (!env || !js_cb || !payload)
        return;

    napi_value recv;
    napi_value this_arg;
    napi_get_undefined(env, &this_arg);
    (void) napi_call_function(env, this_arg, js_cb, 0, NULL, &recv);
}

void socket_monitor_handler_tsfn_call_js(napi_env env,
                                         napi_value js_cb,
                                         void *context,
                                         void *data)
{
    std::unique_ptr<socket_monitor_handler_js_payload_t> payload(
      static_cast<socket_monitor_handler_js_payload_t *>(data));
    (void) context;
    if (!env || !js_cb || !payload)
        return;

    napi_value argv[1];
    argv[0] = create_socket_monitor_event_value(env, payload->event);
    napi_value recv;
    napi_value this_arg;
    napi_get_undefined(env, &this_arg);
    (void) napi_call_function(env, this_arg, js_cb, 1, argv, &recv);
}

void request_tsfn_call_js(napi_env env,
                          napi_value js_cb,
                          void *context,
                          void *data)
{
    std::unique_ptr<request_result_js_payload_t> payload(
      static_cast<request_result_js_payload_t *>(data));
    (void) context;
    if (!env || !js_cb || !payload)
        return;

    napi_value argv[2];
    napi_create_int32(env, payload->errnum, &argv[0]);
    if (payload->errnum != 0) {
        napi_get_null(env, &argv[1]);
    } else {
        napi_value parts_array;
        napi_create_array_with_length(env, payload->parts.size(), &parts_array);
        for (size_t i = 0; i < payload->parts.size(); ++i) {
            napi_value part_buf = create_message_data_buffer(env, &payload->parts[i]);
            if (!part_buf)
                return;
            napi_set_element(env, parts_array, static_cast<uint32_t>(i), part_buf);
        }
        argv[1] = parts_array;
    }

    napi_value recv;
    napi_value this_arg;
    napi_get_undefined(env, &this_arg);
    (void) napi_call_function(env, this_arg, js_cb, 2, argv, &recv);
}

void request_reply_callback_trampoline(zlink_request_result_t errnum_,
                                       zlink_msg_t *parts_,
                                       size_t part_count_,
                                       void *userdata_)
{
    request_js_state_t *state = static_cast<request_js_state_t *>(userdata_);
    if (!state || !state->tsfn) {
        close_recv_parts(parts_, part_count_);
        return;
    }

    std::unique_ptr<request_result_js_payload_t> payload(
      new request_result_js_payload_t());
    payload->errnum = errnum_;
    if (errnum_ == 0) {
        payload->parts.resize(part_count_);
        for (size_t i = 0; i < part_count_; ++i) {
            if (zlink_msg_init(&payload->parts[i]) != 0) {
                close_recv_parts(parts_, part_count_);
                return;
            }
            payload->part_count = i + 1;
            if (zlink_msg_move(&payload->parts[i], &parts_[i]) != 0) {
                close_recv_parts(parts_, part_count_);
                return;
            }
        }
    }
    close_recv_parts(parts_, part_count_);

    if (napi_call_threadsafe_function(
          state->tsfn, payload.get(), napi_tsfn_nonblocking)
        == napi_ok) {
        payload.release();
    }
    (void) napi_release_threadsafe_function(state->tsfn, napi_tsfn_release);
    state->tsfn = NULL;
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

template <size_t Slot>
void stream_on_packet_slot(void *stream_,
                           const zlink_routing_id_t *rid_,
                           zlink_msg_t *header_,
                           zlink_msg_t *body_,
                           void *userdata_)
{
    (void) stream_;
    (void) userdata_;

    const auto close_messages = [header_, body_]() {
        if (header_)
            (void) zlink_msg_close(header_);
        if (body_)
            (void) zlink_msg_close(body_);
    };

    if (!rid_ || !header_ || !body_) {
        close_messages();
        return;
    }

    stream_js_state_t *state = &g_stream_slots[Slot];
    napi_threadsafe_function tsfn = NULL;
    std::unique_ptr<stream_js_payload_t> payload;
    {
        std::lock_guard<std::mutex> lock(g_stream_slots_mu);
        if (!state->used || !state->tsfn) {
            close_messages();
            return;
        }
        if (state->stop_requested.load(std::memory_order_acquire) != 0) {
            close_messages();
            return;
        }
        tsfn = state->tsfn;
        remember_stream_peer_unsafe(state, rid_);
        payload.reset(new stream_js_payload_t());
        payload->routing_id.assign(rid_->data, rid_->data + rid_->size);

        const unsigned char *header_data =
          static_cast<const unsigned char *>(zlink_msg_data(header_));
        const size_t header_size = zlink_msg_size(header_);
        std::vector<unsigned char> header;
        if (header_data && header_size > 0)
            header.assign(header_data, header_data + header_size);
        payload->packets.push_back(header);

        const unsigned char *body_data =
          static_cast<const unsigned char *>(zlink_msg_data(body_));
        const size_t body_size = zlink_msg_size(body_);
        std::vector<unsigned char> body;
        if (body_data && body_size > 0)
            body.assign(body_data, body_data + body_size);
        payload->packets.push_back(body);
    }

    close_messages();

    if (napi_call_threadsafe_function(tsfn, payload.get(), napi_tsfn_blocking)
        == napi_ok) {
        payload.release();
    }
}

typedef void (*stream_slot_packet_callback_t)(void *,
                                              const zlink_routing_id_t *,
                                              zlink_msg_t *,
                                              zlink_msg_t *,
                                              void *);

#define STREAM_SLOT_PACKET_CALLBACK(N) &stream_on_packet_slot<N>
static stream_slot_packet_callback_t
  g_stream_slot_packet_callbacks[k_stream_slot_count] = {
    STREAM_SLOT_PACKET_CALLBACK(0),
    STREAM_SLOT_PACKET_CALLBACK(1),
    STREAM_SLOT_PACKET_CALLBACK(2),
    STREAM_SLOT_PACKET_CALLBACK(3),
    STREAM_SLOT_PACKET_CALLBACK(4),
    STREAM_SLOT_PACKET_CALLBACK(5),
    STREAM_SLOT_PACKET_CALLBACK(6),
    STREAM_SLOT_PACKET_CALLBACK(7),
};
#undef STREAM_SLOT_PACKET_CALLBACK

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

void request_tsfn_finalize(napi_env env, void *finalize_data, void *finalize_hint)
{
    (void) env;
    (void) finalize_hint;
    request_js_state_t *state = static_cast<request_js_state_t *>(finalize_data);
    delete state;
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

void router_handler_release_slot(void *socket)
{
    napi_threadsafe_function tsfn = NULL;
    {
        std::lock_guard<std::mutex> lock(g_router_handler_slots_mu);
        router_handler_js_state_t *state =
          find_router_handler_slot_by_socket_unsafe(socket);
        if (!state)
            return;
        tsfn = state->tsfn;
        reset_router_handler_slot_unsafe(state);
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

void recv_handler_dispatch(const zlink_routing_id_t *source_rid_,
                           zlink_msg_t *parts_,
                           size_t part_count_,
                           void *userdata_)
{
    recv_handler_js_state_t *state =
      static_cast<recv_handler_js_state_t *>(userdata_);
    std::unique_ptr<recv_handler_js_payload_t> payload(
      new recv_handler_js_payload_t());
    napi_threadsafe_function tsfn = NULL;
    {
        std::lock_guard<std::mutex> lock(g_recv_handler_slots_mu);
        if (!state || !state->used || !state->tsfn) {
            close_recv_parts(parts_, part_count_);
            return;
        }
        tsfn = state->tsfn;
        if (source_rid_ && source_rid_->size > 0) {
            payload->routing_id.assign(
              source_rid_->data, source_rid_->data + source_rid_->size);
        }
    }

    payload->parts.resize(part_count_);
    size_t moved = 0;
    for (; moved < part_count_; ++moved) {
        if (zlink_msg_init(&payload->parts[moved]) != 0) {
            close_recv_parts(parts_, part_count_);
            return;
        }
        payload->part_count = moved + 1;
        if (zlink_msg_move(&payload->parts[moved], &parts_[moved]) != 0) {
            close_recv_parts(parts_, part_count_);
            return;
        }
    }
    close_recv_parts(parts_, part_count_);
    if (napi_call_threadsafe_function(tsfn, payload.get(), napi_tsfn_nonblocking)
        == napi_ok) {
        payload.release();
    }
}

template <size_t Slot>
void router_handler_slot_callback(const zlink_routing_id_t *source_node_rid_,
                                  const zlink_routing_id_t *source_spot_rid_,
                                  uint64_t request_seq_,
                                  zlink_msg_t *parts_,
                                  size_t part_count_,
                                  void *userdata_)
{
    (void) userdata_;
    std::unique_ptr<router_handler_js_payload_t> payload(
      new router_handler_js_payload_t());
    router_handler_js_state_t *state = &g_router_handler_slots[Slot];
    napi_threadsafe_function tsfn = NULL;
    {
        std::lock_guard<std::mutex> lock(g_router_handler_slots_mu);
        if (!state->used || !state->tsfn) {
            close_recv_parts(parts_, part_count_);
            return;
        }
        tsfn = state->tsfn;
        payload->request_seq = request_seq_;
        if (source_node_rid_ && source_node_rid_->size > 0) {
            payload->routing_id.assign(
              source_node_rid_->data, source_node_rid_->data + source_node_rid_->size);
        }
        if (source_spot_rid_ && source_spot_rid_->size > 0) {
            payload->spot_routing_id.assign(
              source_spot_rid_->data,
              source_spot_rid_->data + source_spot_rid_->size);
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

typedef void (*router_handler_slot_callback_t)(const zlink_routing_id_t *,
                                               const zlink_routing_id_t *,
                                               uint64_t,
                                               zlink_msg_t *,
                                               size_t,
                                               void *);
typedef void (*subscribe_handler_slot_callback_t)(const zlink_routing_id_t *,
                                                  const char *,
                                                  size_t,
                                                  zlink_msg_t *,
                                                  size_t,
                                                  void *);

#define ROUTER_HANDLER_SLOT_CALLBACK(N) &router_handler_slot_callback<N>
static router_handler_slot_callback_t
  g_router_handler_slot_callbacks[k_router_handler_slot_count] = {
    ROUTER_HANDLER_SLOT_CALLBACK(0),
    ROUTER_HANDLER_SLOT_CALLBACK(1),
    ROUTER_HANDLER_SLOT_CALLBACK(2),
    ROUTER_HANDLER_SLOT_CALLBACK(3),
    ROUTER_HANDLER_SLOT_CALLBACK(4),
    ROUTER_HANDLER_SLOT_CALLBACK(5),
    ROUTER_HANDLER_SLOT_CALLBACK(6),
    ROUTER_HANDLER_SLOT_CALLBACK(7),
};
#undef ROUTER_HANDLER_SLOT_CALLBACK

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

#define SEND_READY_HANDLER_SLOT_CALLBACK(N) &send_ready_handler_slot_callback<N>
typedef void (*send_ready_handler_slot_callback_t)(void *, void *);
template <size_t Slot>
void send_ready_handler_slot_callback(void *subject_, void *userdata_)
{
    (void) userdata_;
    send_ready_handler_js_state_t *state = &g_send_ready_handler_slots[Slot];
    napi_threadsafe_function tsfn = NULL;
    {
        std::lock_guard<std::mutex> lock(g_send_ready_handler_slots_mu);
        if (!state->used || !state->tsfn)
            return;
        tsfn = state->tsfn;
    }

    std::unique_ptr<int> payload(new int(1));
    if (napi_call_threadsafe_function(tsfn, payload.get(), napi_tsfn_nonblocking)
        == napi_ok) {
        payload.release();
    }
}

#define SOCKET_MONITOR_HANDLER_SLOT_CALLBACK(N) \
  &socket_monitor_handler_slot_callback<N>
typedef void (*socket_monitor_handler_slot_callback_t)(const zlink_monitor_event_t *,
                                                       void *);
template <size_t Slot>
void socket_monitor_handler_slot_callback(const zlink_monitor_event_t *event_,
                                          void *userdata_)
{
    (void) userdata_;
    std::unique_ptr<socket_monitor_handler_js_payload_t> payload(
      new socket_monitor_handler_js_payload_t());
    socket_monitor_handler_js_state_t *state = &g_socket_monitor_handler_slots[Slot];
    napi_threadsafe_function tsfn = NULL;
    {
        std::lock_guard<std::mutex> lock(g_socket_monitor_handler_slots_mu);
        if (!state->used || !state->tsfn)
            return;
        tsfn = state->tsfn;
        if (event_)
            payload->event = *event_;
        else
            memset(&payload->event, 0, sizeof(payload->event));
    }

    if (napi_call_threadsafe_function(tsfn, payload.get(), napi_tsfn_nonblocking)
        == napi_ok) {
        payload.release();
    }
}
static socket_monitor_handler_slot_callback_t
  g_socket_monitor_handler_slot_callbacks[k_socket_monitor_handler_slot_count] = {
    SOCKET_MONITOR_HANDLER_SLOT_CALLBACK(0),
    SOCKET_MONITOR_HANDLER_SLOT_CALLBACK(1),
    SOCKET_MONITOR_HANDLER_SLOT_CALLBACK(2),
    SOCKET_MONITOR_HANDLER_SLOT_CALLBACK(3),
    SOCKET_MONITOR_HANDLER_SLOT_CALLBACK(4),
    SOCKET_MONITOR_HANDLER_SLOT_CALLBACK(5),
    SOCKET_MONITOR_HANDLER_SLOT_CALLBACK(6),
    SOCKET_MONITOR_HANDLER_SLOT_CALLBACK(7),
};
#undef SOCKET_MONITOR_HANDLER_SLOT_CALLBACK

static send_ready_handler_slot_callback_t
  g_send_ready_handler_slot_callbacks[k_send_ready_handler_slot_count] = {
    SEND_READY_HANDLER_SLOT_CALLBACK(0),
    SEND_READY_HANDLER_SLOT_CALLBACK(1),
    SEND_READY_HANDLER_SLOT_CALLBACK(2),
    SEND_READY_HANDLER_SLOT_CALLBACK(3),
    SEND_READY_HANDLER_SLOT_CALLBACK(4),
    SEND_READY_HANDLER_SLOT_CALLBACK(5),
    SEND_READY_HANDLER_SLOT_CALLBACK(6),
    SEND_READY_HANDLER_SLOT_CALLBACK(7),
};
#undef SEND_READY_HANDLER_SLOT_CALLBACK

bool attach_recv_handler(napi_env env, void *socket, napi_value handler)
{
    recv_handler_js_state_t *slot = NULL;
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

    int rc = zlink_recv_handler(socket, recv_handler_dispatch, slot);
    if (rc != 0) {
        recv_handler_release_slot(socket);
        throw_last_error(env, "recvHandler failed");
        return false;
    }
    return true;
}

bool attach_router_handler(napi_env env, void *socket, napi_value handler)
{
    (void) socket;
    (void) handler;
    napi_throw_error(
      env, NULL, "routerHandler is not available on the aligned public API");
    return false;
}

bool attach_subscribe_handler(napi_env env, void *socket, napi_value handler)
{
    (void) socket;
    (void) handler;
    napi_throw_error(
      env, NULL, "subscribeHandler is not available on the aligned public API");
    return false;
}

bool attach_send_ready_handler(napi_env env, void *socket, napi_value handler)
{
    send_ready_handler_js_state_t *slot = NULL;
    size_t slot_index = 0;
    {
        std::lock_guard<std::mutex> lock(g_send_ready_handler_slots_mu);
        if (find_send_ready_handler_slot_by_socket_unsafe(socket)) {
            napi_throw_error(env, NULL, "sendReadyHandler already attached");
            return false;
        }
        slot = find_free_send_ready_handler_slot_unsafe();
        if (!slot) {
            napi_throw_error(env, NULL, "no free sendReadyHandler slot");
            return false;
        }
        slot_index = static_cast<size_t>(slot - g_send_ready_handler_slots);
    }

    napi_value resource_name;
    napi_create_string_utf8(
      env, "zlink-send-ready-handler", NAPI_AUTO_LENGTH, &resource_name);
    napi_threadsafe_function tsfn = NULL;
    napi_status tsfn_status = napi_create_threadsafe_function(
      env, handler, NULL, resource_name, 0, 1, slot,
      send_ready_handler_tsfn_finalize, slot, send_ready_handler_tsfn_call_js,
      &tsfn);
    if (tsfn_status != napi_ok) {
        napi_throw_error(
          env, NULL, "sendReadyHandler failed to create callback queue");
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(g_send_ready_handler_slots_mu);
        slot->used = true;
        slot->socket = socket;
        slot->env = env;
        slot->tsfn = tsfn;
    }

    int rc = zlink_send_ready_handler(
      socket, g_send_ready_handler_slot_callbacks[slot_index], slot);
    if (rc != 0) {
        release_socket_send_ready_handler_slot(socket);
        throw_last_error(env, "sendReadyHandler failed");
        return false;
    }
    return true;
}

bool attach_socket_monitor_handler(napi_env env,
                                   void *monitor,
                                   napi_value handler)
{
    socket_monitor_handler_js_state_t *slot = NULL;
    size_t slot_index = 0;
    {
        std::lock_guard<std::mutex> lock(g_socket_monitor_handler_slots_mu);
        if (find_socket_monitor_handler_slot_by_monitor_unsafe(monitor)) {
            napi_throw_error(env, NULL, "monitorHandler already attached");
            return false;
        }
        slot = find_free_socket_monitor_handler_slot_unsafe();
        if (!slot) {
            napi_throw_error(env, NULL, "no free monitorHandler slot");
            return false;
        }
        slot_index = static_cast<size_t>(slot - g_socket_monitor_handler_slots);
    }

    napi_value resource_name;
    napi_create_string_utf8(
      env, "zlink-monitor-handler", NAPI_AUTO_LENGTH, &resource_name);
    napi_threadsafe_function tsfn = NULL;
    napi_status tsfn_status = napi_create_threadsafe_function(
      env, handler, NULL, resource_name, 0, 1, slot,
      socket_monitor_handler_tsfn_finalize, slot,
      socket_monitor_handler_tsfn_call_js, &tsfn);
    if (tsfn_status != napi_ok) {
        napi_throw_error(
          env, NULL, "monitorHandler failed to create callback queue");
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(g_socket_monitor_handler_slots_mu);
        slot->used = true;
        slot->monitor = monitor;
        slot->env = env;
        slot->tsfn = tsfn;
    }

    int rc = zlink_socket_monitor_handler(
      monitor, g_socket_monitor_handler_slot_callbacks[slot_index], slot);
    if (rc != 0) {
        release_socket_monitor_handler_slot(monitor);
        throw_last_error(env, "monitorHandler failed");
        return false;
    }
    return true;
}

} // namespace

void release_socket_recv_handler_slot(void *socket)
{
    recv_handler_release_slot(socket);
}

void release_router_handler_slot(void *socket)
{
    router_handler_release_slot(socket);
}

void release_socket_subscribe_handler_slot(void *socket)
{
    subscribe_handler_release_slot(socket);
}

void release_socket_send_ready_handler_slot(void *socket)
{
    napi_threadsafe_function tsfn = NULL;
    {
        std::lock_guard<std::mutex> lock(g_send_ready_handler_slots_mu);
        send_ready_handler_js_state_t *state =
          find_send_ready_handler_slot_by_socket_unsafe(socket);
        if (!state)
            return;
        tsfn = state->tsfn;
        reset_send_ready_handler_slot_unsafe(state);
    }
    if (tsfn)
        (void) napi_release_threadsafe_function(tsfn, napi_tsfn_abort);
}

void release_socket_monitor_handler_slot(void *monitor)
{
    napi_threadsafe_function tsfn = NULL;
    {
        std::lock_guard<std::mutex> lock(g_socket_monitor_handler_slots_mu);
        socket_monitor_handler_js_state_t *state =
          find_socket_monitor_handler_slot_by_monitor_unsafe(monitor);
        if (!state)
            return;
        tsfn = state->tsfn;
        reset_socket_monitor_handler_slot_unsafe(state);
    }
    if (tsfn)
        (void) napi_release_threadsafe_function(tsfn, napi_tsfn_abort);
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

bool get_uint64_like(napi_env env, napi_value value, uint64_t *out);

request_js_state_t *create_request_js_state(napi_env env, napi_value handler)
{
    request_js_state_t *state = new request_js_state_t();
    state->env = env;

    napi_value resource_name;
    napi_create_string_utf8(
      env, "zlink-request-reply-callback", NAPI_AUTO_LENGTH, &resource_name);
    napi_threadsafe_function tsfn = NULL;
    napi_status status = napi_create_threadsafe_function(
      env, handler, NULL, resource_name, 0, 1, state, request_tsfn_finalize,
      state, request_tsfn_call_js, &tsfn);
    if (status != napi_ok) {
        delete state;
        napi_throw_error(env, NULL, "request callback setup failed");
        return NULL;
    }
    (void) napi_unref_threadsafe_function(env, tsfn);
    state->tsfn = tsfn;
    return state;
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
        zlink_msg_t *msg = &(*out)[i];
        bool is_buf = false;
        if (napi_is_buffer(env, val, &is_buf) == napi_ok && is_buf) {
            void *data = NULL;
            size_t sz = 0;
            if (napi_get_buffer_info(env, val, &data, &sz) != napi_ok) {
                for (size_t j = 0; j < built; j++)
                    zlink_msg_close(&(*out)[j]);
                napi_throw_type_error(env, NULL, "buffer info failed");
                return false;
            }
            if (zlink_msg_init_size(msg, sz) != 0) {
                for (size_t j = 0; j < built; j++)
                    zlink_msg_close(&(*out)[j]);
                throw_last_error(env, "msg_init_size failed");
                return false;
            }
            if (sz > 0 && data)
                memcpy(zlink_msg_data(msg), data, sz);
            built++;
            continue;
        }

        napi_value data_value;
        if (napi_get_named_property(env, val, "data", &data_value) != napi_ok) {
            for (size_t j = 0; j < built; j++)
                zlink_msg_close(&(*out)[j]);
            napi_throw_type_error(env, NULL, "parts must be Buffers or message snapshots");
            return false;
        }
        void *data = NULL;
        size_t sz = 0;
        if (napi_get_buffer_info(env, data_value, &data, &sz) != napi_ok) {
            for (size_t j = 0; j < built; j++)
                zlink_msg_close(&(*out)[j]);
            napi_throw_type_error(env, NULL, "message snapshot data must be a Buffer");
            return false;
        }
        if (zlink_msg_init_size(msg, sz) != 0) {
            for (size_t j = 0; j < built; j++)
                zlink_msg_close(&(*out)[j]);
            throw_last_error(env, "msg_init_size failed");
            return false;
        }
        if (sz > 0 && data)
            memcpy(zlink_msg_data(msg), data, sz);

        built++;
    }
    return true;
}

void close_msg_vector(std::vector<zlink_msg_t> &parts)
{
    for (size_t i = 0; i < parts.size(); i++)
        zlink_msg_close(&parts[i]);
}

bool get_uint64_like(napi_env env, napi_value value, uint64_t *out)
{
    bool lossless = false;
    if (napi_get_value_bigint_uint64(env, value, out, &lossless) == napi_ok)
        return true;

    napi_valuetype type = napi_undefined;
    if (napi_typeof(env, value, &type) != napi_ok)
        return false;
    if (type == napi_number) {
        double number = 0;
        if (napi_get_value_double(env, value, &number) != napi_ok || number < 0)
            return false;
        *out = static_cast<uint64_t>(number);
        return true;
    }

    napi_value coerced;
    if (napi_coerce_to_string(env, value, &coerced) != napi_ok)
        return false;
    std::string text = get_string(env, coerced);
    char *end = NULL;
    errno = 0;
    unsigned long long parsed = strtoull(text.c_str(), &end, 10);
    if (errno != 0 || end == text.c_str() || (end && *end != '\0'))
        return false;
    *out = static_cast<uint64_t>(parsed);
    return true;
}

bool init_msg_from_value(napi_env env, napi_value value, zlink_msg_t *msg)
{
    bool is_buf = false;
    if (napi_is_buffer(env, value, &is_buf) == napi_ok && is_buf) {
        void *data = NULL;
        size_t len = 0;
        if (napi_get_buffer_info(env, value, &data, &len) != napi_ok) {
            napi_throw_type_error(env, NULL, "send buffer invalid");
            return false;
        }
        if (!init_msg_from_bytes(msg, data, len))
            return false;
        return true;
    }

    napi_value data_value;
    if (napi_get_named_property(env, value, "data", &data_value) != napi_ok) {
        napi_throw_type_error(env, NULL, "message must be a Buffer or message snapshot");
        return false;
    }
    void *data = NULL;
    size_t len = 0;
    if (napi_get_buffer_info(env, data_value, &data, &len) != napi_ok) {
        napi_throw_type_error(env, NULL, "message snapshot data must be a Buffer");
        return false;
    }
    if (!init_msg_from_bytes(msg, data, len))
        return false;

    return true;
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

napi_value ctx_shutdown(napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *ctx = NULL;
    napi_get_value_external(env, argv[0], &ctx);
    int rc = zlink_ctx_shutdown(ctx);
    if (rc != 0)
        return throw_last_error(env, "ctx_shutdown failed");
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

napi_value ctx_term(napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *ctx = NULL;
    napi_get_value_external(env, argv[0], &ctx);
    int rc;
    do {
        rc = zlink_ctx_term(ctx);
    } while (rc != 0 && zlink_errno() == EINTR);
    if (rc != 0)
        return throw_last_error(env, "ctx_term failed");
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

napi_value ctx_setopt(napi_env env, napi_callback_info info)
{
    napi_value argv[3];
    size_t argc = 3;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *ctx = NULL;
    napi_get_value_external(env, argv[0], &ctx);
    int32_t opt = 0;
    int32_t value = 0;
    napi_get_value_int32(env, argv[1], &opt);
    bool is_buffer = false;
    napi_is_buffer(env, argv[2], &is_buffer);
    if (is_buffer) {
        void *data = NULL;
        size_t length = 0;
        napi_get_buffer_info(env, argv[2], &data, &length);
        zlink_config_result_t rc =
          zlink_ctx_set_data(ctx, static_cast<zlink_ctx_option_t>(opt), data, length);
        if (rc != ZLINK_CONFIG_OK)
            return throw_last_error(env, "ctx_setopt failed");
        napi_value ok;
        napi_get_undefined(env, &ok);
        return ok;
    }
    napi_get_value_int32(env, argv[2], &value);
    int rc = zlink_ctx_set(ctx, static_cast<zlink_ctx_option_t>(opt), value);
    if (rc != 0)
        return throw_last_error(env, "ctx_setopt failed");
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

napi_value ctx_getopt(napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *ctx = NULL;
    napi_get_value_external(env, argv[0], &ctx);
    int32_t opt = 0;
    napi_get_value_int32(env, argv[1], &opt);
    zlink_config_result_t err = ZLINK_CONFIG_OK;
    const int rc = zlink_ctx_get(ctx, static_cast<zlink_ctx_option_t>(opt), &err);
    if (err != ZLINK_CONFIG_OK)
        return throw_last_error(env, "ctx_getopt failed");
    napi_value out;
    napi_create_int32(env, rc, &out);
    return out;
}

napi_value ctx_recalculate_auto_hwm(napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *ctx = NULL;
    napi_get_value_external(env, argv[0], &ctx);
    zlink_config_result_t rc = zlink_ctx_auto_hwm_recalculate(ctx);
    if (rc != ZLINK_CONFIG_OK)
        return throw_last_error(env, "ctx_auto_hwm_recalculate failed");
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
    router_handler_release_slot(sock);
    subscribe_handler_release_slot(sock);
    release_socket_send_ready_handler_slot(sock);
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
    if (rc != 0) {
        char buf[128];
        snprintf(buf, sizeof(buf), "bind failed (result=%d)", rc);
        napi_throw_error(env, NULL, buf);
        return NULL;
    }
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

napi_value socket_disconnect_rid(napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *sock = NULL;
    napi_get_value_external(env, argv[0], &sock);
    zlink_routing_id_t peer_rid;
    if (!parse_routing_id(env, argv[1], &peer_rid))
        return NULL;
    int rc = zlink_disconnect_rid(sock, &peer_rid);
    if (rc != 0)
        return throw_last_error(env, "disconnect_rid failed");
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

napi_value socket_set_tls_server(napi_env env, napi_callback_info info)
{
    napi_value argv[4];
    size_t argc = 4;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *sock = NULL;
    napi_get_value_external(env, argv[0], &sock);
    std::string cert = get_string(env, argv[1]);
    std::string key = get_string(env, argv[2]);
    int32_t require_client = 0;
    if (argc >= 4)
        napi_get_value_int32(env, argv[3], &require_client);
    int rc = zlink_set_tls_server(
      sock, cert.c_str(), key.c_str(), require_client);
    if (rc != 0)
        return throw_last_error(env, "socket_set_tls_server failed");
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

napi_value socket_set_tls_client(napi_env env, napi_callback_info info)
{
    napi_value argv[4];
    size_t argc = 4;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *sock = NULL;
    napi_get_value_external(env, argv[0], &sock);
    std::string ca = get_string(env, argv[1]);
    std::string host = get_string(env, argv[2]);
    int32_t trust = 0;
    if (argc >= 4)
        napi_get_value_int32(env, argv[3], &trust);
    int rc = zlink_set_tls_client(sock, ca.c_str(), host.c_str(), trust);
    if (rc != 0)
        return throw_last_error(env, "socket_set_tls_client failed");
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
    int32_t flags = 0;
    napi_get_value_int32(env, argv[2], &flags);
    zlink_msg_t msg;
    if (!init_msg_from_value(env, argv[1], &msg))
        return throw_last_error(env, "send failed");
    const size_t len = zlink_msg_size(&msg);
    int rc = send_parts(sock, &msg, 1, static_cast<zlink_send_flags_t>(flags));
    if (rc != ZLINK_SUBMIT_OK)
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
    size_t total = 0;
    for (size_t i = 0; i < parts.size(); ++i)
        total += zlink_msg_size(&parts[i]);
    int rc = send_parts(
      sock, parts.data(), parts.size(), static_cast<zlink_send_flags_t>(flags));
    if (rc != ZLINK_SUBMIT_OK) {
        return throw_last_error(env, "sendParts failed");
    }

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
    bool is_array = false;
    napi_is_array(env, argv[2], &is_array);
    bool is_buf = false;
    napi_valuetype payload_type = napi_undefined;
    napi_typeof(env, argv[2], &payload_type);
    if (is_array) {
        if (!build_msg_vector(env, argv[2], &parts))
            return NULL;
    } else if ((napi_is_buffer(env, argv[2], &is_buf) == napi_ok && is_buf)
               || payload_type == napi_object) {
        parts.resize(1);
        if (!init_msg_from_value(env, argv[2], &parts[0]))
            return throw_last_error(env, "publish failed");
    } else if (!build_msg_vector(env, argv[2], &parts)) {
        return NULL;
    }

    int32_t flags = 0;
    napi_get_value_int32(env, argv[3], &flags);
    size_t total = 0;
    for (size_t i = 0; i < parts.size(); ++i)
        total += zlink_msg_size(&parts[i]);
    int rc = publish_parts(sock,
                           topic.c_str(),
                           parts.data(),
                           parts.size(),
                           static_cast<zlink_send_flags_t>(flags));
    if (rc != ZLINK_SUBMIT_OK) {
        return throw_last_error(env, "publish failed");
    }

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
    bool is_array = false;
    napi_is_array(env, argv[2], &is_array);
    bool is_buf = false;
    napi_valuetype payload_type = napi_undefined;
    napi_typeof(env, argv[2], &payload_type);
    if (is_array) {
        if (!build_msg_vector(env, argv[2], &parts))
            return NULL;
    } else if ((napi_is_buffer(env, argv[2], &is_buf) == napi_ok && is_buf)
               || payload_type == napi_object) {
        parts.resize(1);
        if (!init_msg_from_value(env, argv[2], &parts[0]))
            return throw_last_error(env, "publishNoWaitResult failed");
    } else if (!build_msg_vector(env, argv[2], &parts)) {
        return NULL;
    }

    int rc =
      publish_parts(sock, topic.c_str(), parts.data(), parts.size(),
                    ZLINK_SEND_FLAGS_DONTWAIT);
    if (rc == ZLINK_SUBMIT_OK) {
        rc = ZLINK_SUBMIT_OK;
    } else {
        rc = classify_try_send_errno();
    }
    if (rc < 0) {
        return throw_last_error(env, "publishNoWaitResult failed");
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
    zlink_msg_t msg;
    if (!init_msg_from_value(env, argv[1], &msg))
        return throw_last_error(env, "sendNoWaitResult failed");
    int rc = send_parts(sock, &msg, 1, ZLINK_SEND_FLAGS_DONTWAIT);
    if (rc == ZLINK_SUBMIT_OK)
        rc = ZLINK_SUBMIT_OK;
    else
        rc = classify_try_send_errno();
    if (rc < 0)
        return throw_last_error(env, "sendNoWaitResult failed");
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

    int rc = send_parts(sock, parts.data(), parts.size(), ZLINK_SEND_FLAGS_DONTWAIT);
    if (rc == ZLINK_SUBMIT_OK)
        rc = ZLINK_SUBMIT_OK;
    else
        rc = classify_try_send_errno();
    if (rc < 0) {
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

    zlink_msg_t msg;
    if (!init_msg_from_value(env, argv[2], &msg))
        return throw_last_error(env, "trySendTo failed");
    int rc = send_parts_rid(sock, &routing_id, &msg, 1, ZLINK_SEND_FLAGS_DONTWAIT);
    if (rc == ZLINK_SUBMIT_OK)
        rc = ZLINK_SUBMIT_OK;
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

    int rc = send_parts_rid(
      sock, &routing_id, parts.data(), parts.size(), ZLINK_SEND_FLAGS_DONTWAIT);
    if (rc == ZLINK_SUBMIT_OK)
        rc = ZLINK_SUBMIT_OK;
    else
        rc = classify_try_send_errno();
    if (rc < 0) {
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
    int rc = send_parts(sock, &msg, 1, static_cast<zlink_send_flags_t>(flags));
    if (rc != ZLINK_SUBMIT_OK)
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
    std::vector<zlink_msg_t> parts;
    int rc = recv_parts(sock, &routing_id, &parts, flags);
    if (rc != ZLINK_RECV_OK)
        return throw_last_error(env, "recv failed");
    size_t total = recv_parts_size(parts.data(), parts.size());
    std::vector<unsigned char> bytes(total);
    if (total > 0)
        copy_recv_parts(parts.data(), parts.size(), bytes.data(), total);
    close_msg_vector(parts);
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
    std::vector<zlink_msg_t> parts;
    int rc = recv_parts(sock, &routing_id, &parts, flags);
    if (rc != ZLINK_RECV_OK)
        return throw_last_error(env, "recv failed");

    napi_value out =
      create_recv_message_value(env, routing_id, parts.data(), parts.size());
    close_msg_vector(parts);
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
    std::vector<zlink_msg_t> parts;
    int rc = recv_parts(
      sock,
      &routing_id,
      &parts,
      static_cast<int32_t>(ZLINK_RECV_FLAGS_DONTWAIT));
    if (rc != ZLINK_RECV_OK) {
        if (zlink_errno() == EAGAIN) {
            napi_value none;
            napi_get_null(env, &none);
            return none;
        }
        return throw_last_error(env, "tryReceive failed");
    }
    napi_value out =
      create_recv_message_value(env, routing_id, parts.data(), parts.size());
    close_msg_vector(parts);
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
    std::vector<zlink_msg_t> parts;
    size_t topic_len = topic.size();

    for (;;) {
        memset(&routing_id, 0, sizeof(routing_id));
        int rc = subscribe_parts(sock,
                                 &routing_id,
                                 topic.data(),
                                 topic.size(),
                                 &topic_len,
                                 &parts,
                                 ZLINK_RECV_FLAGS_NONE);
        if (rc == ZLINK_RECV_OK) {
          napi_value out = create_subscribed_value(
            env, routing_id, topic.data(), topic_len, parts.data(), parts.size());
          close_msg_vector(parts);
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
    std::vector<zlink_msg_t> parts;
    size_t topic_len = topic.size();

    for (;;) {
        memset(&routing_id, 0, sizeof(routing_id));
        int rc = subscribe_parts(sock,
                                 &routing_id,
                                 topic.data(),
                                 topic.size(),
                                 &topic_len,
                                 &parts,
                                 ZLINK_RECV_FLAGS_DONTWAIT);
        if (rc == ZLINK_RECV_OK) {
          napi_value out = create_subscribed_value(
            env, routing_id, topic.data(), topic_len, parts.data(), parts.size());
          close_msg_vector(parts);
          return out;
        }
        const int err = zlink_errno();
        if (err == EAGAIN) {
            napi_value none;
            napi_get_null(env, &none);
            return none;
        }
        if (err != EMSGSIZE)
            return throw_last_error(env, "subscribeNoWait failed");
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

napi_value socket_send_ready_handler(napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    if (argc < 2) {
        napi_throw_type_error(
          env, NULL, "sendReadyHandler requires (socket, handler)");
        return NULL;
    }
    void *sock = NULL;
    napi_get_value_external(env, argv[0], &sock);
    napi_valuetype handler_type = napi_undefined;
    napi_typeof(env, argv[1], &handler_type);
    if (handler_type != napi_function) {
        napi_throw_type_error(
          env, NULL, "sendReadyHandler handler must be a function");
        return NULL;
    }
    if (!attach_send_ready_handler(env, sock, argv[1]))
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
        const zlink_routing_id_t *source_rid = NULL;
        memset(&routing_id, 0, sizeof(routing_id));
        int rc = zlink_xpub_recv_part(
          sock,
          &source_rid,
          &subscribed,
          topic.data(),
          topic.size(),
          &topic_len,
          ZLINK_RECV_FLAGS_NONE);
        if (rc == ZLINK_RECV_OK) {
            copy_routing_id(&routing_id, source_rid);
            return create_subscription_event_value(
              env, routing_id, subscribed, topic.data(), topic_len);
        }
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
        const zlink_routing_id_t *source_rid = NULL;
        memset(&routing_id, 0, sizeof(routing_id));
        int rc = zlink_xpub_recv_part(
          sock,
          &source_rid,
          &subscribed,
          topic.data(),
          topic.size(),
          &topic_len,
          ZLINK_RECV_FLAGS_DONTWAIT);
        if (rc == ZLINK_RECV_OK) {
            copy_routing_id(&routing_id, source_rid);
            return create_subscription_event_value(
              env, routing_id, subscribed, topic.data(), topic_len);
        }
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

napi_value subscription_at(napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    if (argc < 2) {
        napi_throw_type_error(env, NULL, "subscriptionAt expects handle, index");
        return NULL;
    }

    void *handle = NULL;
    napi_get_value_external(env, argv[0], &handle);
    uint32_t index = 0;
    napi_get_value_uint32(env, argv[1], &index);

    std::vector<char> filter(256, '\0');
    size_t filter_len = filter.size();
    int is_pattern = 0;
    for (;;) {
        zlink_config_result_t rc = zlink_subscription_at(
          handle, static_cast<size_t>(index), filter.data(), &filter_len,
          &is_pattern);
        if (rc == ZLINK_CONFIG_OK) {
            napi_value obj;
            napi_create_object(env, &obj);
            napi_value filter_value;
            napi_create_string_utf8(env, filter.data(), filter_len, &filter_value);
            napi_set_named_property(env, obj, "filter", filter_value);
            napi_value pattern_value;
            napi_get_boolean(env, is_pattern != 0, &pattern_value);
            napi_set_named_property(env, obj, "isPattern", pattern_value);
            return obj;
        }
        if (rc == ZLINK_CONFIG_NOT_FOUND) {
            napi_value none;
            napi_get_null(env, &none);
            return none;
        }
        if (zlink_errno() != EMSGSIZE)
            return throw_last_error(env, "subscription_at failed");
        filter.assign(filter_len > 0 ? filter_len : 1, '\0');
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
    std::vector<zlink_msg_t> parts;
    int rc = recv_parts(sock, &routing_id, &parts, flags);
    if (rc != ZLINK_RECV_OK)
        return throw_last_error(env, "recvInto failed");
    size_t total = recv_parts_size(parts.data(), parts.size());
    if (total > 0)
        copy_recv_parts(parts.data(), parts.size(),
                        static_cast<unsigned char *>(data), len);
    close_msg_vector(parts);
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
    std::vector<zlink_msg_t> parts;
    int rc = recv_parts(sock, &routing_id, &parts, flags);
    if (rc != ZLINK_RECV_OK)
        return throw_last_error(env, "recvMsgInto failed");
    size_t total = recv_parts_size(parts.data(), parts.size());
    if (total > 0)
        copy_recv_parts(parts.data(), parts.size(),
                        static_cast<unsigned char *>(data), len);
    close_msg_vector(parts);

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
                               "streamAttach mode must be NONE(0) or PACKET(1)");
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

    if (mode == k_stream_dispatch_len32be) {
        zlink_handler_result_t rc = zlink_stream_packet_handler(
          sock, g_stream_slot_packet_callbacks[slot_index], NULL);
        if (rc != ZLINK_HANDLER_OK) {
            stream_release_slot(sock);
            return throw_last_error(env, "streamAttach failed");
        }
    } else {
        int rc = zlink_stream_attach_raw(
          sock, g_stream_slot_raw_legacy_callbacks[slot_index]);
        if (rc != 0) {
            stream_release_slot(sock);
            return throw_last_error(env, "streamAttach failed");
        }
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

    int rc = send_parts_rid(
      sock, &rid, &msg, 1, static_cast<zlink_send_flags_t>(flags));
    if (rc != ZLINK_SUBMIT_OK)
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

napi_value socket_set_channel_name(napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *sock = NULL;
    napi_get_value_external(env, argv[0], &sock);
    std::string channel_name = get_string(env, argv[1]);
    int rc = zlink_socket_set_channel_name(sock, channel_name.c_str());
    if (rc != 0)
        return throw_last_error(env, "socketSetChannelName failed");
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

napi_value socket_get_channel_name(napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *sock = NULL;
    napi_get_value_external(env, argv[0], &sock);
    char channel_name[256];
    size_t channel_name_len = 0;
    memset(channel_name, 0, sizeof(channel_name));
    int rc = zlink_socket_get_channel_name(sock, channel_name,
                                           sizeof(channel_name),
                                           &channel_name_len);
    if (rc != 0)
        return throw_last_error(env, "socketGetChannelName failed");
    napi_value out;
    napi_create_string_utf8(env, channel_name,
                            static_cast<size_t>(channel_name_len), &out);
    return out;
}

napi_value handle_set_routing_id(napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *handle = NULL;
    napi_get_value_external(env, argv[0], &handle);
    zlink_routing_id_t routing_id;
    if (!parse_routing_id(env, argv[1], &routing_id))
        return NULL;
    int rc = zlink_set_routing_id(handle, routing_id.data, routing_id.size);
    if (rc != 0)
        return throw_last_error(env, "set_routing_id failed");
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

napi_value handle_get_routing_id(napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *handle = NULL;
    napi_get_value_external(env, argv[0], &handle);
    zlink_routing_id_t routing_id;
    memset(&routing_id, 0, sizeof(routing_id));
    int rc = zlink_get_routing_id(handle, &routing_id);
    if (rc != 0)
        return throw_last_error(env, "get_routing_id failed");
    return create_routing_id_value(env, routing_id);
}

napi_value dealer_request(napi_env env, napi_callback_info info)
{
    napi_value argv[5];
    size_t argc = 5;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    if (argc < 5) {
        napi_throw_type_error(
          env, NULL, "dealerRequest requires (socket, parts, handler, flags, timeoutMs)");
        return NULL;
    }
    void *dealer = NULL;
    napi_get_value_external(env, argv[0], &dealer);
    std::vector<zlink_msg_t> parts;
    if (!build_msg_vector(env, argv[1], &parts))
        return NULL;
    napi_valuetype handler_type = napi_undefined;
    napi_typeof(env, argv[2], &handler_type);
    if (handler_type != napi_function) {
        close_msg_vector(parts);
        napi_throw_type_error(env, NULL, "dealerRequest handler must be a function");
        return NULL;
    }
    int32_t flags = 0;
    napi_get_value_int32(env, argv[3], &flags);
    int32_t timeout_ms = 0;
    napi_get_value_int32(env, argv[4], &timeout_ms);
    request_js_state_t *state = create_request_js_state(env, argv[2]);
    if (!state) {
        close_msg_vector(parts);
        return NULL;
    }
    int rc = dealer_request_parts(
      dealer,
      parts.data(),
      parts.size(),
      request_reply_callback_trampoline,
      state,
      static_cast<zlink_send_flags_t>(flags),
      static_cast<uint32_t>(timeout_ms));
    if (rc != ZLINK_SUBMIT_OK) {
        if (state->tsfn) {
            (void) napi_release_threadsafe_function(state->tsfn, napi_tsfn_abort);
            state->tsfn = NULL;
        }
        return throw_last_error(env, "dealerRequest failed");
    }
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

napi_value router_request(napi_env env, napi_callback_info info)
{
    napi_value argv[6];
    size_t argc = 6;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    if (argc < 6) {
        napi_throw_type_error(
          env, NULL,
          "routerRequest requires (socket, routingId, parts, handler, flags, timeoutMs)");
        return NULL;
    }
    void *router = NULL;
    napi_get_value_external(env, argv[0], &router);
    zlink_routing_id_t peer_rid;
    if (!parse_routing_id(env, argv[1], &peer_rid))
        return NULL;
    std::vector<zlink_msg_t> parts;
    if (!build_msg_vector(env, argv[2], &parts))
        return NULL;
    napi_valuetype handler_type = napi_undefined;
    napi_typeof(env, argv[3], &handler_type);
    if (handler_type != napi_function) {
        close_msg_vector(parts);
        napi_throw_type_error(env, NULL, "routerRequest handler must be a function");
        return NULL;
    }
    int32_t flags = 0;
    napi_get_value_int32(env, argv[4], &flags);
    int32_t timeout_ms = 0;
    napi_get_value_int32(env, argv[5], &timeout_ms);
    request_js_state_t *state = create_request_js_state(env, argv[3]);
    if (!state) {
        close_msg_vector(parts);
        return NULL;
    }
    int rc = router_request_parts(
      router,
      &peer_rid,
      parts.data(),
      parts.size(),
      request_reply_callback_trampoline,
      state,
      static_cast<zlink_send_flags_t>(flags),
      static_cast<uint32_t>(timeout_ms));
    if (rc != ZLINK_SUBMIT_OK) {
        if (state->tsfn) {
            (void) napi_release_threadsafe_function(state->tsfn, napi_tsfn_abort);
            state->tsfn = NULL;
        }
        return throw_last_error(env, "routerRequest failed");
    }
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

napi_value router_reply(napi_env env, napi_callback_info info)
{
    napi_value argv[4];
    size_t argc = 4;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    if (argc < 4) {
        napi_throw_type_error(
          env, NULL, "routerReply requires (socket, routingId, requestSeq, parts)");
        return NULL;
    }
    void *router = NULL;
    napi_get_value_external(env, argv[0], &router);
    zlink_routing_id_t peer_rid;
    if (!parse_routing_id(env, argv[1], &peer_rid))
        return NULL;
    uint64_t request_seq = 0;
    if (!get_uint64_like(env, argv[2], &request_seq)) {
        napi_throw_type_error(env, NULL, "requestSeq must be uint64");
        return NULL;
    }
    std::vector<zlink_msg_t> parts;
    if (!build_msg_vector(env, argv[3], &parts))
        return NULL;
    int rc =
      router_reply_parts(router, &peer_rid, request_seq, parts.data(), parts.size());
    if (rc != ZLINK_SUBMIT_OK) {
        return throw_last_error(env, "routerReply failed");
    }
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

napi_value router_handler_message(napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    if (argc < 2) {
        napi_throw_type_error(
          env, NULL, "routerHandlerMessage requires (socket, handler)");
        return NULL;
    }

    void *router = NULL;
    napi_get_value_external(env, argv[0], &router);

    napi_valuetype handler_type = napi_undefined;
    napi_typeof(env, argv[1], &handler_type);
    if (handler_type != napi_function) {
        napi_throw_type_error(
          env, NULL, "routerHandlerMessage handler must be a function");
        return NULL;
    }

    if (!attach_router_handler(env, router, argv[1]))
        return NULL;

    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

napi_value router_recv_message(napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *router = NULL;
    napi_get_value_external(env, argv[0], &router);
    int32_t flags = 0;
    if (argc >= 2)
        napi_get_value_int32(env, argv[1], &flags);

    zlink_routing_id_t peer_rid;
    zlink_routing_id_t spot_rid;
    uint64_t request_seq = 0;
    std::vector<zlink_msg_t> parts;
    int rc = router_recv_parts(
      router, &peer_rid, &spot_rid, &request_seq, &parts, flags);
    if (rc != ZLINK_RECV_OK)
        return throw_last_error(env, "routerRecvMessage failed");

    napi_value out = create_router_recv_message_value(
      env,
      peer_rid,
      spot_rid.size > 0 ? &spot_rid : NULL,
      request_seq,
      parts.data(),
      parts.size());
    close_msg_vector(parts);
    return out;
}

napi_value router_try_recv_message(napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *router = NULL;
    napi_get_value_external(env, argv[0], &router);

    zlink_routing_id_t peer_rid;
    zlink_routing_id_t spot_rid;
    uint64_t request_seq = 0;
    std::vector<zlink_msg_t> parts;
    int rc = router_recv_parts(
      router,
      &peer_rid,
      &spot_rid,
      &request_seq,
      &parts,
      ZLINK_RECV_FLAGS_DONTWAIT);
    if (rc != ZLINK_RECV_OK) {
        if (zlink_errno() == EAGAIN) {
            napi_value none;
            napi_get_null(env, &none);
            return none;
        }
        return throw_last_error(env, "routerRecvMessageNoWait failed");
    }

    napi_value out = create_router_recv_message_value(
      env,
      peer_rid,
      spot_rid.size > 0 ? &spot_rid : NULL,
      request_seq,
      parts.data(),
      parts.size());
    close_msg_vector(parts);
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

napi_value monitor_handler(napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *mon = NULL;
    napi_get_value_external(env, argv[0], &mon);

    napi_valuetype handler_type = napi_undefined;
    napi_typeof(env, argv[1], &handler_type);
    if (handler_type != napi_function) {
        napi_throw_type_error(env, NULL, "monitorHandler handler must be a function");
        return NULL;
    }

    if (!attach_socket_monitor_handler(env, mon, argv[1]))
        return NULL;

    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
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
    int rc = zlink_socket_monitor_recv(mon, &evt, ZLINK_RECV_FLAGS_NONE);
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
    int rc = zlink_socket_monitor_recv(mon, &evt, ZLINK_RECV_FLAGS_DONTWAIT);
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
    napi_value auto_hwm_enabled, auto_hwm_profile, auto_hwm_role;
    napi_value auto_hwm_policy_class;
    napi_value auto_hwm_unit_budget_bytes;
    napi_value auto_hwm_size_cap;
    napi_value auto_hwm_socket_message_slots;
    napi_value auto_hwm_effective_message_bytes;
    napi_value auto_hwm_applied_sndhwm, auto_hwm_applied_rcvhwm;
    napi_value auto_hwm_effective_sndbuf, auto_hwm_effective_rcvbuf;
    napi_value auto_hwm_last_recalc_ms;
    napi_value auto_hwm_last_recalc_reason;
    napi_value auto_hwm_send_blocked_ratio_ppm;
    napi_value auto_hwm_deferred_sndhwm, auto_hwm_deferred_rcvhwm;
    napi_create_uint32(env, static_cast<uint32_t>(snapshot.source_kind),
                       &source_kind);
    napi_create_uint32(env, snapshot.state_flags, &state_flags);
    napi_create_uint32(env, snapshot.detail_flags, &detail_flags);
    napi_create_int64(env, static_cast<int64_t>(snapshot.snd_pending_msgs),
                      &snd_pending);
    napi_create_int64(env, static_cast<int64_t>(snapshot.rcv_pending_msgs),
                      &rcv_pending);
    napi_get_boolean(env, snapshot.auto_hwm_enabled != 0, &auto_hwm_enabled);
    napi_create_uint32(env, snapshot.auto_hwm_profile, &auto_hwm_profile);
    napi_create_uint32(env, snapshot.auto_hwm_role, &auto_hwm_role);
    napi_create_uint32(env, snapshot.auto_hwm_policy_class,
                       &auto_hwm_policy_class);
    napi_create_int64(env,
                      static_cast<int64_t> (
                        snapshot.auto_hwm_unit_budget_bytes),
                      &auto_hwm_unit_budget_bytes);
    napi_create_uint32(env, snapshot.auto_hwm_size_cap, &auto_hwm_size_cap);
    napi_create_int64(env,
                      static_cast<int64_t> (
                        snapshot.auto_hwm_socket_message_slots),
                      &auto_hwm_socket_message_slots);
    napi_create_int64(env,
                      static_cast<int64_t> (
                        snapshot.auto_hwm_effective_message_bytes),
                      &auto_hwm_effective_message_bytes);
    napi_create_int32(env, snapshot.auto_hwm_applied_sndhwm,
                      &auto_hwm_applied_sndhwm);
    napi_create_int32(env, snapshot.auto_hwm_applied_rcvhwm,
                      &auto_hwm_applied_rcvhwm);
    napi_create_int32(env, snapshot.auto_hwm_effective_sndbuf,
                      &auto_hwm_effective_sndbuf);
    napi_create_int32(env, snapshot.auto_hwm_effective_rcvbuf,
                      &auto_hwm_effective_rcvbuf);
    napi_create_int64(env,
                      static_cast<int64_t> (snapshot.auto_hwm_last_recalc_ms),
                      &auto_hwm_last_recalc_ms);
    napi_create_uint32(env, snapshot.auto_hwm_last_recalc_reason,
                       &auto_hwm_last_recalc_reason);
    napi_create_uint32(env, snapshot.auto_hwm_send_blocked_ratio_ppm,
                       &auto_hwm_send_blocked_ratio_ppm);
    napi_create_int32(env, snapshot.auto_hwm_deferred_sndhwm,
                      &auto_hwm_deferred_sndhwm);
    napi_create_int32(env, snapshot.auto_hwm_deferred_rcvhwm,
                      &auto_hwm_deferred_rcvhwm);
    napi_set_named_property(env, obj, "sourceKind", source_kind);
    napi_set_named_property(env, obj, "stateFlags", state_flags);
    napi_set_named_property(env, obj, "detailFlags", detail_flags);
    napi_set_named_property(env, obj, "sndPendingMsgs", snd_pending);
    napi_set_named_property(env, obj, "rcvPendingMsgs", rcv_pending);
    napi_set_named_property(env, obj, "autoHwmEnabled", auto_hwm_enabled);
    napi_set_named_property(env, obj, "autoHwmProfile", auto_hwm_profile);
    napi_set_named_property(env, obj, "autoHwmRole", auto_hwm_role);
    napi_set_named_property(env, obj, "autoHwmPolicyClass",
                            auto_hwm_policy_class);
    napi_set_named_property(env, obj, "autoHwmUnitBudgetBytes",
                            auto_hwm_unit_budget_bytes);
    napi_set_named_property(env, obj, "autoHwmSizeCap", auto_hwm_size_cap);
    napi_set_named_property(env, obj, "autoHwmSocketMessageSlots",
                            auto_hwm_socket_message_slots);
    napi_set_named_property(env, obj, "autoHwmEffectiveMessageBytes",
                            auto_hwm_effective_message_bytes);
    napi_set_named_property(env, obj, "autoHwmAppliedSndHwm",
                            auto_hwm_applied_sndhwm);
    napi_set_named_property(env, obj, "autoHwmAppliedRcvHwm",
                            auto_hwm_applied_rcvhwm);
    napi_set_named_property(env, obj, "autoHwmEffectiveSndBuf",
                            auto_hwm_effective_sndbuf);
    napi_set_named_property(env, obj, "autoHwmEffectiveRcvBuf",
                            auto_hwm_effective_rcvbuf);
    napi_set_named_property(env, obj, "autoHwmLastRecalcMs",
                            auto_hwm_last_recalc_ms);
    napi_set_named_property(env, obj, "autoHwmLastRecalcReason",
                            auto_hwm_last_recalc_reason);
    napi_set_named_property(env, obj, "autoHwmSendBlockedRatioPpm",
                            auto_hwm_send_blocked_ratio_ppm);
    napi_set_named_property(env, obj, "autoHwmDeferredSndHwm",
                            auto_hwm_deferred_sndhwm);
    napi_set_named_property(env, obj, "autoHwmDeferredRcvHwm",
                            auto_hwm_deferred_rcvhwm);
    return obj;
}

napi_value monitor_close(napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *monitor = NULL;
    napi_get_value_external(env, argv[0], &monitor);
    release_socket_monitor_handler_slot(monitor);
    void *tmp = monitor;
    int rc = zlink_monitor_close(&tmp);
    if (rc != 0)
        return throw_last_error(env, "monitor_close failed");
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

static napi_value create_external_or_null(napi_env env, void *ptr)
{
    napi_value out;
    if (!ptr) {
        napi_get_null(env, &out);
        return out;
    }
    napi_create_external(env, ptr, NULL, NULL, &out);
    return out;
}

static napi_value create_userdata_value(napi_env env, void *ptr)
{
    napi_value out;
    if (!ptr) {
        napi_get_null(env, &out);
        return out;
    }
    napi_create_bigint_uint64(
      env, static_cast<uint64_t>(reinterpret_cast<uintptr_t>(ptr)), &out);
    return out;
}

static void *get_external_or_null(napi_env env, napi_value value)
{
    if (!value)
        return NULL;
    napi_valuetype type;
    if (napi_typeof(env, value, &type) != napi_ok)
        return NULL;
    if (type == napi_null || type == napi_undefined)
        return NULL;
    if (type == napi_bigint) {
        uint64_t raw = 0;
        bool lossless = false;
        if (napi_get_value_bigint_uint64(env, value, &raw, &lossless) == napi_ok
            && lossless)
            return reinterpret_cast<void *>(static_cast<uintptr_t>(raw));
        return NULL;
    }
    if (type == napi_number) {
        uint32_t raw = 0;
        if (napi_get_value_uint32(env, value, &raw) == napi_ok)
            return reinterpret_cast<void *>(static_cast<uintptr_t>(raw));
        return NULL;
    }
    if (type != napi_external)
        return NULL;
    void *ptr = NULL;
    if (napi_get_value_external(env, value, &ptr) != napi_ok)
        return NULL;
    return ptr;
}

static napi_value create_poller_event_value(napi_env env,
                                           const zlink_poller_event_t &event)
{
    napi_value obj;
    napi_create_object(env, &obj);

    napi_value value;
    napi_create_uint32(env, static_cast<uint32_t>(event.source_kind), &value);
    napi_set_named_property(env, obj, "sourceKind", value);
    napi_set_named_property(env, obj, "socket", create_external_or_null(env, event.socket));
    napi_create_int64(env, static_cast<int64_t>(event.fd), &value);
    napi_set_named_property(env, obj, "fd", value);
    napi_set_named_property(env, obj, "timer", create_external_or_null(env, event.timer));
    napi_set_named_property(env, obj, "userData", create_userdata_value(env, event.user_data));
    napi_create_int32(env, static_cast<int32_t>(event.events), &value);
    napi_set_named_property(env, obj, "events", value);
    return obj;
}

static napi_value create_poller_event_result(napi_env env,
                                            const zlink_poller_event_t &event,
                                            int rc)
{
    if (rc <= 0) {
        napi_value none;
        napi_get_null(env, &none);
        return none;
    }
    return create_poller_event_value(env, event);
}

static napi_value create_spot_routed_value(napi_env env,
                                          const zlink_routing_id_t *source_rid,
                                          const zlink_routing_id_t *spot_rid,
                                          uint64_t request_seq,
                                          zlink_msg_t *parts,
                                          size_t part_count)
{
    napi_value obj;
    napi_create_object(env, &obj);

    napi_value rid = source_rid ? create_routing_id_value(env, *source_rid)
                                : create_external_or_null(env, NULL);
    napi_set_named_property(env, obj, "sourceRid", rid);
    napi_value spot = spot_rid ? create_routing_id_value(env, *spot_rid)
                               : create_external_or_null(env, NULL);
    napi_set_named_property(env, obj, "spotRid", spot);

    napi_value request_seq_value;
    if (request_seq == 0) {
        napi_get_null(env, &request_seq_value);
    } else {
        napi_create_bigint_uint64(env, request_seq, &request_seq_value);
    }
    napi_set_named_property(env, obj, "requestSeq", request_seq_value);

    napi_value parts_array;
    napi_create_array_with_length(env, part_count, &parts_array);
    for (size_t i = 0; i < part_count; ++i) {
        napi_value part_obj;
        napi_create_object(env, &part_obj);
        napi_value part_buf;
        napi_create_buffer_copy(
          env,
          zlink_msg_size(&parts[i]),
          zlink_msg_size(&parts[i]) > 0 ? zlink_msg_data(&parts[i]) : NULL,
          NULL,
          &part_buf);
        napi_set_named_property(env, part_obj, "data", part_buf);
        napi_set_element(env, parts_array, static_cast<uint32_t>(i), part_obj);
    }
    napi_set_named_property(env, obj, "parts", parts_array);
    return obj;
}

static napi_value create_spot_routed_recv_value(napi_env env,
                                               const zlink_routing_id_t *source_rid,
                                               const zlink_routing_id_t *spot_rid,
                                               uint64_t request_seq,
                                               zlink_msg_t *parts,
                                               size_t part_count)
{
    return create_spot_routed_value(env, source_rid, spot_rid, request_seq, parts, part_count);
}

struct timer_handler_js_state_t
{
    timer_handler_js_state_t () : used (false), timer (NULL), env (NULL), tsfn (NULL) {}

    bool used;
    void *timer;
    napi_env env;
    napi_threadsafe_function tsfn;
};

static const size_t k_timer_handler_slot_count = 8;
static std::mutex g_timer_handler_slots_mu;
static timer_handler_js_state_t g_timer_handler_slots[k_timer_handler_slot_count];

static timer_handler_js_state_t *find_timer_handler_slot_by_timer_unsafe(void *timer)
{
    for (size_t i = 0; i < k_timer_handler_slot_count; ++i) {
        if (g_timer_handler_slots[i].used && g_timer_handler_slots[i].timer == timer)
            return &g_timer_handler_slots[i];
    }
    return NULL;
}

static timer_handler_js_state_t *find_free_timer_handler_slot_unsafe()
{
    for (size_t i = 0; i < k_timer_handler_slot_count; ++i) {
        if (!g_timer_handler_slots[i].used)
            return &g_timer_handler_slots[i];
    }
    return NULL;
}

static void reset_timer_handler_slot_unsafe(timer_handler_js_state_t *state)
{
    if (!state)
        return;
    state->used = false;
    state->timer = NULL;
    state->env = NULL;
    state->tsfn = NULL;
}

static void timer_handler_tsfn_finalize(napi_env env,
                                        void *finalize_data,
                                        void *finalize_hint)
{
    (void) env;
    (void) finalize_hint;
    timer_handler_js_state_t *state =
      static_cast<timer_handler_js_state_t *>(finalize_data);
    if (!state)
        return;
    std::lock_guard<std::mutex> lock(g_timer_handler_slots_mu);
    reset_timer_handler_slot_unsafe(state);
}

static void timer_handler_tsfn_call_js(napi_env env,
                                       napi_value js_cb,
                                       void *context,
                                       void *data)
{
    (void) context;
    std::unique_ptr<uint64_t> fire_count(static_cast<uint64_t *>(data));
    if (!env || !js_cb || !fire_count)
        return;

    napi_value argv[1];
    napi_create_bigint_uint64(env, *fire_count, &argv[0]);
    napi_value recv;
    napi_value this_arg;
    napi_get_undefined(env, &this_arg);
    (void) napi_call_function(env, this_arg, js_cb, 1, argv, &recv);
}

static void release_timer_handler_slot(void *timer)
{
    napi_threadsafe_function tsfn = NULL;
    {
        std::lock_guard<std::mutex> lock(g_timer_handler_slots_mu);
        timer_handler_js_state_t *state =
          find_timer_handler_slot_by_timer_unsafe(timer);
        if (!state)
            return;
        tsfn = state->tsfn;
        reset_timer_handler_slot_unsafe(state);
    }
    if (tsfn)
        (void) napi_release_threadsafe_function(tsfn, napi_tsfn_abort);
}

static void timer_handler_dispatch(void *timer_, uint64_t fire_count_, void *closure)
{
    (void) timer_;
    timer_handler_js_state_t *state =
      static_cast<timer_handler_js_state_t *>(closure);
    if (!state)
        return;

    napi_threadsafe_function tsfn = NULL;
    {
        std::lock_guard<std::mutex> lock(g_timer_handler_slots_mu);
        if (!state->used || !state->tsfn)
            return;
        tsfn = state->tsfn;
    }

    std::unique_ptr<uint64_t> payload(new uint64_t(fire_count_));
    if (napi_call_threadsafe_function(
          tsfn, payload.get(), napi_tsfn_nonblocking)
        != napi_ok) {
        return;
    }
    (void) payload.release();
}

static napi_value create_stopwatch_value(napi_env env, unsigned long value)
{
    napi_value out;
    napi_create_double(env, static_cast<double>(value), &out);
    return out;
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
    zlink_config_result_t err = ZLINK_CONFIG_OK;
    int rc = zlink_poll(items.data(), items.size(), timeout, &err);
    if (err != ZLINK_CONFIG_OK)
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

napi_value poller_new(napi_env env, napi_callback_info info)
{
    (void) info;
    void *poller = zlink_poller_new();
    if (!poller)
        return throw_last_error(env, "poller_new failed");
    napi_value out;
    napi_create_external(env, poller, NULL, NULL, &out);
    return out;
}

napi_value poller_destroy(napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *poller = NULL;
    napi_get_value_external(env, argv[0], &poller);
    void *tmp = poller;
    int rc = zlink_poller_destroy(&tmp);
    if (rc != 0)
        return throw_last_error(env, "poller_destroy failed");
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

napi_value poller_size(napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *poller = NULL;
    napi_get_value_external(env, argv[0], &poller);
    zlink_config_result_t err = ZLINK_CONFIG_OK;
    int size = zlink_poller_size(poller, &err);
    if (err != ZLINK_CONFIG_OK)
        return throw_last_error(env, "poller_size failed");
    napi_value out;
    napi_create_int32(env, size, &out);
    return out;
}

napi_value poller_add(napi_env env, napi_callback_info info)
{
    napi_value argv[4];
    size_t argc = 4;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *poller = NULL;
    void *socket = NULL;
    napi_get_value_external(env, argv[0], &poller);
    napi_get_value_external(env, argv[1], &socket);
    int32_t events = 0;
    napi_get_value_int32(env, argv[3], &events);
    void *user_data = argc >= 3 ? get_external_or_null(env, argv[2]) : NULL;
    zlink_config_result_t rc = zlink_poller_add(poller, socket, user_data, (short)events);
    if (rc != ZLINK_CONFIG_OK)
        return throw_last_error(env, "poller_add failed");
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

napi_value poller_modify(napi_env env, napi_callback_info info)
{
    napi_value argv[3];
    size_t argc = 3;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *poller = NULL;
    void *socket = NULL;
    napi_get_value_external(env, argv[0], &poller);
    napi_get_value_external(env, argv[1], &socket);
    int32_t events = 0;
    napi_get_value_int32(env, argv[2], &events);
    zlink_config_result_t rc = zlink_poller_modify(poller, socket, (short)events);
    if (rc != ZLINK_CONFIG_OK)
        return throw_last_error(env, "poller_modify failed");
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

napi_value poller_remove(napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *poller = NULL;
    void *socket = NULL;
    napi_get_value_external(env, argv[0], &poller);
    napi_get_value_external(env, argv[1], &socket);
    zlink_config_result_t rc = zlink_poller_remove(poller, socket);
    if (rc != ZLINK_CONFIG_OK)
        return throw_last_error(env, "poller_remove failed");
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

napi_value poller_add_fd(napi_env env, napi_callback_info info)
{
    napi_value argv[4];
    size_t argc = 4;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *poller = NULL;
    napi_get_value_external(env, argv[0], &poller);
    int32_t fd = 0, events = 0;
    napi_get_value_int32(env, argv[1], &fd);
    napi_get_value_int32(env, argv[3], &events);
    void *user_data = argc >= 3 ? get_external_or_null(env, argv[2]) : NULL;
    zlink_config_result_t rc = zlink_poller_add_fd(poller, fd, user_data, (short)events);
    if (rc != ZLINK_CONFIG_OK)
        return throw_last_error(env, "poller_add_fd failed");
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

napi_value poller_modify_fd(napi_env env, napi_callback_info info)
{
    napi_value argv[3];
    size_t argc = 3;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *poller = NULL;
    napi_get_value_external(env, argv[0], &poller);
    int32_t fd = 0, events = 0;
    napi_get_value_int32(env, argv[1], &fd);
    napi_get_value_int32(env, argv[2], &events);
    zlink_config_result_t rc = zlink_poller_modify_fd(poller, fd, (short)events);
    if (rc != ZLINK_CONFIG_OK)
        return throw_last_error(env, "poller_modify_fd failed");
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

napi_value poller_remove_fd(napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *poller = NULL;
    napi_get_value_external(env, argv[0], &poller);
    int32_t fd = 0;
    napi_get_value_int32(env, argv[1], &fd);
    zlink_config_result_t rc = zlink_poller_remove_fd(poller, fd);
    if (rc != ZLINK_CONFIG_OK)
        return throw_last_error(env, "poller_remove_fd failed");
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

napi_value poller_add_timer(napi_env env, napi_callback_info info)
{
    napi_value argv[3];
    size_t argc = 3;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *poller = NULL;
    void *timer = NULL;
    napi_get_value_external(env, argv[0], &poller);
    napi_get_value_external(env, argv[1], &timer);
    void *user_data = argc >= 3 ? get_external_or_null(env, argv[2]) : NULL;
    zlink_config_result_t rc = zlink_poller_add_timer(poller, timer, user_data);
    if (rc != ZLINK_CONFIG_OK)
        return throw_last_error(env, "poller_add_timer failed");
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

napi_value poller_remove_timer(napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *poller = NULL;
    void *timer = NULL;
    napi_get_value_external(env, argv[0], &poller);
    napi_get_value_external(env, argv[1], &timer);
    zlink_config_result_t rc = zlink_poller_remove_timer(poller, timer);
    if (rc != ZLINK_CONFIG_OK)
        return throw_last_error(env, "poller_remove_timer failed");
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

napi_value poller_wait(napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *poller = NULL;
    napi_get_value_external(env, argv[0], &poller);
    int32_t timeout = 0;
    napi_get_value_int32(env, argv[1], &timeout);
    zlink_poller_event_t event;
    memset(&event, 0, sizeof(event));
    zlink_config_result_t err = ZLINK_CONFIG_OK;
    int rc = zlink_poller_wait(poller, &event, 1, timeout, &err);
    if (err != ZLINK_CONFIG_OK)
        return throw_last_error(env, "poller_wait failed");
    return create_poller_event_result(env, event, rc);
}

napi_value poller_wait_many(napi_env env, napi_callback_info info)
{
    napi_value argv[3];
    size_t argc = 3;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *poller = NULL;
    napi_get_value_external(env, argv[0], &poller);
    int32_t n_events = 0;
    int32_t timeout = 0;
    napi_get_value_int32(env, argv[1], &n_events);
    napi_get_value_int32(env, argv[2], &timeout);
    if (n_events <= 0) {
        napi_throw_range_error(env, NULL, "maxEvents must be a positive integer");
        return NULL;
    }
    std::vector<zlink_poller_event_t> events(static_cast<size_t>(n_events));
    zlink_config_result_t err = ZLINK_CONFIG_OK;
    int rc = zlink_poller_wait(
      poller, events.data(), n_events, timeout, &err);
    if (err != ZLINK_CONFIG_OK)
        return throw_last_error(env, "poller_wait_many failed");
    napi_value out;
    napi_create_array_with_length(env, rc > 0 ? static_cast<size_t>(rc) : 0, &out);
    for (int i = 0; i < rc; ++i) {
        napi_value event_value = create_poller_event_value(env, events[static_cast<size_t>(i)]);
        napi_set_element(env, out, static_cast<uint32_t>(i), event_value);
    }
    return out;
}

napi_value timer_new(napi_env env, napi_callback_info info)
{
    (void) info;
    void *timer = zlink_timer_new();
    if (!timer)
        return throw_last_error(env, "timer_new failed");
    napi_value out;
    napi_create_external(env, timer, NULL, NULL, &out);
    return out;
}

napi_value spot_timer_new(napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *spot = NULL;
    napi_get_value_external(env, argv[0], &spot);
    void *timer = zlink_spot_timer_new(spot);
    if (!timer)
        return throw_last_error(env, "spot_timer_new failed");
    napi_value out;
    napi_create_external(env, timer, NULL, NULL, &out);
    return out;
}

napi_value timer_destroy(napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *timer = NULL;
    napi_get_value_external(env, argv[0], &timer);
    release_timer_handler_slot(timer);
    void *tmp = timer;
    int rc = zlink_timer_destroy(&tmp);
    if (rc != 0)
        return throw_last_error(env, "timer_destroy failed");
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

napi_value timer_start(napi_env env, napi_callback_info info)
{
    napi_value argv[3];
    size_t argc = 3;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *timer = NULL;
    napi_get_value_external(env, argv[0], &timer);
    bool lossless = false;
    uint64_t interval = 0;
    uint64_t repeat = 0;
    napi_get_value_bigint_uint64(env, argv[1], &interval, &lossless);
    napi_get_value_bigint_uint64(env, argv[2], &repeat, &lossless);
    zlink_config_result_t rc = zlink_timer_start(timer, interval, repeat);
    if (rc != ZLINK_CONFIG_OK)
        return throw_last_error(env, "timer_start failed");
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

napi_value timer_stop(napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *timer = NULL;
    napi_get_value_external(env, argv[0], &timer);
    zlink_config_result_t rc = zlink_timer_stop(timer);
    if (rc != ZLINK_CONFIG_OK)
        return throw_last_error(env, "timer_stop failed");
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

napi_value timer_recv(napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *timer = NULL;
    napi_get_value_external(env, argv[0], &timer);
    int32_t flags = 0;
    if (argc >= 2)
        napi_get_value_int32(env, argv[1], &flags);
    uint64_t fire_count = 0;
    zlink_recv_result_t rc = zlink_timer_recv(timer, &fire_count);
    if (rc != ZLINK_RECV_OK)
        return throw_last_error(env, "timer_recv failed");
    napi_value out;
    napi_create_bigint_uint64(env, fire_count, &out);
    return out;
}

napi_value timer_handler(napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *timer = NULL;
    napi_get_value_external(env, argv[0], &timer);
    napi_value handler = argv[1];

    timer_handler_js_state_t *slot = NULL;
    {
        std::lock_guard<std::mutex> lock(g_timer_handler_slots_mu);
        if (find_timer_handler_slot_by_timer_unsafe(timer)) {
            napi_throw_error(env, NULL, "timer handler already attached");
            return NULL;
        }
        slot = find_free_timer_handler_slot_unsafe();
        if (!slot) {
            napi_throw_error(env, NULL, "no free timer handler slot");
            return NULL;
        }
    }

    napi_value resource_name;
    napi_create_string_utf8(
      env, "zlink-timer-handler", NAPI_AUTO_LENGTH, &resource_name);
    napi_threadsafe_function tsfn = NULL;
    napi_status tsfn_status = napi_create_threadsafe_function(
      env, handler, NULL, resource_name, 0, 1, slot,
      timer_handler_tsfn_finalize, slot, timer_handler_tsfn_call_js, &tsfn);
    if (tsfn_status != napi_ok) {
        napi_throw_error(
          env, NULL, "timer handler failed to create callback queue");
        return NULL;
    }

    {
        std::lock_guard<std::mutex> lock(g_timer_handler_slots_mu);
        slot->used = true;
        slot->timer = timer;
        slot->env = env;
        slot->tsfn = tsfn;
    }

    zlink_handler_result_t rc = zlink_timer_handler(timer, &timer_handler_dispatch, slot);
    if (rc != ZLINK_HANDLER_OK) {
        release_timer_handler_slot(timer);
        return throw_last_error(env, "timer_handler failed");
    }
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

napi_value stopwatch_start(napi_env env, napi_callback_info info)
{
    (void) info;
    void *watch = zlink_stopwatch_start();
    if (!watch)
        return throw_last_error(env, "stopwatch_start failed");
    napi_value out;
    napi_create_external(env, watch, NULL, NULL, &out);
    return out;
}

napi_value stopwatch_intermediate(napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *watch = NULL;
    napi_get_value_external(env, argv[0], &watch);
    unsigned long value = zlink_stopwatch_intermediate(watch);
    return create_stopwatch_value(env, value);
}

napi_value stopwatch_stop(napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *watch = NULL;
    napi_get_value_external(env, argv[0], &watch);
    unsigned long value = zlink_stopwatch_stop(watch);
    return create_stopwatch_value(env, value);
}

napi_value atomic_counter_new(napi_env env, napi_callback_info info)
{
    (void) info;
    void *counter = zlink_atomic_counter_new();
    if (!counter)
        return throw_last_error(env, "atomic_counter_new failed");
    napi_value out;
    napi_create_external(env, counter, NULL, NULL, &out);
    return out;
}

napi_value atomic_counter_set(napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *counter = NULL;
    int32_t value = 0;
    napi_get_value_external(env, argv[0], &counter);
    napi_get_value_int32(env, argv[1], &value);
    zlink_atomic_counter_set(counter, value);
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

napi_value atomic_counter_inc(napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *counter = NULL;
    napi_get_value_external(env, argv[0], &counter);
    napi_value out;
    napi_create_int32(env, zlink_atomic_counter_inc(counter), &out);
    return out;
}

napi_value atomic_counter_dec(napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *counter = NULL;
    napi_get_value_external(env, argv[0], &counter);
    napi_value out;
    napi_create_int32(env, zlink_atomic_counter_dec(counter), &out);
    return out;
}

napi_value atomic_counter_value(napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *counter = NULL;
    napi_get_value_external(env, argv[0], &counter);
    napi_value out;
    napi_create_int32(env, zlink_atomic_counter_value(counter), &out);
    return out;
}

napi_value atomic_counter_destroy(napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *counter = NULL;
    napi_get_value_external(env, argv[0], &counter);
    zlink_atomic_counter_destroy(&counter);
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}
