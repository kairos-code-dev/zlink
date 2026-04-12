/* SPDX-License-Identifier: MPL-2.0 */

#include "addon_api.h"
#include <memory>
#include <mutex>
#include <vector>
#include <errno.h>

namespace {

static napi_value create_routing_id_value(napi_env env, const zlink_routing_id_t &rid);
static napi_value unsupported_spot_node(napi_env env, const char *method);

static const size_t k_spot_send_ready_slot_count = 8;

struct spot_send_ready_js_state_t
{
    spot_send_ready_js_state_t () : used (false), spot (NULL), env (NULL), tsfn (NULL) {}

    bool used;
    void *spot;
    napi_env env;
    napi_threadsafe_function tsfn;
};

static std::mutex g_spot_send_ready_slots_mu;
static spot_send_ready_js_state_t
  g_spot_send_ready_slots[k_spot_send_ready_slot_count];

static bool parse_routing_id_value(napi_env env,
                                   napi_value value,
                                   zlink_routing_id_t *routing_id)
{
    void *data = NULL;
    size_t len = 0;
    if (napi_get_buffer_info(env, value, &data, &len) != napi_ok) {
        napi_throw_type_error(env, NULL, "routingId must be Buffer");
        return false;
    }
    if (len == 0 || len > sizeof(routing_id->data)) {
        napi_throw_range_error(env, NULL, "routingId length must be 1..255 bytes");
        return false;
    }
    memset(routing_id, 0, sizeof(*routing_id));
    routing_id->size = static_cast<uint8_t>(len);
    memcpy(routing_id->data, data, len);
    return true;
}

static const size_t k_spot_routed_slot_count = 8;
static const size_t k_router_spot_slot_count = 8;
static const size_t k_spot_dispatch_event_slot_count = 8;

struct spot_routed_js_payload_t
{
    std::vector<unsigned char> source_rid;
    std::vector<unsigned char> spot_rid;
    uint64_t request_seq;
    std::vector<std::vector<unsigned char> > parts;
};

struct spot_routed_js_state_t
{
    spot_routed_js_state_t () : used (false), spot (NULL), env (NULL), tsfn (NULL) {}

    bool used;
    void *spot;
    napi_env env;
    napi_threadsafe_function tsfn;
};

struct router_spot_js_state_t
{
    router_spot_js_state_t () : used (false), router (NULL), env (NULL), tsfn (NULL) {}

    bool used;
    void *router;
    napi_env env;
    napi_threadsafe_function tsfn;
};

struct spot_dispatch_event_js_payload_t
{
    int event;
};

struct spot_dispatch_event_js_state_t
{
    spot_dispatch_event_js_state_t ()
      : used (false), spot (NULL), env (NULL), tsfn (NULL) {}

    bool used;
    void *spot;
    napi_env env;
    napi_threadsafe_function tsfn;
};

static std::mutex g_spot_routed_slots_mu;
static spot_routed_js_state_t g_spot_routed_slots[k_spot_routed_slot_count];
static std::mutex g_router_spot_slots_mu;
static router_spot_js_state_t g_router_spot_slots[k_router_spot_slot_count];
static std::mutex g_spot_dispatch_event_slots_mu;
static spot_dispatch_event_js_state_t
  g_spot_dispatch_event_slots[k_spot_dispatch_event_slot_count];

static spot_routed_js_state_t *find_spot_routed_slot_by_spot_unsafe(void *spot)
{
    for (size_t i = 0; i < k_spot_routed_slot_count; ++i) {
        if (g_spot_routed_slots[i].used && g_spot_routed_slots[i].spot == spot)
            return &g_spot_routed_slots[i];
    }
    return NULL;
}

static spot_routed_js_state_t *find_free_spot_routed_slot_unsafe()
{
    for (size_t i = 0; i < k_spot_routed_slot_count; ++i) {
        if (!g_spot_routed_slots[i].used)
            return &g_spot_routed_slots[i];
    }
    return NULL;
}

static void reset_spot_routed_slot_unsafe(spot_routed_js_state_t *state)
{
    if (!state)
        return;
    state->used = false;
    state->spot = NULL;
    state->env = NULL;
    state->tsfn = NULL;
}

static void spot_routed_tsfn_finalize(napi_env env,
                                      void *finalize_data,
                                      void *finalize_hint)
{
    (void) env;
    (void) finalize_hint;
    spot_routed_js_state_t *state =
      static_cast<spot_routed_js_state_t *>(finalize_data);
    if (!state)
        return;
    std::lock_guard<std::mutex> lock(g_spot_routed_slots_mu);
    reset_spot_routed_slot_unsafe(state);
}

static void spot_routed_tsfn_call_js(napi_env env,
                                     napi_value js_cb,
                                     void *context,
                                     void *data)
{
    (void) context;
    std::unique_ptr<spot_routed_js_payload_t> payload(
      static_cast<spot_routed_js_payload_t *>(data));
    if (!env || !js_cb || !payload)
        return;

    napi_value argv[4];
    if (!payload->source_rid.empty()) {
        if (napi_create_buffer_copy(
              env, payload->source_rid.size(), payload->source_rid.data(), NULL,
              &argv[0])
            != napi_ok) {
            return;
        }
    } else {
        napi_get_null(env, &argv[0]);
    }
    if (!payload->spot_rid.empty()) {
        if (napi_create_buffer_copy(
              env, payload->spot_rid.size(), payload->spot_rid.data(), NULL,
              &argv[1])
            != napi_ok) {
            return;
        }
    } else {
        napi_get_null(env, &argv[1]);
    }
    napi_create_bigint_uint64(env, payload->request_seq, &argv[2]);
    if (napi_create_array_with_length(env, payload->parts.size(), &argv[3])
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
        napi_set_element(env, argv[3], static_cast<uint32_t>(i), part_buf);
    }

    napi_value recv;
    napi_value this_arg;
    napi_get_undefined(env, &this_arg);
    (void) napi_call_function(env, this_arg, js_cb, 4, argv, &recv);
}

static void release_spot_routed_handler_slot(void *spot)
{
    napi_threadsafe_function tsfn = NULL;
    {
        std::lock_guard<std::mutex> lock(g_spot_routed_slots_mu);
        spot_routed_js_state_t *state = find_spot_routed_slot_by_spot_unsafe(spot);
        if (!state)
            return;
        tsfn = state->tsfn;
        reset_spot_routed_slot_unsafe(state);
    }
    if (tsfn)
        (void) napi_release_threadsafe_function(tsfn, napi_tsfn_abort);
}

static void spot_routed_dispatch(void *closure,
                                 const zlink_routing_id_t *source_rid,
                                 const zlink_routing_id_t *spot_rid,
                                 uint64_t request_seq,
                                 zlink_msg_t *parts,
                                 size_t part_count,
                                 void *userdata)
{
    (void) userdata;
    spot_routed_js_state_t *state =
      static_cast<spot_routed_js_state_t *>(closure);
    if (!state)
        return;

    napi_threadsafe_function tsfn = NULL;
    {
        std::lock_guard<std::mutex> lock(g_spot_routed_slots_mu);
        if (!state->used || !state->tsfn)
            return;
        tsfn = state->tsfn;
    }

    std::unique_ptr<spot_routed_js_payload_t> payload(
      new spot_routed_js_payload_t());
    if (source_rid && source_rid->size > 0) {
        payload->source_rid.assign(source_rid->data, source_rid->data + source_rid->size);
    }
    if (spot_rid && spot_rid->size > 0) {
        payload->spot_rid.assign(spot_rid->data, spot_rid->data + spot_rid->size);
    }
    payload->request_seq = request_seq;
    payload->parts.reserve(part_count);
    for (size_t i = 0; i < part_count; ++i) {
        const size_t size = zlink_msg_size(&parts[i]);
        const unsigned char *data = static_cast<const unsigned char *>(zlink_msg_data(&parts[i]));
        payload->parts.push_back(std::vector<unsigned char>(data, data + size));
    }

    if (napi_call_threadsafe_function(tsfn, payload.get(), napi_tsfn_nonblocking)
        != napi_ok) {
        return;
    }
    (void) payload.release();
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

    napi_value source_value = source_rid ? create_routing_id_value(env, *source_rid)
                                         : (napi_value) NULL;
    if (!source_rid || source_rid->size == 0) {
        napi_get_null(env, &source_value);
    }
    napi_set_named_property(env, obj, "sourceRid", source_value);
    napi_set_named_property(env, obj, "sourceNodeRid", source_value);

    napi_value spot_value = spot_rid ? create_routing_id_value(env, *spot_rid)
                                     : (napi_value) NULL;
    if (!spot_rid || spot_rid->size == 0) {
        napi_get_null(env, &spot_value);
    }
    napi_set_named_property(env, obj, "spotRid", spot_value);
    napi_set_named_property(env, obj, "sourceSpotRid", spot_value);

    napi_value request_seq_value;
    napi_create_bigint_uint64(env, request_seq, &request_seq_value);
    napi_set_named_property(env, obj, "requestSeq", request_seq_value);

    napi_value parts_array;
    napi_create_array_with_length(env, part_count, &parts_array);
    for (size_t i = 0; i < part_count; ++i) {
        napi_value part_buf;
        napi_create_buffer_copy(
          env,
          zlink_msg_size(&parts[i]),
          zlink_msg_size(&parts[i]) > 0 ? zlink_msg_data(&parts[i]) : NULL,
          NULL,
          &part_buf);
        napi_set_element(env, parts_array, static_cast<uint32_t>(i), part_buf);
    }
    napi_set_named_property(env, obj, "parts", parts_array);
    return obj;
}

static napi_value create_spot_routed_event_value(napi_env env,
                                                 const zlink_routing_id_t *source_rid,
                                                 const zlink_routing_id_t *spot_rid,
                                                 uint64_t request_seq,
                                                 zlink_msg_t *parts,
                                                 size_t part_count)
{
    return create_spot_routed_value(env, source_rid, spot_rid, request_seq, parts, part_count);
}

static spot_dispatch_event_js_state_t *find_spot_dispatch_event_slot_by_spot_unsafe(void *spot)
{
    for (size_t i = 0; i < k_spot_dispatch_event_slot_count; ++i) {
        if (g_spot_dispatch_event_slots[i].used
            && g_spot_dispatch_event_slots[i].spot == spot) {
            return &g_spot_dispatch_event_slots[i];
        }
    }
    return NULL;
}

static spot_dispatch_event_js_state_t *find_free_spot_dispatch_event_slot_unsafe()
{
    for (size_t i = 0; i < k_spot_dispatch_event_slot_count; ++i) {
        if (!g_spot_dispatch_event_slots[i].used)
            return &g_spot_dispatch_event_slots[i];
    }
    return NULL;
}

static void reset_spot_dispatch_event_slot_unsafe(spot_dispatch_event_js_state_t *state)
{
    if (!state)
        return;
    state->used = false;
    state->spot = NULL;
    state->env = NULL;
    state->tsfn = NULL;
}

static void spot_dispatch_event_tsfn_finalize(napi_env env,
                                              void *finalize_data,
                                              void *finalize_hint)
{
    (void) env;
    (void) finalize_hint;
    spot_dispatch_event_js_state_t *state =
      static_cast<spot_dispatch_event_js_state_t *>(finalize_data);
    if (!state)
        return;
    std::lock_guard<std::mutex> lock(g_spot_dispatch_event_slots_mu);
    reset_spot_dispatch_event_slot_unsafe(state);
}

static void spot_dispatch_event_tsfn_call_js(napi_env env,
                                             napi_value js_cb,
                                             void *context,
                                             void *data)
{
    (void) context;
    std::unique_ptr<spot_dispatch_event_js_payload_t> payload(
      static_cast<spot_dispatch_event_js_payload_t *>(data));
    if (!env || !js_cb || !payload)
        return;

    napi_value argv[1];
    napi_create_int32(env, payload->event, &argv[0]);
    napi_value recv;
    napi_value this_arg;
    napi_get_undefined(env, &this_arg);
    (void) napi_call_function(env, this_arg, js_cb, 1, argv, &recv);
}

static void release_spot_dispatch_event_handler_slot(void *spot)
{
    napi_threadsafe_function tsfn = NULL;
    {
        std::lock_guard<std::mutex> lock(g_spot_dispatch_event_slots_mu);
        spot_dispatch_event_js_state_t *state =
          find_spot_dispatch_event_slot_by_spot_unsafe(spot);
        if (!state)
            return;
        tsfn = state->tsfn;
        reset_spot_dispatch_event_slot_unsafe(state);
    }
    if (tsfn)
        (void) napi_release_threadsafe_function(tsfn, napi_tsfn_abort);
}

static void spot_dispatch_event_dispatch(void *closure, void *spot_, int event)
{
    (void) spot_;
    spot_dispatch_event_js_state_t *state =
      static_cast<spot_dispatch_event_js_state_t *>(closure);
    if (!state)
        return;

    napi_threadsafe_function tsfn = NULL;
    {
        std::lock_guard<std::mutex> lock(g_spot_dispatch_event_slots_mu);
        if (!state->used || !state->tsfn)
            return;
        tsfn = state->tsfn;
    }

    std::unique_ptr<spot_dispatch_event_js_payload_t> payload(
      new spot_dispatch_event_js_payload_t());
    payload->event = event;
    if (napi_call_threadsafe_function(tsfn, payload.get(), napi_tsfn_nonblocking)
        != napi_ok) {
        return;
    }
    (void) payload.release();
}

static router_spot_js_state_t *find_router_spot_slot_by_router_unsafe(void *router)
{
    for (size_t i = 0; i < k_router_spot_slot_count; ++i) {
        if (g_router_spot_slots[i].used && g_router_spot_slots[i].router == router)
            return &g_router_spot_slots[i];
    }
    return NULL;
}

static router_spot_js_state_t *find_free_router_spot_slot_unsafe()
{
    for (size_t i = 0; i < k_router_spot_slot_count; ++i) {
        if (!g_router_spot_slots[i].used)
            return &g_router_spot_slots[i];
    }
    return NULL;
}

static void reset_router_spot_slot_unsafe(router_spot_js_state_t *state)
{
    if (!state)
        return;
    state->used = false;
    state->router = NULL;
    state->env = NULL;
    state->tsfn = NULL;
}

static void router_spot_tsfn_finalize(napi_env env,
                                      void *finalize_data,
                                      void *finalize_hint)
{
    (void) env;
    (void) finalize_hint;
    router_spot_js_state_t *state =
      static_cast<router_spot_js_state_t *>(finalize_data);
    if (!state)
        return;
    std::lock_guard<std::mutex> lock(g_router_spot_slots_mu);
    reset_router_spot_slot_unsafe(state);
}

static void router_spot_tsfn_call_js(napi_env env,
                                     napi_value js_cb,
                                     void *context,
                                     void *data)
{
    (void) context;
    std::unique_ptr<spot_routed_js_payload_t> payload(
      static_cast<spot_routed_js_payload_t *>(data));
    if (!env || !js_cb || !payload)
        return;

    napi_value argv[4];
    if (!payload->source_rid.empty()) {
        if (napi_create_buffer_copy(
              env, payload->source_rid.size(), payload->source_rid.data(), NULL,
              &argv[0])
            != napi_ok) {
            return;
        }
    } else {
        napi_get_null(env, &argv[0]);
    }
    if (!payload->spot_rid.empty()) {
        if (napi_create_buffer_copy(
              env, payload->spot_rid.size(), payload->spot_rid.data(), NULL,
              &argv[1])
            != napi_ok) {
            return;
        }
    } else {
        napi_get_null(env, &argv[1]);
    }
    napi_create_bigint_uint64(env, payload->request_seq, &argv[2]);
    if (napi_create_array_with_length(env, payload->parts.size(), &argv[3])
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
        napi_set_element(env, argv[3], static_cast<uint32_t>(i), part_buf);
    }

    napi_value recv;
    napi_value this_arg;
    napi_get_undefined(env, &this_arg);
    (void) napi_call_function(env, this_arg, js_cb, 4, argv, &recv);
}

static void release_router_spot_handler_slot(void *router)
{
    napi_threadsafe_function tsfn = NULL;
    {
        std::lock_guard<std::mutex> lock(g_router_spot_slots_mu);
        router_spot_js_state_t *state = find_router_spot_slot_by_router_unsafe(router);
        if (!state)
            return;
        tsfn = state->tsfn;
        reset_router_spot_slot_unsafe(state);
    }
    if (tsfn)
        (void) napi_release_threadsafe_function(tsfn, napi_tsfn_abort);
}

static void router_spot_dispatch(void *closure,
                                 const zlink_routing_id_t *source_node_rid,
                                 const zlink_routing_id_t *source_spot_rid,
                                 uint64_t request_seq,
                                 zlink_msg_t *parts,
                                 size_t part_count,
                                 void *userdata)
{
    (void) userdata;
    router_spot_js_state_t *state =
      static_cast<router_spot_js_state_t *>(closure);
    if (!state)
        return;

    napi_threadsafe_function tsfn = NULL;
    {
        std::lock_guard<std::mutex> lock(g_router_spot_slots_mu);
        if (!state->used || !state->tsfn)
            return;
        tsfn = state->tsfn;
    }

    std::unique_ptr<spot_routed_js_payload_t> payload(
      new spot_routed_js_payload_t());
    if (source_node_rid && source_node_rid->size > 0) {
        payload->source_rid.assign(
          source_node_rid->data, source_node_rid->data + source_node_rid->size);
    }
    if (source_spot_rid && source_spot_rid->size > 0) {
        payload->spot_rid.assign(
          source_spot_rid->data, source_spot_rid->data + source_spot_rid->size);
    }
    payload->request_seq = request_seq;
    payload->parts.reserve(part_count);
    for (size_t i = 0; i < part_count; ++i) {
        const size_t size = zlink_msg_size(&parts[i]);
        const unsigned char *data = static_cast<const unsigned char *>(zlink_msg_data(&parts[i]));
        payload->parts.push_back(std::vector<unsigned char>(data, data + size));
    }

    if (napi_call_threadsafe_function(tsfn, payload.get(), napi_tsfn_nonblocking)
        != napi_ok) {
        return;
    }
    (void) payload.release();
}

spot_send_ready_js_state_t *find_spot_send_ready_slot_by_spot_unsafe(void *spot)
{
    for (size_t i = 0; i < k_spot_send_ready_slot_count; ++i) {
        if (g_spot_send_ready_slots[i].used
            && g_spot_send_ready_slots[i].spot == spot) {
            return &g_spot_send_ready_slots[i];
        }
    }
    return NULL;
}

spot_send_ready_js_state_t *find_free_spot_send_ready_slot_unsafe()
{
    for (size_t i = 0; i < k_spot_send_ready_slot_count; ++i) {
        if (!g_spot_send_ready_slots[i].used)
            return &g_spot_send_ready_slots[i];
    }
    return NULL;
}

void reset_spot_send_ready_slot_unsafe(spot_send_ready_js_state_t *state)
{
    if (!state)
        return;
    state->used = false;
    state->spot = NULL;
    state->env = NULL;
    state->tsfn = NULL;
}

void spot_send_ready_tsfn_finalize(napi_env env,
                                   void *finalize_data,
                                   void *finalize_hint)
{
    (void) env;
    (void) finalize_hint;
    spot_send_ready_js_state_t *state =
      static_cast<spot_send_ready_js_state_t *>(finalize_data);
    if (!state)
        return;
    std::lock_guard<std::mutex> lock(g_spot_send_ready_slots_mu);
    reset_spot_send_ready_slot_unsafe(state);
}

void spot_send_ready_tsfn_call_js(napi_env env,
                                  napi_value js_cb,
                                  void *context,
                                  void *data)
{
    (void) context;
    std::unique_ptr<int> payload(static_cast<int *>(data));
    if (!env || !js_cb || !payload)
        return;

    napi_value recv;
    napi_value this_arg;
    napi_get_undefined(env, &this_arg);
    (void) napi_call_function(env, this_arg, js_cb, 0, NULL, &recv);
}

void release_spot_send_ready_handler_slot(void *spot)
{
    napi_threadsafe_function tsfn = NULL;
    {
        std::lock_guard<std::mutex> lock(g_spot_send_ready_slots_mu);
        spot_send_ready_js_state_t *state =
          find_spot_send_ready_slot_by_spot_unsafe(spot);
        if (!state)
            return;
        tsfn = state->tsfn;
        reset_spot_send_ready_slot_unsafe(state);
    }
    if (tsfn)
        (void) napi_release_threadsafe_function(tsfn, napi_tsfn_abort);
}

void spot_send_ready_dispatch(void *closure, void *)
{
    spot_send_ready_js_state_t *state =
      static_cast<spot_send_ready_js_state_t *>(closure);
    if (!state)
        return;

    napi_threadsafe_function tsfn = NULL;
    {
        std::lock_guard<std::mutex> lock(g_spot_send_ready_slots_mu);
        if (!state->used || !state->tsfn)
            return;
        tsfn = state->tsfn;
    }

    std::unique_ptr<int> payload(new int(1));
    if (napi_call_threadsafe_function(
          tsfn, payload.get(), napi_tsfn_nonblocking)
        != napi_ok) {
        return;
    }
    (void) payload.release();
}

bool attach_spot_send_ready_handler(napi_env env, void *spot, napi_value handler)
{
    spot_send_ready_js_state_t *slot = NULL;
    {
        std::lock_guard<std::mutex> lock(g_spot_send_ready_slots_mu);
        if (find_spot_send_ready_slot_by_spot_unsafe(spot)) {
            napi_throw_error(env, NULL, "sendReadyHandler already attached");
            return false;
        }
        slot = find_free_spot_send_ready_slot_unsafe();
        if (!slot) {
            napi_throw_error(env, NULL, "no free sendReadyHandler slot");
            return false;
        }
    }

    napi_value resource_name;
    napi_create_string_utf8(
      env, "zlink-spot-send-ready-handler", NAPI_AUTO_LENGTH, &resource_name);
    napi_threadsafe_function tsfn = NULL;
    napi_status tsfn_status = napi_create_threadsafe_function(
      env, handler, NULL, resource_name, 0, 1, slot,
      spot_send_ready_tsfn_finalize, slot, spot_send_ready_tsfn_call_js,
      &tsfn);
    if (tsfn_status != napi_ok) {
      napi_throw_error(
        env, NULL, "sendReadyHandler failed to create callback queue");
      return false;
    }

    {
        std::lock_guard<std::mutex> lock(g_spot_send_ready_slots_mu);
        slot->used = true;
        slot->spot = spot;
        slot->env = env;
        slot->tsfn = tsfn;
    }

    int rc = zlink_send_ready_handler(spot, &spot_send_ready_dispatch, slot);
    if (rc != 0) {
        release_spot_send_ready_handler_slot(spot);
        throw_last_error(env, "sendReadyHandler failed");
        return false;
    }
    return true;
}

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

    set_property("Socket-Type", zlink_msg_gets(msg, "Socket-Type"));
    set_property("User-Id", zlink_msg_gets(msg, "User-Id"));
    set_property("Peer-Address", zlink_msg_gets(msg, "Peer-Address"));

    return props;
}

napi_value create_message_snapshot_value(napi_env env,
                                        const zlink_routing_id_t *routing_id,
                                        zlink_msg_t *msg)
{
    napi_value obj;
    napi_create_object(env, &obj);

    napi_value data;
    napi_create_buffer_copy(
      env, zlink_msg_size(msg), zlink_msg_data(msg), NULL, &data);
    zlink_config_result_t refcnt_err = ZLINK_CONFIG_OK;
    const int refcnt = zlink_msg_refcnt(msg, &refcnt_err);
    if (refcnt_err != ZLINK_CONFIG_OK)
        return throw_last_error(env, "message refcnt failed");
    napi_value ref_count;
    napi_create_int32(env, refcnt, &ref_count);
    napi_value props = create_message_properties_snapshot(env, routing_id, msg);

    napi_set_named_property(env, obj, "data", data);
    napi_set_named_property(env, obj, "refCount", ref_count);
    napi_set_named_property(env, obj, "properties", props);
    return obj;
}

bool build_spot_node_peer_filter(napi_env env,
                                 napi_value value,
                                 zlink_spot_node_peer_filter_t *out)
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
    if (napi_has_named_property(env, value, "peerEndpoint", &has_prop) == napi_ok
        && has_prop
        && napi_get_named_property(env, value, "peerEndpoint", &prop) == napi_ok) {
        std::string peer_endpoint = get_string(env, prop);
        strncpy(out->peer_endpoint, peer_endpoint.c_str(),
                sizeof(out->peer_endpoint) - 1);
    }
    if (napi_has_named_property(env, value, "source", &has_prop) == napi_ok
        && has_prop
        && napi_get_named_property(env, value, "source", &prop) == napi_ok) {
        uint32_t raw = 0;
        napi_get_value_uint32(env, prop, &raw);
        out->source = static_cast<zlink_spot_peer_source_t>(raw);
    }
    if (napi_has_named_property(env, value, "state", &has_prop) == napi_ok
        && has_prop
        && napi_get_named_property(env, value, "state", &prop) == napi_ok) {
        uint32_t raw = 0;
        napi_get_value_uint32(env, prop, &raw);
        out->state = static_cast<zlink_spot_peer_state_t>(raw);
    }
    return true;
}

bool build_spot_node_subject_filter(napi_env env,
                                    napi_value value,
                                    zlink_spot_node_subject_filter_t *out)
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
    if (napi_has_named_property(env, value, "role", &has_prop) == napi_ok
        && has_prop
        && napi_get_named_property(env, value, "role", &prop) == napi_ok) {
        uint32_t raw = 0;
        napi_get_value_uint32(env, prop, &raw);
        out->role = static_cast<zlink_spot_role_t>(raw);
    }
    if (napi_has_named_property(env, value, "subject", &has_prop) == napi_ok
        && has_prop
        && napi_get_named_property(env, value, "subject", &prop) == napi_ok) {
        std::string subject = get_string(env, prop);
        strncpy(out->subject, subject.c_str(), sizeof(out->subject) - 1);
    }
    if (napi_has_named_property(env, value, "subjectKind", &has_prop) == napi_ok
        && has_prop
        && napi_get_named_property(env, value, "subjectKind", &prop) == napi_ok) {
        uint32_t raw = 0;
        napi_get_value_uint32(env, prop, &raw);
        out->subject_kind = raw;
    }
    return true;
}

} // namespace

napi_value router_spot_send(napi_env env, napi_callback_info info)
{
    (void) info;
    napi_throw_error(env, NULL, "routerSpotSend not implemented");
    return NULL;
}

napi_value spot_send_spot(napi_env env, napi_callback_info info)
{
    (void) info;
    napi_throw_error(env, NULL, "spotSendSpot not implemented");
    return NULL;
}

napi_value spot_request_spot(napi_env env, napi_callback_info info)
{
    (void) info;
    napi_throw_error(env, NULL, "spotRequestSpot not implemented");
    return NULL;
}

napi_value spot_reply_spot(napi_env env, napi_callback_info info)
{
    (void) info;
    napi_throw_error(env, NULL, "spotReplySpot not implemented");
    return NULL;
}

napi_value spot_send_router(napi_env env, napi_callback_info info)
{
    (void) info;
    napi_throw_error(env, NULL, "spotSendRouter not implemented");
    return NULL;
}

napi_value spot_request_router(napi_env env, napi_callback_info info)
{
    (void) info;
    napi_throw_error(env, NULL, "spotRequestRouter not implemented");
    return NULL;
}

napi_value spot_reply_router(napi_env env, napi_callback_info info)
{
    (void) info;
    napi_throw_error(env, NULL, "spotReplyRouter not implemented");
    return NULL;
}

napi_value spot_routed_handler(napi_env env, napi_callback_info info)
{
    (void) info;
    napi_throw_error(env, NULL, "spotRoutedHandler not implemented");
    return NULL;
}

napi_value spot_dispatch_event_handler(napi_env env, napi_callback_info info)
{
    (void) info;
    napi_throw_error(env, NULL, "spotDispatchEventHandler not implemented");
    return NULL;
}

napi_value spot_recv_routed(napi_env env, napi_callback_info info)
{
    (void) info;
    napi_throw_error(env, NULL, "spotRecvRouted not implemented");
    return NULL;
}

napi_value router_spot_request(napi_env env, napi_callback_info info)
{
    (void) info;
    napi_throw_error(env, NULL, "routerSpotRequest not implemented");
    return NULL;
}

napi_value router_spot_reply(napi_env env, napi_callback_info info)
{
    (void) info;
    napi_throw_error(env, NULL, "routerSpotReply not implemented");
    return NULL;
}

napi_value router_spot_handler(napi_env env, napi_callback_info info)
{
    (void) info;
    napi_throw_error(env, NULL, "routerSpotHandler not implemented");
    return NULL;
}

napi_value router_spot_recv(napi_env env, napi_callback_info info)
{
    (void) info;
    napi_throw_error(env, NULL, "routerSpotRecv not implemented");
    return NULL;
}

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

napi_value spot_node_peers_query(napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *node = NULL;
    napi_get_value_external(env, argv[0], &node);

    zlink_spot_node_peer_filter_t filter;
    zlink_spot_node_peer_filter_t *filter_ptr =
      build_spot_node_peer_filter(env, argc >= 2 ? argv[1] : NULL, &filter) ? &filter
                                                                             : NULL;

    size_t count = 0;
    int rc = zlink_spot_node_peers_query(node, filter_ptr, NULL, &count);
    if (rc != 0)
        return throw_last_error(env, "spot_node_peers_query failed");
    napi_value arr;
    napi_create_array_with_length(env, count, &arr);
    if (count == 0)
        return arr;

    std::vector<zlink_spot_node_peer_entry_t> entries(count);
    rc = zlink_spot_node_peers_query(node, filter_ptr, entries.data(), &count);
    if (rc != 0)
        return throw_last_error(env, "spot_node_peers_query failed");
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
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *node = NULL;
    napi_get_value_external(env, argv[0], &node);
    zlink_spot_node_subject_filter_t filter;
    zlink_spot_node_subject_filter_t *filter_ptr =
      build_spot_node_subject_filter(env, argc >= 2 ? argv[1] : NULL, &filter) ? &filter
                                                                                : NULL;

    size_t count = 0;
    int rc = zlink_spot_node_subjects_snapshot(node, filter_ptr, NULL, &count);
    if (rc != 0)
        return throw_last_error(env, "spot_node_subjects_snapshot failed");
    napi_value arr;
    napi_create_array_with_length(env, count, &arr);
    if (count == 0)
        return arr;

    std::vector<zlink_spot_node_subject_entry_t> entries(count);
    rc = zlink_spot_node_subjects_snapshot(node, filter_ptr, entries.data(), &count);
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
    void *node = NULL;
    napi_get_value_external(env, argv[0], &node);
    void *spot = zlink_spot_new(node);
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
    release_socket_subscribe_handler_slot(spot);
    release_spot_send_ready_handler_slot(spot);
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

napi_value spot_try_publish(napi_env env, napi_callback_info info)
{
    napi_value argv[3];
    size_t argc = 3;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *spot = NULL;
    napi_get_value_external(env, argv[0], &spot);
    std::string topic = get_string(env, argv[1]);

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
            return throw_last_error(env, "tryPublish failed");
        if (len > 0)
            memcpy(zlink_msg_data(&parts[0]), data, len);
    } else {
        if (!build_msg_vector(env, argv[2], &parts))
            return NULL;
    }

    int rc = zlink_publish(spot, topic.c_str(), parts.data(), parts.size(),
                           ZLINK_DONTWAIT);
    if (rc == 0) {
        rc = ZLINK_SUBMIT_OK;
    } else {
        switch (zlink_errno()) {
        case EAGAIN:
            rc = ZLINK_SUBMIT_BACKPRESSURED;
            break;
#ifdef ENOTCONN
        case ENOTCONN:
#endif
#ifdef EHOSTUNREACH
        case EHOSTUNREACH:
#endif
#ifdef ETIMEDOUT
        case ETIMEDOUT:
#endif
            rc = ZLINK_SUBMIT_NOT_CONNECTED;
            break;
        default:
            rc = -1;
            break;
        }
    }
    if (rc < 0) {
        close_msg_vector(parts);
        return throw_last_error(env, "tryPublish failed");
    }

    napi_value out;
    napi_create_int32(env, rc, &out);
    return out;
}

napi_value spot_send_ready_handler(napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    if (argc < 2) {
        napi_throw_type_error(
          env, NULL, "sendReadyHandler requires (spot, handler)");
        return NULL;
    }
    void *spot = NULL;
    napi_get_value_external(env, argv[0], &spot);
    napi_valuetype handler_type = napi_undefined;
    napi_typeof(env, argv[1], &handler_type);
    if (handler_type != napi_function) {
        napi_throw_type_error(
          env, NULL, "sendReadyHandler handler must be a function");
        return NULL;
    }
    if (!attach_spot_send_ready_handler(env, spot, argv[1]))
        return NULL;
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
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *spot = NULL;
    napi_get_value_external(env, argv[0], &spot);
    std::vector<char> topic(256, '\0');
    zlink_routing_id_t routing_id;
    zlink_msg_t *parts = NULL;
    size_t count = 0;
    size_t topic_len = topic.size();

    for (;;) {
        memset(&routing_id, 0, sizeof(routing_id));
        int rc = zlink_subscribe(
          spot, &routing_id, &parts, &count, topic.data(), &topic_len, 0);
        if (rc == 0) {
            napi_value arr;
            napi_create_array_with_length(env, count, &arr);
            for (size_t i = 0; i < count; ++i) {
                napi_value part =
                  create_message_snapshot_value(env, &routing_id, &parts[i]);
                napi_set_element(env, arr, static_cast<uint32_t>(i), part);
            }
            zlink_multipart_close(parts, count);

            napi_value obj;
            napi_create_object(env, &obj);
            napi_value topic_value;
            napi_create_string_utf8(env, topic.data(), topic_len, &topic_value);
            napi_set_named_property(env, obj, "topic", topic_value);
            napi_set_named_property(env, obj, "parts", arr);
            napi_value rid = create_routing_id_value(env, routing_id);
            napi_set_named_property(env, obj, "routingId", rid);
            return obj;
        }
        if (zlink_errno() != EMSGSIZE)
            return throw_last_error(env, "spot_recv failed");
        topic.assign(topic_len > 0 ? topic_len : 1, '\0');
    }
}

napi_value spot_try_recv(napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *spot = NULL;
    napi_get_value_external(env, argv[0], &spot);

    std::vector<char> topic(256, '\0');
    zlink_routing_id_t routing_id;
    zlink_msg_t *parts = NULL;
    size_t count = 0;
    size_t topic_len = topic.size();

    for (;;) {
        memset(&routing_id, 0, sizeof(routing_id));
        int rc = zlink_subscribe(
          spot, &routing_id, &parts, &count, topic.data(), &topic_len, ZLINK_DONTWAIT);
        if (rc == 0) {
            napi_value arr;
            napi_create_array_with_length(env, count, &arr);
            for (size_t i = 0; i < count; ++i) {
                napi_value part =
                  create_message_snapshot_value(env, &routing_id, &parts[i]);
                napi_set_element(env, arr, static_cast<uint32_t>(i), part);
            }
            zlink_multipart_close(parts, count);

            napi_value obj;
            napi_create_object(env, &obj);
            napi_value topic_value;
            napi_create_string_utf8(env, topic.data(), topic_len, &topic_value);
            napi_set_named_property(env, obj, "topic", topic_value);
            napi_set_named_property(env, obj, "parts", arr);
            napi_value rid = create_routing_id_value(env, routing_id);
            napi_set_named_property(env, obj, "routingId", rid);
            return obj;
        }
        const int err = zlink_errno();
        if (err == EAGAIN) {
            napi_value none;
            napi_get_null(env, &none);
            return none;
        }
        if (err != EMSGSIZE)
            return throw_last_error(env, "spot_try_recv failed");
        topic.assign(topic_len > 0 ? topic_len : 1, '\0');
    }
}

napi_value spot_subscribe_handler(napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    if (argc < 2) {
        napi_throw_type_error(
          env, NULL, "subscribeHandler requires (spot, handler)");
        return NULL;
    }
    void *spot = NULL;
    napi_get_value_external(env, argv[0], &spot);
    napi_valuetype handler_type = napi_undefined;
    napi_typeof(env, argv[1], &handler_type);
    if (handler_type != napi_function) {
        napi_throw_type_error(
          env, NULL, "subscribeHandler handler must be a function");
        return NULL;
    }
    if (!attach_socket_subscribe_handler(env, spot, argv[1]))
        return NULL;
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}
