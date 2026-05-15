/* SPDX-License-Identifier: MPL-2.0 */

#include "addon_spot_api.h"
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <errno.h>

namespace {

static napi_value create_routing_id_value(napi_env env, const zlink_routing_id_t &rid);
static napi_value unsupported_spot_node(napi_env env, const char *method);
static napi_value create_message_snapshot_value(napi_env env,
                                                const zlink_routing_id_t *routing_id,
                                                zlink_msg_t *msg);
static napi_value create_buffer_copy_or_empty(napi_env env,
                                              const void *data,
                                              size_t len);
static napi_value create_message_data_buffer(napi_env env, zlink_msg_t *msg);
static void set_uint32_property(napi_env env,
                                napi_value obj,
                                const char *name,
                                uint32_t value);
static void set_string_property(napi_env env,
                                napi_value obj,
                                const char *name,
                                const char *value);
static napi_value create_monitor_snapshot_value(
  napi_env env, const zlink_monitor_snapshot_t &snapshot);
static bool build_spot_node_socket_snapshot_filter(
  napi_env env, napi_value value,
  zlink_spot_node_socket_snapshot_filter_t *out);
static napi_value create_actor_ref_value(napi_env env,
                                         const zlink_actor_ref_t &actor);
static bool parse_actor_ref_value(napi_env env,
                                  napi_value value,
                                  zlink_actor_ref_t *out);
static napi_value create_actor_recv_info_value(
  napi_env env, const zlink_actor_recv_info_t &info);
static napi_value create_actor_join_info_value(
  napi_env env, const zlink_actor_join_info_t &info);
static bool parse_actor_join_info_value(napi_env env,
                                        napi_value value,
                                        zlink_actor_join_info_t *out);
static napi_value create_actor_part_value(
  napi_env env,
  const zlink_actor_recv_info_t &info,
  const std::vector<unsigned char> &part,
  int more);

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

static std::string get_optional_string_property(napi_env env,
                                                napi_value obj,
                                                const char *name)
{
    napi_value prop;
    bool has_prop = false;
    if (napi_has_named_property(env, obj, name, &has_prop) != napi_ok
        || !has_prop
        || napi_get_named_property(env, obj, name, &prop) != napi_ok) {
        return std::string();
    }
    return get_string(env, prop);
}

static bool get_optional_routing_id_property(napi_env env,
                                             napi_value obj,
                                             const char *name,
                                             zlink_routing_id_t *out)
{
    memset(out, 0, sizeof(*out));
    napi_value prop;
    bool has_prop = false;
    if (napi_has_named_property(env, obj, name, &has_prop) != napi_ok
        || !has_prop
        || napi_get_named_property(env, obj, name, &prop) != napi_ok) {
        return true;
    }
    napi_valuetype type = napi_undefined;
    napi_typeof(env, prop, &type);
    if (type == napi_null || type == napi_undefined)
        return true;
    return parse_routing_id_value(env, prop, out);
}

static bool parse_actor_ref_value(napi_env env,
                                  napi_value value,
                                  zlink_actor_ref_t *out)
{
    memset(out, 0, sizeof(*out));
    napi_valuetype type = napi_undefined;
    if (napi_typeof(env, value, &type) != napi_ok || type != napi_object) {
        napi_throw_type_error(env, NULL, "actorRef must be an object");
        return false;
    }
    if (!get_optional_routing_id_property(env, value, "nodeRid", &out->node_rid)
        || (out->node_rid.size == 0
            && !get_optional_routing_id_property(env, value, "node_rid",
                                                 &out->node_rid))) {
        return false;
    }
    if (out->node_rid.size == 0) {
        napi_throw_type_error(env, NULL, "actorRef.nodeRid must be a RoutingId");
        return false;
    }
    std::string actor_id = get_optional_string_property(env, value, "actorId");
    if (actor_id.empty())
        actor_id = get_optional_string_property(env, value, "actor_id");
    if (actor_id.empty() || actor_id.size() >= sizeof(out->actor_id)) {
        napi_throw_range_error(env, NULL, "actorId must be 1..255 bytes");
        return false;
    }
    memcpy(out->actor_id, actor_id.c_str(), actor_id.size());
    napi_value generation_value;
    bool has_generation = false;
    if (napi_has_named_property(env, value, "generation", &has_generation)
        == napi_ok
        && has_generation
        && napi_get_named_property(env, value, "generation", &generation_value)
             == napi_ok) {
        bool lossless = false;
        napi_get_value_bigint_uint64(env, generation_value, &out->generation,
                                     &lossless);
    }
    return true;
}

static napi_value create_actor_ref_value(napi_env env,
                                         const zlink_actor_ref_t &actor)
{
    napi_value obj;
    napi_create_object(env, &obj);
    napi_value node_rid = create_routing_id_value(env, actor.node_rid);
    napi_set_named_property(env, obj, "nodeRid", node_rid);
    set_string_property(env, obj, "actorId", actor.actor_id);
    napi_value generation;
    napi_create_bigint_uint64(env, actor.generation, &generation);
    napi_set_named_property(env, obj, "generation", generation);
    return obj;
}

static napi_value create_actor_recv_info_value(
  napi_env env, const zlink_actor_recv_info_t &info)
{
    napi_value obj;
    napi_create_object(env, &obj);
    napi_set_named_property(env, obj, "actor",
                            create_actor_ref_value(env, info.actor));
    napi_set_named_property(env, obj, "sourceNodeRid",
                            create_routing_id_value(env, info.source_node_rid));
    napi_set_named_property(
      env, obj, "sourceSessionRid",
      create_routing_id_value(env, info.source_session_rid));
    set_uint32_property(env, obj, "flags", info.flags);
    return obj;
}

static napi_value create_actor_join_info_value(
  napi_env env, const zlink_actor_join_info_t &info)
{
    napi_value obj;
    napi_create_object(env, &obj);
    napi_set_named_property(env, obj, "sourceActor",
                            create_actor_ref_value(env, info.source_actor));
    napi_set_named_property(env, obj, "targetActor",
                            create_actor_ref_value(env, info.target_actor));
    napi_set_named_property(env, obj, "actor",
                            create_actor_ref_value(env, info.source_actor));
    napi_set_named_property(env, obj, "sourceNodeRid",
                            create_routing_id_value(env, info.source_node_rid));
    napi_set_named_property(env, obj, "sourceSpotRid",
                            create_routing_id_value(env, info.source_spot_rid));
    napi_set_named_property(env, obj, "targetNodeRid",
                            create_routing_id_value(env, info.target_node_rid));
    napi_set_named_property(env, obj, "targetSpotRid",
                            create_routing_id_value(env, info.target_spot_rid));
    napi_value join_epoch;
    napi_create_bigint_uint64(env, info.join_epoch, &join_epoch);
    napi_set_named_property(env, obj, "joinEpoch", join_epoch);
    napi_value request;
    napi_create_bigint_uint64(
      env, static_cast<uint64_t>(reinterpret_cast<uintptr_t>(info.request)),
      &request);
    napi_set_named_property(env, obj, "requestHandle", request);
    set_uint32_property(env, obj, "flags", info.flags);
    return obj;
}

static bool parse_actor_join_info_value(napi_env env,
                                        napi_value value,
                                        zlink_actor_join_info_t *out)
{
    memset(out, 0, sizeof(*out));
    napi_valuetype type = napi_undefined;
    if (napi_typeof(env, value, &type) != napi_ok || type != napi_object) {
        napi_throw_type_error(env, NULL, "join info must be an object");
        return false;
    }
    napi_value actor_value;
    if (napi_get_named_property(env, value, "sourceActor", &actor_value) == napi_ok) {
        if (!parse_actor_ref_value(env, actor_value, &out->source_actor))
            return false;
    } else if (napi_get_named_property(env, value, "actor", &actor_value) != napi_ok
               || !parse_actor_ref_value(env, actor_value, &out->source_actor)) {
        return false;
    }
    if (napi_get_named_property(env, value, "targetActor", &actor_value) == napi_ok) {
        if (!parse_actor_ref_value(env, actor_value, &out->target_actor))
            return false;
    } else {
        out->target_actor = out->source_actor;
    }
    if (!get_optional_routing_id_property(env, value, "sourceNodeRid",
                                          &out->source_node_rid)) {
        return false;
    }
    if (!get_optional_routing_id_property(env, value, "sourceSpotRid",
                                          &out->source_spot_rid)) {
        return false;
    }
    if (!get_optional_routing_id_property(env, value, "targetNodeRid",
                                          &out->target_node_rid)) {
        return false;
    }
    if (!get_optional_routing_id_property(env, value, "targetSpotRid",
                                          &out->target_spot_rid)) {
        return false;
    }
    napi_value join_epoch_value;
    bool has_join_epoch = false;
    if (napi_has_named_property(env, value, "joinEpoch", &has_join_epoch)
        == napi_ok
        && has_join_epoch
        && napi_get_named_property(env, value, "joinEpoch", &join_epoch_value)
             == napi_ok) {
        bool lossless = false;
        napi_get_value_bigint_uint64(env, join_epoch_value, &out->join_epoch,
                                     &lossless);
    }
    napi_value request_value;
    bool has_request = false;
    if (napi_has_named_property(env, value, "requestHandle", &has_request)
        != napi_ok
        || !has_request
        || napi_get_named_property(env, value, "requestHandle", &request_value)
             != napi_ok) {
        napi_throw_type_error(env, NULL, "join info requestHandle is required");
        return false;
    }
    uint64_t request_raw = 0;
    bool lossless = false;
    napi_get_value_bigint_uint64(env, request_value, &request_raw, &lossless);
    out->request = reinterpret_cast<void *>(static_cast<uintptr_t>(request_raw));
    napi_value flags_value;
    bool has_flags = false;
    if (napi_has_named_property(env, value, "flags", &has_flags) == napi_ok
        && has_flags
        && napi_get_named_property(env, value, "flags", &flags_value) == napi_ok) {
        napi_get_value_uint32(env, flags_value, &out->flags);
    }
    return true;
}

static napi_value create_actor_part_value(
  napi_env env,
  const zlink_actor_recv_info_t &info,
  const std::vector<unsigned char> &part,
  int more)
{
    napi_value obj;
    napi_create_object(env, &obj);
    napi_set_named_property(env, obj, "info",
                            create_actor_recv_info_value(env, info));
    napi_value msg = create_buffer_copy_or_empty(
      env, part.empty() ? NULL : part.data(), part.size());
    napi_set_named_property(env, obj, "message", msg);
    napi_value more_value;
    napi_get_boolean(env, more != ZLINK_PART_FINAL, &more_value);
    napi_set_named_property(env, obj, "more", more_value);
    return obj;
}

static napi_value create_actor_route_value(napi_env env,
                                           const zlink_actor_route_t &route)
{
    napi_value obj;
    napi_create_object(env, &obj);
    napi_set_named_property(env, obj, "actor",
                            create_actor_ref_value(env, route.actor));
    napi_value joined;
    napi_get_boolean(env, route.joined != 0, &joined);
    napi_set_named_property(env, obj, "joined", joined);
    napi_set_named_property(env, obj, "joinedSpotRid",
                            create_routing_id_value(env, route.joined_spot_rid));
    return obj;
}

static napi_value create_spot_node_spot_entry_value(
  napi_env env, const zlink_spot_node_spot_entry_t &entry)
{
    napi_value obj;
    napi_create_object(env, &obj);
    napi_set_named_property(env, obj, "spotRid",
                            create_routing_id_value(env, entry.spot_rid));
    napi_value dispatch_attached;
    napi_get_boolean(env, entry.dispatch_handler_attached != 0,
                     &dispatch_attached);
    napi_set_named_property(env, obj, "dispatchHandlerAttached",
                            dispatch_attached);
    set_uint32_property(env, obj, "joinedActorCount",
                        entry.joined_actor_count);
    set_uint32_property(env, obj, "pendingActorJoinCount",
                        entry.pending_actor_join_count);
    napi_value route_synced;
    napi_get_boolean(env, entry.route_synced != 0, &route_synced);
    napi_set_named_property(env, obj, "routeSynced", route_synced);
    napi_value changed;
    napi_create_bigint_uint64(env, entry.last_changed_ms, &changed);
    napi_set_named_property(env, obj, "lastChangedMs", changed);
    return obj;
}

static napi_value create_spot_node_actor_entry_value(
  napi_env env, const zlink_spot_node_actor_entry_t &entry)
{
    napi_value obj;
    napi_create_object(env, &obj);
    napi_set_named_property(env, obj, "actor",
                            create_actor_ref_value(env, entry.actor));
    napi_value joined;
    napi_get_boolean(env, entry.joined != 0, &joined);
    napi_set_named_property(env, obj, "joined", joined);
    napi_set_named_property(env, obj, "joinedSpotRid",
                            create_routing_id_value(env, entry.joined_spot_rid));
    napi_value route_synced;
    napi_get_boolean(env, entry.route_synced != 0, &route_synced);
    napi_set_named_property(env, obj, "routeSynced", route_synced);
    set_uint32_property(env, obj, "pendingMessageCount",
                        entry.pending_message_count);
    napi_value changed;
    napi_create_bigint_uint64(env, entry.last_changed_ms, &changed);
    napi_set_named_property(env, obj, "lastChangedMs", changed);
    return obj;
}

static const size_t k_spot_routed_slot_count = 256;
static const size_t k_router_spot_slot_count = 256;
static const size_t k_spot_dispatch_event_slot_count = 256;

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
    uint32_t subject_kind;
    uint64_t subject_handle;
    std::vector<zlink_actor_recv_info_t> actor_infos;
    std::vector<std::vector<unsigned char> > actor_parts;
    std::vector<int> actor_more;
};

struct spot_dispatch_event_js_state_t
{
    spot_dispatch_event_js_state_t ()
      : used (false), spot (NULL), node (NULL), env (NULL), tsfn (NULL) {}

    bool used;
    void *spot;
    void *node;
    napi_env env;
    napi_threadsafe_function tsfn;
};

struct request_result_js_payload_t
{
    request_result_js_payload_t ()
      : errnum (0), has_actor_join (false), has_actor_lookup (false),
        join_epoch (0), flags (0)
    {
        memset(&actor, 0, sizeof(actor));
        memset(&joined_spot_rid, 0, sizeof(joined_spot_rid));
    }
    int errnum;
    std::vector<std::vector<unsigned char> > parts;
    // Optional rich-result payload for ActorJoin/ActorLookup callbacks.
    bool has_actor_join;
    bool has_actor_lookup;
    zlink_actor_ref_t actor;
    zlink_routing_id_t joined_spot_rid;
    uint64_t join_epoch;
    uint32_t flags;
};

struct request_js_state_t
{
    request_js_state_t () : env (NULL), tsfn (NULL) {}

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

static void close_recv_parts(zlink_msg_t *parts, size_t part_count)
{
    if (!parts)
        return;
    zlink_multipart_close(parts, part_count);
}

static int classify_try_send_errno()
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

static void copy_routing_id(zlink_routing_id_t *out,
                            const zlink_routing_id_t *in)
{
    if (!out)
        return;
    if (in)
        memcpy(out, in, sizeof(*out));
    else
        memset(out, 0, sizeof(*out));
}

static bool append_msg_move(std::vector<zlink_msg_t> *parts, zlink_msg_t *part)
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

static int collect_recv_parts(void *socket,
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
          socket, &source_rid, &next_part, &more, ZLINK_RECV_FLAGS_DONTWAIT);
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

static int router_send_spot_parts(void *router,
                                  const zlink_routing_id_t *dest_node_rid,
                                  const zlink_routing_id_t *dest_spot_rid,
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
        int rc = zlink_router_send_spot_part(
          router, dest_node_rid, dest_spot_rid, &parts[i], flags, part_flag);
        if (rc != ZLINK_SUBMIT_OK) {
            for (size_t j = i + 1u; j < part_count; ++j)
                zlink_msg_close(&parts[j]);
            return rc;
        }
    }

    return ZLINK_SUBMIT_OK;
}

static int spot_reply_spot_parts(void *spot,
                                 const zlink_routing_id_t *dest_node_rid,
                                 const zlink_routing_id_t *dest_spot_rid,
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
        int rc = zlink_spot_reply_spot_part(
          spot, dest_node_rid, dest_spot_rid, request_seq, &parts[i], part_flag);
        if (rc != ZLINK_SUBMIT_OK) {
            for (size_t j = i + 1u; j < part_count; ++j)
                zlink_msg_close(&parts[j]);
            return rc;
        }
    }

    return ZLINK_SUBMIT_OK;
}

static int spot_send_spot_parts(void *spot,
                                const zlink_routing_id_t *dest_node_rid,
                                const zlink_routing_id_t *dest_spot_rid,
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
        int rc = zlink_spot_send_spot_part(
          spot, dest_node_rid, dest_spot_rid, &parts[i], flags, part_flag);
        if (rc != ZLINK_SUBMIT_OK) {
            for (size_t j = i + 1u; j < part_count; ++j)
                zlink_msg_close(&parts[j]);
            return rc;
        }
    }

    return ZLINK_SUBMIT_OK;
}

static int spot_reply_router_parts(void *spot,
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
        int rc = zlink_spot_reply_router_part(
          spot, peer_rid, request_seq, &parts[i], part_flag);
        if (rc != ZLINK_SUBMIT_OK) {
            for (size_t j = i + 1u; j < part_count; ++j)
                zlink_msg_close(&parts[j]);
            return rc;
        }
    }

    return ZLINK_SUBMIT_OK;
}

static int spot_recv_parts(void *spot,
                           zlink_routing_id_t *source_rid,
                           zlink_routing_id_t *spot_rid,
                           uint64_t *request_seq,
                           std::vector<zlink_msg_t> *parts,
                           zlink_recv_flags_t flags)
{
    const zlink_routing_id_t *source_rid_ptr = NULL;
    const zlink_routing_id_t *spot_rid_ptr = NULL;
    zlink_msg_t first_part;
    if (zlink_msg_init(&first_part) != 0)
        return ZLINK_RECV_INTERNAL_ERROR;
    zlink_part_flag_t has_more = ZLINK_PART_FINAL;

    copy_routing_id(source_rid, NULL);
    copy_routing_id(spot_rid, NULL);
    if (request_seq)
        *request_seq = 0;
    if (parts)
        parts->clear();

    int rc = zlink_spot_recv_part(
      spot,
      &source_rid_ptr,
      &spot_rid_ptr,
      request_seq,
      &first_part,
      &has_more,
      flags);
    if (rc != ZLINK_RECV_OK) {
        zlink_msg_close(&first_part);
        return rc;
    }

    copy_routing_id(source_rid, source_rid_ptr);
    copy_routing_id(spot_rid, spot_rid_ptr);
    return collect_recv_parts(spot, &first_part, has_more, parts);
}

static int router_request_spot_parts(void *router,
                                     const zlink_routing_id_t *dest_node_rid,
                                     const zlink_routing_id_t *dest_spot_rid,
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
        int rc = zlink_router_request_spot_part(
          router,
          dest_node_rid,
          dest_spot_rid,
          &parts[i],
          is_final ? handler : NULL,
          is_final ? userdata : NULL,
          flags,
          part_flag,
          is_final ? timeout_ms : 0u);
        if (rc != ZLINK_SUBMIT_OK) {
            for (size_t j = i + 1u; j < part_count; ++j)
                zlink_msg_close(&parts[j]);
            return rc;
        }
    }

    return ZLINK_SUBMIT_OK;
}

static int router_reply_spot_parts(void *router,
                                   const zlink_routing_id_t *dest_node_rid,
                                   const zlink_routing_id_t *dest_spot_rid,
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
        int rc = zlink_router_reply_spot_part(
          router, dest_node_rid, dest_spot_rid, request_seq, &parts[i], part_flag);
        if (rc != ZLINK_SUBMIT_OK) {
            for (size_t j = i + 1u; j < part_count; ++j)
                zlink_msg_close(&parts[j]);
            return rc;
        }
    }

    return ZLINK_SUBMIT_OK;
}

static int spot_publish_parts(void *spot,
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
        int rc = zlink_spot_publish_part(
          spot, topic, &parts[i], flags, part_flag);
        if (rc != ZLINK_SUBMIT_OK) {
            for (size_t j = i + 1u; j < part_count; ++j)
                zlink_msg_close(&parts[j]);
            return rc;
        }
    }

    return ZLINK_SUBMIT_OK;
}

static int spot_send_channel_parts(void *spot,
                                   const char *channel_name,
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
        int rc = zlink_spot_send_channel_part(
          spot, channel_name, &parts[i], flags, part_flag);
        if (rc != ZLINK_SUBMIT_OK) {
            for (size_t j = i + 1u; j < part_count; ++j)
                zlink_msg_close(&parts[j]);
            return rc;
        }
    }

    return ZLINK_SUBMIT_OK;
}

static int spot_request_channel_parts(void *spot,
                                      const char *channel_name,
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
        int rc = zlink_spot_request_channel_part(
          spot,
          channel_name,
          &parts[i],
          is_final ? handler : NULL,
          is_final ? userdata : NULL,
          flags,
          part_flag,
          is_final ? timeout_ms : 0u);
        if (rc != ZLINK_SUBMIT_OK) {
            for (size_t j = i + 1u; j < part_count; ++j)
                zlink_msg_close(&parts[j]);
            return rc;
        }
    }

    return ZLINK_SUBMIT_OK;
}

static int spot_request_spot_parts(void *spot,
                                   const zlink_routing_id_t *dest_node_rid,
                                   const zlink_routing_id_t *dest_spot_rid,
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
        int rc = zlink_spot_request_spot_part(
          spot, dest_node_rid, dest_spot_rid, &parts[i],
          is_final ? handler : NULL, is_final ? userdata : NULL, flags,
          part_flag, is_final ? timeout_ms : 0u);
        if (rc != ZLINK_SUBMIT_OK) {
            for (size_t j = i + 1u; j < part_count; ++j)
                zlink_msg_close(&parts[j]);
            return rc;
        }
    }

    return ZLINK_SUBMIT_OK;
}

static int spot_request_router_parts(void *spot,
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
        int rc = zlink_spot_request_router_part(
          spot, peer_rid, &parts[i], is_final ? handler : NULL,
          is_final ? userdata : NULL, flags, part_flag,
          is_final ? timeout_ms : 0u);
        if (rc != ZLINK_SUBMIT_OK) {
            for (size_t j = i + 1u; j < part_count; ++j)
                zlink_msg_close(&parts[j]);
            return rc;
        }
    }

    return ZLINK_SUBMIT_OK;
}

static int spot_subscribe_recv_parts(void *spot,
                                     zlink_routing_id_t *source_rid,
                                     char *topic,
                                     size_t topic_capacity,
                                     size_t *topic_len,
                                     std::vector<zlink_msg_t> *parts,
                                     zlink_recv_flags_t flags)
{
    const zlink_routing_id_t *source_rid_ptr = NULL;
    zlink_msg_t first_part;
    if (zlink_msg_init(&first_part) != 0)
        return ZLINK_RECV_INTERNAL_ERROR;
    zlink_part_flag_t has_more = ZLINK_PART_FINAL;

    copy_routing_id(source_rid, NULL);
    if (parts)
        parts->clear();

    int rc = zlink_spot_subscribe_part(
      spot,
      &source_rid_ptr,
      topic,
      topic_capacity,
      topic_len,
      &first_part,
      &has_more,
      flags);
    if (rc != ZLINK_RECV_OK) {
        zlink_msg_close(&first_part);
        return rc;
    }

    copy_routing_id(source_rid, source_rid_ptr);
    return collect_recv_parts(spot, &first_part, has_more, parts);
}

static void copy_recv_parts_to_vectors(
  zlink_msg_t *parts,
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
        const size_t part_size = zlink_msg_size(&parts[i]);
        std::vector<unsigned char> copy;
        if (part_data && part_size > 0)
            copy.assign(part_data, part_data + part_size);
        out->push_back(copy);
    }
}

static napi_value create_buffer_copy_or_empty(napi_env env,
                                              const void *data,
                                              size_t len)
{
    napi_value out;
    napi_create_buffer_copy(env, len, len == 0 ? NULL : data, NULL, &out);
    return out;
}

static void finalize_external_msg_buffer(napi_env env, void *data, void *hint)
{
    (void) env;
    (void) data;
    zlink_msg_t *msg = static_cast<zlink_msg_t *>(hint);
    if (!msg)
        return;
    zlink_msg_close(msg);
    delete msg;
}

static napi_value create_message_data_buffer(napi_env env, zlink_msg_t *msg)
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

static void spot_routed_dispatch(const zlink_routing_id_t *source_rid,
                                 const zlink_routing_id_t *spot_rid,
                                 uint64_t request_seq,
                                 zlink_msg_t *parts,
                                 size_t part_count,
                                 void *userdata)
{
    spot_routed_js_state_t *state =
      static_cast<spot_routed_js_state_t *>(userdata);
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
        napi_value part =
          create_message_snapshot_value(env, source_rid, &parts[i]);
        napi_set_element(env, parts_array, static_cast<uint32_t>(i), part);
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
    state->node = NULL;
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
    napi_value info;
    napi_create_object(env, &info);
    set_uint32_property(env, info, "event",
                        static_cast<uint32_t>(payload->event));
    set_uint32_property(env, info, "subjectKind", payload->subject_kind);
    napi_value subject_handle;
    napi_create_bigint_uint64(env, payload->subject_handle, &subject_handle);
    napi_set_named_property(env, info, "subjectHandle", subject_handle);
    napi_value actor_parts;
    napi_create_array_with_length(env, payload->actor_parts.size(), &actor_parts);
    for (size_t i = 0; i < payload->actor_parts.size(); ++i) {
        napi_value part = create_actor_part_value(env,
                                                  payload->actor_infos[i],
                                                  payload->actor_parts[i],
                                                  payload->actor_more[i]);
        napi_set_element(env, actor_parts, static_cast<uint32_t>(i), part);
    }
    napi_set_named_property(env, info, "actorParts", actor_parts);
    argv[0] = info;
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

static void spot_dispatch_event_dispatch(void *spot_,
                                         const zlink_spot_dispatch_info_t *info,
                                         void *userdata)
{
    (void) spot_;
    spot_dispatch_event_js_state_t *state =
      static_cast<spot_dispatch_event_js_state_t *>(userdata);
    if (!state || !info)
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
    payload->event = static_cast<int>(info->event);
    payload->subject_kind = static_cast<uint32_t>(info->subject_kind);
    payload->subject_handle =
      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(info->subject));
        if (info->event == ZLINK_SPOT_DISPATCH_EVENT_ACTOR_READABLE
        && info->subject_kind == ZLINK_SPOT_DISPATCH_SUBJECT_ACTOR
        && info->subject
        && state->node) {
        for (;;) {
            zlink_actor_recv_info_t recv_info;
            zlink_msg_t part;
            zlink_part_flag_t more = ZLINK_PART_FINAL;
            if (zlink_msg_init(&part) != 0)
                break;
            int rc = zlink_spot_node_actor_recv_part(
              state->node,
              static_cast<const zlink_actor_ref_t *>(info->subject),
              &recv_info,
              &part,
              &more,
              ZLINK_RECV_FLAGS_DONTWAIT);
            if (rc != ZLINK_RECV_OK) {
                zlink_msg_close(&part);
                break;
            }
            const unsigned char *data =
              static_cast<const unsigned char *>(zlink_msg_data(&part));
            const size_t size = zlink_msg_size(&part);
            payload->actor_infos.push_back(recv_info);
            std::vector<unsigned char> copy;
            if (data && size > 0)
                copy.assign(data, data + size);
            payload->actor_parts.push_back(copy);
            payload->actor_more.push_back(static_cast<int>(more));
            zlink_msg_close(&part);
            if (more == ZLINK_PART_FINAL)
                break;
        }
    }
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

static napi_value create_monitor_event_value(napi_env env,
                                             const zlink_monitor_event_t &event)
{
    napi_value obj;
    napi_create_object(env, &obj);

    napi_value event_value;
    napi_create_uint32(env, static_cast<uint32_t>(event.event), &event_value);
    napi_set_named_property(env, obj, "event", event_value);

    napi_value detail_value;
    napi_create_uint32(env, static_cast<uint32_t>(event.value), &detail_value);
    napi_set_named_property(env, obj, "value", detail_value);

    napi_value routing_id = create_routing_id_value(env, event.routing_id);
    napi_set_named_property(env, obj, "routingId", routing_id);

    set_string_property(env, obj, "localAddr", event.local_addr);
    set_string_property(env, obj, "remoteAddr", event.remote_addr);
    return obj;
}

static void request_tsfn_finalize(napi_env env,
                                  void *finalize_data,
                                  void *finalize_hint)
{
    (void) env;
    (void) finalize_hint;
    request_js_state_t *state = static_cast<request_js_state_t *>(finalize_data);
    delete state;
}

static void request_tsfn_call_js(napi_env env,
                                 napi_value js_cb,
                                 void *context,
                                 void *data)
{
    (void) context;
    std::unique_ptr<request_result_js_payload_t> payload(
      static_cast<request_result_js_payload_t *>(data));
    if (!env || !js_cb || !payload)
        return;

    napi_value argv[2];
    if (payload->has_actor_join) {
        // ActorJoinResult { result, actor, joinedSpotRid, joinEpoch, flags }
        napi_value result_obj;
        napi_create_object(env, &result_obj);
        napi_value result_value;
        napi_create_int32(env, payload->errnum, &result_value);
        napi_set_named_property(env, result_obj, "result", result_value);
        napi_set_named_property(env, result_obj, "actor",
                                create_actor_ref_value(env, payload->actor));
        napi_set_named_property(env, result_obj, "joinedSpotRid",
                                create_routing_id_value(env, payload->joined_spot_rid));
        napi_value join_epoch;
        napi_create_bigint_uint64(env, payload->join_epoch, &join_epoch);
        napi_set_named_property(env, result_obj, "joinEpoch", join_epoch);
        napi_value flags_value;
        napi_create_uint32(env, payload->flags, &flags_value);
        napi_set_named_property(env, result_obj, "flags", flags_value);
        argv[0] = result_obj;
        if (payload->errnum != 0) {
            napi_get_null(env, &argv[1]);
        } else {
            napi_value parts_array;
            napi_create_array_with_length(env, payload->parts.size(), &parts_array);
            for (size_t i = 0; i < payload->parts.size(); ++i) {
                const std::vector<unsigned char> &part = payload->parts[i];
                napi_value part_buf = create_buffer_copy_or_empty(
                  env, part.empty() ? NULL : part.data(), part.size());
                napi_set_element(env, parts_array, static_cast<uint32_t>(i), part_buf);
            }
            argv[1] = parts_array;
        }
    } else if (payload->has_actor_lookup) {
        // ActorLookupResult { result, actor, flags }; callback takes one arg.
        napi_value result_obj;
        napi_create_object(env, &result_obj);
        napi_value result_value;
        napi_create_int32(env, payload->errnum, &result_value);
        napi_set_named_property(env, result_obj, "result", result_value);
        napi_set_named_property(env, result_obj, "actor",
                                create_actor_ref_value(env, payload->actor));
        napi_value flags_value;
        napi_create_uint32(env, payload->flags, &flags_value);
        napi_set_named_property(env, result_obj, "flags", flags_value);
        argv[0] = result_obj;
        napi_value recv;
        napi_value this_arg;
        napi_get_undefined(env, &this_arg);
        (void) napi_call_function(env, this_arg, js_cb, 1, argv, &recv);
        return;
    } else {
        napi_create_int32(env, payload->errnum, &argv[0]);
        if (payload->errnum != 0) {
            napi_get_null(env, &argv[1]);
        } else {
            napi_value parts_array;
            napi_create_array_with_length(env, payload->parts.size(), &parts_array);
            for (size_t i = 0; i < payload->parts.size(); ++i) {
                const std::vector<unsigned char> &part = payload->parts[i];
                napi_value part_buf = create_buffer_copy_or_empty(
                  env, part.empty() ? NULL : part.data(), part.size());
                napi_set_element(env, parts_array, static_cast<uint32_t>(i), part_buf);
            }
            argv[1] = parts_array;
        }
    }

    napi_value recv;
    napi_value this_arg;
    napi_get_undefined(env, &this_arg);
    (void) napi_call_function(env, this_arg, js_cb, 2, argv, &recv);
}

static request_js_state_t *create_request_js_state(napi_env env,
                                                   napi_value handler)
{
    request_js_state_t *state = new request_js_state_t();
    state->env = env;

    napi_value resource_name;
    napi_create_string_utf8(
      env, "zlink-spot-request-callback", NAPI_AUTO_LENGTH, &resource_name);
    napi_threadsafe_function tsfn = NULL;
    napi_status status = napi_create_threadsafe_function(
      env, handler, NULL, resource_name, 0, 1, state, request_tsfn_finalize,
      state, request_tsfn_call_js, &tsfn);
    if (status != napi_ok) {
        delete state;
        napi_throw_error(env, NULL, "spot request callback setup failed");
        return NULL;
    }
    state->tsfn = tsfn;
    return state;
}

static void request_reply_callback_trampoline(zlink_request_result_t errnum_,
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
    if (errnum_ == 0)
        copy_recv_parts_to_vectors(parts_, part_count_, &payload->parts);
    close_recv_parts(parts_, part_count_);

    if (napi_call_threadsafe_function(
          state->tsfn, payload.get(), napi_tsfn_nonblocking)
        == napi_ok) {
        payload.release();
    }
    (void) napi_release_threadsafe_function(state->tsfn, napi_tsfn_release);
    state->tsfn = NULL;
}

static void actor_join_callback_trampoline(
  const zlink_actor_join_result_t *result_,
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
    payload->has_actor_join = true;
    payload->errnum = result_ ? result_->result : ZLINK_REQUEST_INTERNAL_ERROR;
    if (result_) {
        payload->actor = result_->actor;
        payload->joined_spot_rid = result_->joined_spot_rid;
        payload->join_epoch = result_->join_epoch;
        payload->flags = result_->flags;
    }
    if (payload->errnum == 0)
        copy_recv_parts_to_vectors(parts_, part_count_, &payload->parts);
    close_recv_parts(parts_, part_count_);

    if (napi_call_threadsafe_function(
          state->tsfn, payload.get(), napi_tsfn_nonblocking)
        == napi_ok) {
        payload.release();
    }
    (void) napi_release_threadsafe_function(state->tsfn, napi_tsfn_release);
    state->tsfn = NULL;
}

static void actor_lookup_callback_trampoline(
  const zlink_actor_lookup_result_t *result_,
  void *userdata_)
{
    request_js_state_t *state = static_cast<request_js_state_t *>(userdata_);
    if (!state || !state->tsfn)
        return;

    std::unique_ptr<request_result_js_payload_t> payload(
      new request_result_js_payload_t());
    payload->has_actor_lookup = true;
    payload->errnum = result_ ? result_->result : ZLINK_REQUEST_INTERNAL_ERROR;
    if (result_) {
        payload->actor = result_->actor;
        payload->flags = result_->flags;
    }

    if (napi_call_threadsafe_function(
          state->tsfn, payload.get(), napi_tsfn_nonblocking)
        == napi_ok) {
        payload.release();
    }
    (void) napi_release_threadsafe_function(state->tsfn, napi_tsfn_release);
    state->tsfn = NULL;
}

struct sync_request_state_t
{
    sync_request_state_t () : result (ZLINK_REQUEST_INTERNAL_ERROR), done (false) {}
    std::mutex mu;
    std::condition_variable cv;
    zlink_request_result_t result;
    bool done;
};

static void sync_request_callback(zlink_request_result_t result_,
                                  zlink_msg_t *parts_,
                                  size_t part_count_,
                                  void *userdata_)
{
    sync_request_state_t *state =
      static_cast<sync_request_state_t *>(userdata_);
    close_recv_parts(parts_, part_count_);
    if (!state)
        return;
    {
        std::lock_guard<std::mutex> lock(state->mu);
        state->result = result_;
        state->done = true;
    }
    state->cv.notify_one();
}

static zlink_request_result_t wait_sync_request(sync_request_state_t *state)
{
    std::unique_lock<std::mutex> lock(state->mu);
    if (!state->cv.wait_for(lock, std::chrono::seconds(5),
                            [state] { return state->done; }))
        return ZLINK_REQUEST_TIMED_OUT;
    return state->result;
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

static bool build_spot_node_socket_snapshot_filter(
  napi_env env,
  napi_value value,
  zlink_spot_node_socket_snapshot_filter_t *out)
{
    memset(out, 0, sizeof(*out));
    out->owner = ZLINK_SPOT_NODE_SOCKET_OWNER_ANY;
    out->socket_type = ZLINK_SOCKET_ANY;
    if (value == NULL)
        return false;
    napi_valuetype type = napi_undefined;
    if (napi_typeof(env, value, &type) != napi_ok
        || type == napi_undefined || type == napi_null) {
        return false;
    }

    napi_value prop;
    bool has_prop = false;
    if (napi_has_named_property(env, value, "owner", &has_prop) == napi_ok
        && has_prop
        && napi_get_named_property(env, value, "owner", &prop) == napi_ok) {
        uint32_t raw = 0;
        napi_get_value_uint32(env, prop, &raw);
        out->owner = static_cast<zlink_spot_node_socket_owner_t>(raw);
    }
    if (napi_has_named_property(env, value, "socketType", &has_prop) == napi_ok
        && has_prop
        && napi_get_named_property(env, value, "socketType", &prop) == napi_ok) {
        uint32_t raw = 0;
        napi_get_value_uint32(env, prop, &raw);
        out->socket_type = static_cast<zlink_socket_type_t>(raw);
    }
    if (napi_has_named_property(env, value, "socketName", &has_prop) == napi_ok
        && has_prop
        && napi_get_named_property(env, value, "socketName", &prop) == napi_ok) {
        std::string socket_name = get_string(env, prop);
        strncpy(out->socket_name, socket_name.c_str(),
                sizeof(out->socket_name) - 1);
    }
    return true;
}

static napi_value create_monitor_snapshot_value(
  napi_env env,
  const zlink_monitor_snapshot_t &snapshot)
{
    napi_value obj;
    napi_create_object(env, &obj);
    set_uint32_property(env, obj, "sourceKind",
                        static_cast<uint32_t>(snapshot.source_kind));
    set_uint32_property(env, obj, "stateFlags", snapshot.state_flags);
    set_uint32_property(env, obj, "detailFlags", snapshot.detail_flags);
    set_int64_property(env, obj, "sndPendingMsgs",
                       static_cast<int64_t>(snapshot.snd_pending_msgs));
    set_int64_property(env, obj, "rcvPendingMsgs",
                       static_cast<int64_t>(snapshot.rcv_pending_msgs));
    napi_value auto_hwm_enabled;
    napi_get_boolean(env, snapshot.auto_hwm_enabled != 0, &auto_hwm_enabled);
    napi_set_named_property(env, obj, "autoHwmEnabled", auto_hwm_enabled);
    set_uint32_property(env, obj, "autoHwmProfile", snapshot.auto_hwm_profile);
    set_uint32_property(env, obj, "autoHwmRole", snapshot.auto_hwm_role);
    set_uint32_property(env, obj, "autoHwmPolicyClass",
                        snapshot.auto_hwm_policy_class);
    set_int64_property(env, obj, "autoHwmUnitBudgetBytes",
                       static_cast<int64_t>(snapshot.auto_hwm_unit_budget_bytes));
    set_uint32_property(env, obj, "autoHwmSizeCap",
                        snapshot.auto_hwm_size_cap);
    set_int64_property(env, obj, "autoHwmSocketMessageSlots",
                       static_cast<int64_t>(snapshot.auto_hwm_socket_message_slots));
    set_int64_property(env, obj, "autoHwmEffectiveMessageBytes",
                       static_cast<int64_t>(snapshot.auto_hwm_effective_message_bytes));
    set_int64_property(env, obj, "autoHwmAppliedSndHwm",
                       snapshot.auto_hwm_applied_sndhwm);
    set_int64_property(env, obj, "autoHwmAppliedRcvHwm",
                       snapshot.auto_hwm_applied_rcvhwm);
    set_int64_property(env, obj, "autoHwmEffectiveSndBuf",
                       snapshot.auto_hwm_effective_sndbuf);
    set_int64_property(env, obj, "autoHwmEffectiveRcvBuf",
                       snapshot.auto_hwm_effective_rcvbuf);
    set_int64_property(env, obj, "autoHwmLastRecalcMs",
                       static_cast<int64_t>(snapshot.auto_hwm_last_recalc_ms));
    set_uint32_property(env, obj, "autoHwmLastRecalcReason",
                        snapshot.auto_hwm_last_recalc_reason);
    set_uint32_property(env, obj, "autoHwmSendBlockedRatioPpm",
                        snapshot.auto_hwm_send_blocked_ratio_ppm);
    set_int64_property(env, obj, "autoHwmDeferredSndHwm",
                       snapshot.auto_hwm_deferred_sndhwm);
    set_int64_property(env, obj, "autoHwmDeferredRcvHwm",
                       snapshot.auto_hwm_deferred_rcvhwm);
    return obj;
}

} // namespace

napi_value router_spot_send(napi_env env, napi_callback_info info)
{
    napi_value argv[5];
    size_t argc = 5;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    if (argc < 4) {
        napi_throw_type_error(
          env, NULL,
          "routerSpotSend requires (socket, destNodeRid, destSpotRid, parts, flags)");
        return NULL;
    }
    void *router = NULL;
    napi_get_value_external(env, argv[0], &router);
    zlink_routing_id_t dest_node_rid;
    zlink_routing_id_t dest_spot_rid;
    if (!parse_routing_id_value(env, argv[1], &dest_node_rid))
        return NULL;
    if (!parse_routing_id_value(env, argv[2], &dest_spot_rid))
        return NULL;
    std::vector<zlink_msg_t> parts;
    if (!build_msg_vector(env, argv[3], &parts))
        return NULL;
    int32_t flags = 0;
    if (argc >= 5)
        napi_get_value_int32(env, argv[4], &flags);
    int rc = router_send_spot_parts(router,
                                    &dest_node_rid,
                                    &dest_spot_rid,
                                    parts.data(),
                                    parts.size(),
                                    static_cast<zlink_send_flags_t>(flags));
    if (rc != ZLINK_SUBMIT_OK) {
        return throw_last_error(env, "routerSpotSend failed");
    }
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

napi_value spot_reply_spot(napi_env env, napi_callback_info info)
{
    napi_value argv[5];
    size_t argc = 5;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *spot = NULL;
    napi_get_value_external(env, argv[0], &spot);
    zlink_routing_id_t dest_node_rid;
    zlink_routing_id_t dest_spot_rid;
    if (!parse_routing_id_value(env, argv[1], &dest_node_rid))
        return NULL;
    if (!parse_routing_id_value(env, argv[2], &dest_spot_rid))
        return NULL;
    bool lossless = false;
    uint64_t request_seq = 0;
    napi_get_value_bigint_uint64(env, argv[3], &request_seq, &lossless);
    std::vector<zlink_msg_t> parts;
    if (!build_msg_vector(env, argv[4], &parts))
        return NULL;
    int rc = spot_reply_spot_parts(spot,
                                   &dest_node_rid,
                                   &dest_spot_rid,
                                   request_seq,
                                   parts.data(),
                                   parts.size());
    if (rc != ZLINK_SUBMIT_OK) {
        return throw_last_error(env, "spotReplySpot failed");
    }
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

napi_value spot_send_spot(napi_env env, napi_callback_info info)
{
    napi_value argv[5];
    size_t argc = 5;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *spot = NULL;
    napi_get_value_external(env, argv[0], &spot);
    zlink_routing_id_t dest_node_rid;
    zlink_routing_id_t dest_spot_rid;
    if (!parse_routing_id_value(env, argv[1], &dest_node_rid))
        return NULL;
    if (!parse_routing_id_value(env, argv[2], &dest_spot_rid))
        return NULL;
    std::vector<zlink_msg_t> parts;
    if (!build_msg_vector(env, argv[3], &parts))
        return NULL;
    int32_t flags = 0;
    if (argc >= 5)
        napi_get_value_int32(env, argv[4], &flags);
    int rc = spot_send_spot_parts(spot,
                                  &dest_node_rid,
                                  &dest_spot_rid,
                                  parts.data(),
                                  parts.size(),
                                  static_cast<zlink_send_flags_t>(flags));
    if (rc != ZLINK_SUBMIT_OK)
        return throw_last_error(env, "spotSendToSpot failed");
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

napi_value spot_reply_router(napi_env env, napi_callback_info info)
{
    napi_value argv[4];
    size_t argc = 4;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *spot = NULL;
    napi_get_value_external(env, argv[0], &spot);
    zlink_routing_id_t peer_rid;
    if (!parse_routing_id_value(env, argv[1], &peer_rid))
        return NULL;
    bool lossless = false;
    uint64_t request_seq = 0;
    napi_get_value_bigint_uint64(env, argv[2], &request_seq, &lossless);
    std::vector<zlink_msg_t> parts;
    if (!build_msg_vector(env, argv[3], &parts))
        return NULL;
    int rc = spot_reply_router_parts(
      spot, &peer_rid, request_seq, parts.data(), parts.size());
    if (rc != ZLINK_SUBMIT_OK) {
        return throw_last_error(env, "spotReplyRouter failed");
    }
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

napi_value spot_routed_handler(napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    if (argc < 2) {
        napi_throw_type_error(env, NULL, "spotRoutedHandler requires (spot, handler)");
        return NULL;
    }
    void *spot = NULL;
    napi_get_value_external(env, argv[0], &spot);
    napi_valuetype handler_type = napi_undefined;
    napi_typeof(env, argv[1], &handler_type);
    if (handler_type != napi_function) {
        napi_throw_type_error(env, NULL, "spotRoutedHandler handler must be a function");
        return NULL;
    }
    release_spot_routed_handler_slot(spot);

    spot_routed_js_state_t *state = NULL;
    {
        std::lock_guard<std::mutex> lock(g_spot_routed_slots_mu);
        state = find_free_spot_routed_slot_unsafe();
        if (!state) {
            napi_throw_error(env, NULL, "spot routed handler slot exhausted");
            return NULL;
        }
        state->used = true;
        state->spot = spot;
        state->env = env;
        state->tsfn = NULL;
    }

    napi_value resource_name;
    napi_create_string_utf8(env, "zlink-spot-routed-handler", NAPI_AUTO_LENGTH, &resource_name);
    napi_threadsafe_function tsfn = NULL;
    napi_status status = napi_create_threadsafe_function(
      env, argv[1], NULL, resource_name, 0, 1, state, spot_routed_tsfn_finalize,
      state, spot_routed_tsfn_call_js, &tsfn);
    if (status != napi_ok) {
        std::lock_guard<std::mutex> lock(g_spot_routed_slots_mu);
        reset_spot_routed_slot_unsafe(state);
        napi_throw_error(env, NULL, "spot routed handler setup failed");
        return NULL;
    }
    (void) napi_unref_threadsafe_function(env, tsfn);
    {
        std::lock_guard<std::mutex> lock(g_spot_routed_slots_mu);
        state->tsfn = tsfn;
    }

    int rc = zlink_spot_handler(spot, spot_routed_dispatch, state);
    if (rc != 0) {
        release_spot_routed_handler_slot(spot);
        return throw_last_error(env, "spotRoutedHandler failed");
    }
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

napi_value spot_dispatch_event_handler(napi_env env, napi_callback_info info)
{
    napi_value argv[3];
    size_t argc = 3;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    if (argc < 3) {
        napi_throw_type_error(env, NULL, "spotDispatchEventHandler requires (spot, node, handler)");
        return NULL;
    }
    void *spot = NULL;
    void *node = NULL;
    napi_get_value_external(env, argv[0], &spot);
    napi_get_value_external(env, argv[1], &node);
    napi_valuetype handler_type = napi_undefined;
    napi_typeof(env, argv[2], &handler_type);
    if (handler_type != napi_function) {
        napi_throw_type_error(env, NULL, "spotDispatchEventHandler handler must be a function");
        return NULL;
    }
    release_spot_dispatch_event_handler_slot(spot);

    spot_dispatch_event_js_state_t *state = NULL;
    {
        std::lock_guard<std::mutex> lock(g_spot_dispatch_event_slots_mu);
        state = find_free_spot_dispatch_event_slot_unsafe();
        if (!state) {
            napi_throw_error(env, NULL, "spot dispatch handler slot exhausted");
            return NULL;
        }
        state->used = true;
        state->spot = spot;
        state->node = node;
        state->env = env;
        state->tsfn = NULL;
    }

    napi_value resource_name;
    napi_create_string_utf8(env, "zlink-spot-dispatch-handler", NAPI_AUTO_LENGTH, &resource_name);
    napi_threadsafe_function tsfn = NULL;
    napi_status status = napi_create_threadsafe_function(
      env, argv[2], NULL, resource_name, 0, 1, state,
      spot_dispatch_event_tsfn_finalize, state, spot_dispatch_event_tsfn_call_js,
      &tsfn);
    if (status != napi_ok) {
      std::lock_guard<std::mutex> lock(g_spot_dispatch_event_slots_mu);
      reset_spot_dispatch_event_slot_unsafe(state);
      napi_throw_error(env, NULL, "spot dispatch handler setup failed");
      return NULL;
    }
    (void) napi_unref_threadsafe_function(env, tsfn);
    {
        std::lock_guard<std::mutex> lock(g_spot_dispatch_event_slots_mu);
        state->tsfn = tsfn;
    }

    int rc = zlink_spot_dispatch_event_handler(spot, spot_dispatch_event_dispatch, state);
    if (rc != 0) {
        release_spot_dispatch_event_handler_slot(spot);
        return throw_last_error(env, "spotDispatchEventHandler failed");
    }
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

napi_value spot_recv_routed(napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *spot = NULL;
    napi_get_value_external(env, argv[0], &spot);
    int32_t flags = 0;
    if (argc >= 2)
        napi_get_value_int32(env, argv[1], &flags);
    zlink_routing_id_t source_rid;
    zlink_routing_id_t spot_rid;
    uint64_t request_seq = 0;
    std::vector<zlink_msg_t> parts;
    int rc = spot_recv_parts(
      spot,
      &source_rid,
      &spot_rid,
      &request_seq,
      &parts,
      static_cast<zlink_recv_flags_t>(flags));
    if (rc != ZLINK_RECV_OK)
        return throw_last_error(env, "spotRecvRouted failed");
    napi_value out =
      create_spot_routed_event_value(env,
                                     source_rid.size > 0 ? &source_rid : NULL,
                                     spot_rid.size > 0 ? &spot_rid : NULL,
                                     request_seq,
                                     parts.data(),
                                     parts.size());
    close_msg_vector(parts);
    return out;
}

napi_value spot_actor_join_recv(napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *spot = NULL;
    napi_get_value_external(env, argv[0], &spot);
    int32_t flags = 0;
    if (argc >= 2)
        napi_get_value_int32(env, argv[1], &flags);
    zlink_actor_join_info_t join_info;
    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    int rc = zlink_spot_actor_join_recv(
      spot, &join_info, &parts, &part_count, static_cast<zlink_recv_flags_t>(flags));
    if (rc != ZLINK_RECV_OK) {
        if ((flags & ZLINK_RECV_FLAGS_DONTWAIT) && zlink_errno() == EAGAIN) {
            napi_value none;
            napi_get_null(env, &none);
            return none;
        }
        return throw_last_error(env, "spotActorJoinRecv failed");
    }
    napi_value out;
    napi_create_object(env, &out);
    napi_set_named_property(env, out, "info",
                            create_actor_join_info_value(env, join_info));
    napi_value parts_array;
    napi_create_array_with_length(env, part_count, &parts_array);
    for (size_t i = 0; i < part_count; ++i) {
        napi_value part = create_message_snapshot_value(env, NULL, &parts[i]);
        napi_set_element(env, parts_array, static_cast<uint32_t>(i), part);
    }
    napi_set_named_property(env, out, "parts", parts_array);
    napi_value message;
    if (part_count > 0) {
        napi_get_element(env, parts_array, 0, &message);
    } else {
        zlink_msg_t empty;
        if (zlink_msg_init(&empty) != 0) {
            zlink_multipart_close(parts, part_count);
            return throw_last_error(env, "spotActorJoinRecv failed");
        }
        message = create_message_snapshot_value(env, NULL, &empty);
        zlink_msg_close(&empty);
    }
    napi_set_named_property(env, out, "message", message);
    zlink_multipart_close(parts, part_count);
    return out;
}

napi_value spot_actor_join_reply(napi_env env, napi_callback_info info)
{
    napi_value argv[4];
    size_t argc = 4;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *spot = NULL;
    napi_get_value_external(env, argv[0], &spot);
    zlink_actor_join_info_t join_info;
    if (!parse_actor_join_info_value(env, argv[1], &join_info))
        return NULL;
    bool accepted = false;
    napi_get_value_bool(env, argv[2], &accepted);
    std::vector<zlink_msg_t> parts;
    if (!build_msg_vector(env, argv[3], &parts))
        return NULL;
    int rc = zlink_spot_actor_join_reply(
      spot,
      &join_info,
      accepted ? 1u : 0u,
      parts.empty() ? NULL : parts.data(),
      parts.size());
    if (rc != ZLINK_SUBMIT_OK) {
        close_msg_vector(parts);
        return throw_last_error(env, "spotActorJoinReply failed");
    }
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

napi_value spot_actors_snapshot(napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *spot = NULL;
    napi_get_value_external(env, argv[0], &spot);
    size_t count = 0;
    int rc = zlink_spot_actors_snapshot(spot, NULL, &count);
    if (rc != ZLINK_CONFIG_OK)
        return throw_last_error(env, "spotActorsSnapshot failed");
    napi_value arr;
    napi_create_array_with_length(env, count, &arr);
    if (count == 0)
        return arr;
    std::vector<zlink_actor_ref_t> entries(count);
    rc = zlink_spot_actors_snapshot(spot, entries.data(), &count);
    if (rc != ZLINK_CONFIG_OK)
        return throw_last_error(env, "spotActorsSnapshot failed");
    for (size_t i = 0; i < count; ++i) {
        napi_set_element(env, arr, static_cast<uint32_t>(i),
                         create_actor_ref_value(env, entries[i]));
    }
    return arr;
}

napi_value router_spot_request(napi_env env, napi_callback_info info)
{
    napi_value argv[7];
    size_t argc = 7;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    if (argc < 7) {
        napi_throw_type_error(
          env, NULL,
          "routerSpotRequest requires (socket, destNodeRid, destSpotRid, parts, handler, flags, timeoutMs)");
        return NULL;
    }
    void *router = NULL;
    napi_get_value_external(env, argv[0], &router);
    zlink_routing_id_t dest_node_rid;
    zlink_routing_id_t dest_spot_rid;
    if (!parse_routing_id_value(env, argv[1], &dest_node_rid))
        return NULL;
    if (!parse_routing_id_value(env, argv[2], &dest_spot_rid))
        return NULL;
    std::vector<zlink_msg_t> parts;
    if (!build_msg_vector(env, argv[3], &parts))
        return NULL;
    napi_valuetype handler_type = napi_undefined;
    napi_typeof(env, argv[4], &handler_type);
    if (handler_type != napi_function) {
        close_msg_vector(parts);
        napi_throw_type_error(
          env, NULL, "routerSpotRequest handler must be a function");
        return NULL;
    }
    int32_t flags = 0;
    napi_get_value_int32(env, argv[5], &flags);
    int32_t timeout_ms = 0;
    napi_get_value_int32(env, argv[6], &timeout_ms);
    request_js_state_t *state = create_request_js_state(env, argv[4]);
    if (!state) {
        close_msg_vector(parts);
        return NULL;
    }
    int rc = router_request_spot_parts(router,
                                       &dest_node_rid,
                                       &dest_spot_rid,
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
        return throw_last_error(env, "routerSpotRequest failed");
    }
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

napi_value router_spot_reply(napi_env env, napi_callback_info info)
{
    napi_value argv[5];
    size_t argc = 5;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    if (argc < 5) {
        napi_throw_type_error(
          env, NULL,
          "routerSpotReply requires (socket, destNodeRid, destSpotRid, requestSeq, parts)");
        return NULL;
    }
    void *router = NULL;
    napi_get_value_external(env, argv[0], &router);
    zlink_routing_id_t dest_node_rid;
    zlink_routing_id_t dest_spot_rid;
    if (!parse_routing_id_value(env, argv[1], &dest_node_rid))
        return NULL;
    if (!parse_routing_id_value(env, argv[2], &dest_spot_rid))
        return NULL;
    uint64_t request_seq = 0;
    bool lossless = false;
    if (napi_get_value_bigint_uint64(env, argv[3], &request_seq, &lossless)
        != napi_ok
        || !lossless) {
        napi_throw_type_error(env, NULL, "requestSeq must be uint64");
        return NULL;
    }
    std::vector<zlink_msg_t> parts;
    if (!build_msg_vector(env, argv[4], &parts))
        return NULL;
    int rc = router_reply_spot_parts(router,
                                     &dest_node_rid,
                                     &dest_spot_rid,
                                     request_seq,
                                     parts.data(),
                                     parts.size());
    if (rc != ZLINK_SUBMIT_OK) {
        return throw_last_error(env, "routerSpotReply failed");
    }
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
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

napi_value discovery_resolve_actor(napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *discovery = NULL;
    napi_get_value_external(env, argv[0], &discovery);
    std::string actor_id = get_string(env, argv[1]);
    zlink_actor_route_t route;
    int rc = zlink_discovery_resolve_actor(discovery, actor_id.c_str(), &route);
    if (rc != ZLINK_CONFIG_OK)
        return throw_last_error(env, "discoveryResolveActor failed");
    return create_actor_route_value(env, route);
}

napi_value spot_node_new(napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *ctx = NULL;
    napi_get_value_external(env, argv[0], &ctx);
    zlink_spot_node_options_t options;
    zlink_spot_node_options_t *options_ptr = NULL;
    memset(&options, 0, sizeof(options));
    if (argc >= 2) {
        napi_valuetype type = napi_undefined;
        if (napi_typeof(env, argv[1], &type) == napi_ok
            && type != napi_undefined && type != napi_null) {
            napi_value mode_value;
            bool has_mode = false;
            if (napi_has_named_property(env, argv[1], "mode", &has_mode) == napi_ok
                && has_mode
                && napi_get_named_property(env, argv[1], "mode", &mode_value) == napi_ok) {
                uint32_t raw_mode = 0;
                napi_get_value_uint32(env, mode_value, &raw_mode);
                options.mode = static_cast<zlink_spot_node_mode_t>(raw_mode);
                options_ptr = &options;
            }
        }
    }
    void *node = zlink_spot_node_new(ctx, options_ptr);
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

napi_value spot_node_disconnect_peer_rid(napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *node = NULL;
    napi_get_value_external(env, argv[0], &node);
    zlink_routing_id_t peer_rid;
    if (!parse_routing_id_value(env, argv[1], &peer_rid))
        return NULL;
    int rc = zlink_spot_node_disconnect_peer_rid(node, &peer_rid);
    if (rc != 0)
        return throw_last_error(env, "spot_node_disconnect_peer_rid failed");
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

napi_value spot_node_attach_channel_dealer(napi_env env, napi_callback_info info)
{
    napi_value argv[3];
    size_t argc = 3;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *node = NULL;
    void *discovery = NULL;
    void *dealer = NULL;
    napi_get_value_external(env, argv[0], &node);
    napi_get_value_external(env, argv[1], &discovery);
    napi_get_value_external(env, argv[2], &dealer);
    int rc = zlink_spot_node_attach_channel_dealer(node, discovery, dealer);
    if (rc != 0)
        return throw_last_error(env, "spotNodeAttachChannelDealer failed");
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

napi_value spot_node_attach_channel_dealer_manual(napi_env env, napi_callback_info info)
{
    napi_value argv[3];
    size_t argc = 3;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *node = NULL;
    void *dealer = NULL;
    napi_get_value_external(env, argv[0], &node);
    std::string channel_name = get_string(env, argv[1]);
    napi_get_value_external(env, argv[2], &dealer);
    int rc = zlink_spot_node_attach_channel_dealer_manual(
      node, channel_name.c_str(), dealer);
    if (rc != 0)
        return throw_last_error(env, "spotNodeAttachChannelDealerManual failed");
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

napi_value spot_node_attach_pub_ingress(napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *node = NULL;
    void *pub = NULL;
    napi_get_value_external(env, argv[0], &node);
    napi_get_value_external(env, argv[1], &pub);
    int rc = zlink_spot_node_attach_pub_ingress(node, pub);
    if (rc != 0)
        return throw_last_error(env, "spotNodeAttachPubIngress failed");
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

napi_value spot_node_setsockopt(napi_env env, napi_callback_info info)
{
    napi_value argv[3];
    size_t argc = 3;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    if (argc < 3) {
        napi_throw_type_error(env, NULL, "spotNodeSetOption expects node, option, value");
        return NULL;
    }

    void *node = NULL;
    napi_get_value_external(env, argv[0], &node);
    int32_t opt = 0;
    if (napi_get_value_int32(env, argv[1], &opt) != napi_ok) {
        napi_throw_type_error(env, NULL, "option must be an integer");
        return NULL;
    }
    void *data = NULL;
    size_t len = 0;
    if (napi_get_buffer_info(env, argv[2], &data, &len) != napi_ok) {
        napi_throw_type_error(env, NULL, "value must be Buffer");
        return NULL;
    }

    int rc = zlink_set_spot_node_option(
      node, static_cast<zlink_spot_node_option_t>(opt), data, len);
    if (rc != 0)
        return throw_last_error(env, "spotNodeSetOption failed");

    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

napi_value spot_node_getsockopt(napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    if (argc < 2) {
        napi_throw_type_error(env, NULL, "spotNodeGetOption expects node, option");
        return NULL;
    }

    void *node = NULL;
    napi_get_value_external(env, argv[0], &node);
    int32_t opt = 0;
    if (napi_get_value_int32(env, argv[1], &opt) != napi_ok) {
        napi_throw_type_error(env, NULL, "option must be an integer");
        return NULL;
    }

    size_t len = sizeof(int);
    void *data = NULL;
    napi_value buf;
    napi_create_buffer(env, len, &data, &buf);
    int rc = zlink_get_spot_node_option(
      node, static_cast<zlink_spot_node_option_t>(opt), data, &len);
    if (rc != 0)
        return throw_last_error(env, "spotNodeGetOption failed");
    return buf;
}

napi_value spot_node_entry_spot(napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    if (argc < 1) {
        napi_throw_type_error(env, NULL, "spotNodeEntrySpot expects node");
        return NULL;
    }

    void *node = NULL;
    napi_get_value_external(env, argv[0], &node);
    void *spot = NULL;
    zlink_config_result_t rc = zlink_spot_node_entry_spot(node, &spot);
    if (rc != ZLINK_CONFIG_OK)
        return throw_last_error(env, "spotNodeEntrySpot failed");

    napi_value out;
    napi_create_external(env, spot, NULL, NULL, &out);
    return out;
}

napi_value spot_node_spot_lookup(napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    if (argc < 2) {
        napi_throw_type_error(env, NULL, "spotNodeSpotLookup expects node, spotRid");
        return NULL;
    }

    void *node = NULL;
    napi_get_value_external(env, argv[0], &node);
    zlink_routing_id_t spot_rid;
    if (!parse_routing_id_value(env, argv[1], &spot_rid))
        return NULL;

    void *spot = NULL;
    zlink_config_result_t rc = zlink_spot_node_spot_lookup(node, &spot_rid, &spot);
    if (rc == ZLINK_CONFIG_NOT_FOUND) {
        napi_value none;
        napi_get_null(env, &none);
        return none;
    }
    if (rc != ZLINK_CONFIG_OK)
        return throw_last_error(env, "spotNodeSpotLookup failed");

    napi_value out;
    napi_create_external(env, spot, NULL, NULL, &out);
    return out;
}

napi_value spot_set_option(napi_env env, napi_callback_info info)
{
    napi_value argv[3];
    size_t argc = 3;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    if (argc < 3) {
        napi_throw_type_error(env, NULL, "spotSetOption expects spot, option, value");
        return NULL;
    }

    void *spot = NULL;
    napi_get_value_external(env, argv[0], &spot);
    int32_t opt = 0;
    if (napi_get_value_int32(env, argv[1], &opt) != napi_ok) {
        napi_throw_type_error(env, NULL, "option must be an integer");
        return NULL;
    }
    void *data = NULL;
    size_t len = 0;
    if (napi_get_buffer_info(env, argv[2], &data, &len) != napi_ok) {
        napi_throw_type_error(env, NULL, "value must be Buffer");
        return NULL;
    }

    int rc = zlink_set_spot_option(
      spot, static_cast<zlink_spot_option_t>(opt), data, len);
    if (rc != 0)
        return throw_last_error(env, "spotSetOption failed");

    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

napi_value spot_get_option(napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    if (argc < 2) {
        napi_throw_type_error(env, NULL, "spotGetOption expects spot, option");
        return NULL;
    }

    void *spot = NULL;
    napi_get_value_external(env, argv[0], &spot);
    int32_t opt = 0;
    if (napi_get_value_int32(env, argv[1], &opt) != napi_ok) {
        napi_throw_type_error(env, NULL, "option must be an integer");
        return NULL;
    }

    size_t len = sizeof(int);
    void *data = NULL;
    napi_value buf;
    napi_create_buffer(env, len, &data, &buf);
    int rc = zlink_get_spot_option(
      spot, static_cast<zlink_spot_option_t>(opt), data, &len);
    if (rc != 0)
        return throw_last_error(env, "spotGetOption failed");
    return buf;
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
    set_string_property(env, obj, "channelName", status.channel_name);
    set_string_property(env, obj, "localEndpoint", status.local_endpoint);
    napi_value rid = create_routing_id_value(env, status.node_routing_id);
    napi_set_named_property(env, obj, "nodeRoutingId", rid);
    set_uint32_property(env, obj, "state", static_cast<uint32_t>(status.state));
    set_uint32_property(env, obj, "configuredPeerCount", status.configured_peer_count);
    set_uint32_property(env, obj, "activePeerCount", status.active_peer_count);
    set_uint32_property(env, obj, "connectedPeerCount", status.connected_peer_count);
    set_uint32_property(env, obj, "subjectCount", status.subject_count);
    set_uint32_property(env, obj, "readySubjectCount", status.ready_subject_count);
    set_uint32_property(env, obj, "disconnectedSubTargetCount",
                        status.disconnected_sub_target_count);
    set_uint32_property(env, obj, "disconnectedRoutedTargetCount",
                        status.disconnected_routed_target_count);
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
        set_string_property(env, obj, "channelName", entries[i].channel_name);
        set_string_property(env, obj, "localEndpoint", entries[i].local_endpoint);
        set_string_property(env, obj, "peerEndpoint", entries[i].peer_endpoint);
        set_uint32_property(env, obj, "source", static_cast<uint32_t>(entries[i].source));
        set_uint32_property(env, obj, "state", static_cast<uint32_t>(entries[i].state));
        set_uint32_property(env, obj, "weight",
                            static_cast<uint32_t>(entries[i].weight));
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
        set_string_property(env, obj, "channelName", entries[i].channel_name);
        set_string_property(env, obj, "localEndpoint", entries[i].local_endpoint);
        set_string_property(env, obj, "peerEndpoint", entries[i].peer_endpoint);
        set_uint32_property(env, obj, "source", static_cast<uint32_t>(entries[i].source));
        set_uint32_property(env, obj, "state", static_cast<uint32_t>(entries[i].state));
        set_uint32_property(env, obj, "weight",
                            static_cast<uint32_t>(entries[i].weight));
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

napi_value spot_node_internal_sockets_snapshot(napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *node = NULL;
    napi_get_value_external(env, argv[0], &node);
    zlink_spot_node_socket_snapshot_filter_t filter;
    zlink_spot_node_socket_snapshot_filter_t *filter_ptr =
      build_spot_node_socket_snapshot_filter(
        env, argc >= 2 ? argv[1] : NULL, &filter) ? &filter : NULL;

    size_t count = 0;
    int rc = zlink_spot_node_internal_sockets_snapshot(node, filter_ptr, NULL, &count);
    if (rc != 0)
        return throw_last_error(env, "spot_node_internal_sockets_snapshot failed");
    napi_value arr;
    napi_create_array_with_length(env, count, &arr);
    if (count == 0)
        return arr;

    std::vector<zlink_spot_node_socket_snapshot_entry_t> entries(count);
    rc = zlink_spot_node_internal_sockets_snapshot(
      node, filter_ptr, entries.data(), &count);
    if (rc != 0)
        return throw_last_error(env, "spot_node_internal_sockets_snapshot failed");
    for (size_t i = 0; i < count; ++i) {
        napi_value obj;
        napi_create_object(env, &obj);
        set_uint32_property(env, obj, "owner",
                            static_cast<uint32_t>(entries[i].owner));
        set_int64_property(env, obj, "ownerId",
                           static_cast<int64_t>(entries[i].owner_id));
        set_string_property(env, obj, "ownerName", entries[i].owner_name);
        set_string_property(env, obj, "socketName", entries[i].socket_name);
        set_uint32_property(env, obj, "socketType",
                            static_cast<uint32_t>(entries[i].socket_type));
        napi_value visible;
        napi_get_boolean(env, entries[i].auto_hwm_visible != 0, &visible);
        napi_set_named_property(env, obj, "autoHwmVisible", visible);
        napi_value snapshot = create_monitor_snapshot_value(env, entries[i].snapshot);
        napi_set_named_property(env, obj, "snapshot", snapshot);
        napi_set_element(env, arr, static_cast<uint32_t>(i), obj);
    }
    return arr;
}

napi_value spot_node_spots_snapshot(napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *node = NULL;
    napi_get_value_external(env, argv[0], &node);
    size_t count = 0;
    int rc = zlink_spot_node_spots_snapshot(node, NULL, &count);
    if (rc != 0)
        return throw_last_error(env, "spotNodeSpotsSnapshot failed");
    napi_value arr;
    napi_create_array_with_length(env, count, &arr);
    if (count == 0)
        return arr;
    std::vector<zlink_spot_node_spot_entry_t> entries(count);
    rc = zlink_spot_node_spots_snapshot(node, entries.data(), &count);
    if (rc != 0)
        return throw_last_error(env, "spotNodeSpotsSnapshot failed");
    for (size_t i = 0; i < count; ++i) {
        napi_set_element(env, arr, static_cast<uint32_t>(i),
                         create_spot_node_spot_entry_value(env, entries[i]));
    }
    return arr;
}

napi_value spot_node_actors_snapshot(napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *node = NULL;
    napi_get_value_external(env, argv[0], &node);
    size_t count = 0;
    int rc = zlink_spot_node_actors_snapshot(node, NULL, &count);
    if (rc != 0)
        return throw_last_error(env, "spotNodeActorsSnapshot failed");
    napi_value arr;
    napi_create_array_with_length(env, count, &arr);
    if (count == 0)
        return arr;
    std::vector<zlink_spot_node_actor_entry_t> entries(count);
    rc = zlink_spot_node_actors_snapshot(node, entries.data(), &count);
    if (rc != 0)
        return throw_last_error(env, "spotNodeActorsSnapshot failed");
    for (size_t i = 0; i < count; ++i) {
        napi_set_element(env, arr, static_cast<uint32_t>(i),
                         create_spot_node_actor_entry_value(env, entries[i]));
    }
    return arr;
}

napi_value spot_node_actor_new(napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *node = NULL;
    napi_get_value_external(env, argv[0], &node);
    std::string actor_id = get_string(env, argv[1]);
    zlink_actor_ref_t ref;
    int rc = zlink_spot_node_actor_new(node, actor_id.c_str(), &ref);
    if (rc != ZLINK_CONFIG_OK)
        return throw_last_error(env, "spotNodeActorNew failed");
    return create_actor_ref_value(env, ref);
}

napi_value spot_node_actor_destroy(napi_env env, napi_callback_info info)
{
    napi_value argv[4];
    size_t argc = 4;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *node = NULL;
    napi_get_value_external(env, argv[0], &node);
    zlink_actor_ref_t ref;
    if (!parse_actor_ref_value(env, argv[1], &ref))
        return NULL;
    napi_valuetype handler_type = napi_undefined;
    if (argc >= 3)
        napi_typeof(env, argv[2], &handler_type);
    int32_t timeout_ms = 0;
    if (argc >= 4)
        napi_get_value_int32(env, argv[3], &timeout_ms);
    if (handler_type == napi_function) {
        request_js_state_t *state = create_request_js_state(env, argv[2]);
        if (!state)
            return NULL;
        int rc = zlink_spot_node_actor_destroy(
          node, &ref, request_reply_callback_trampoline, state,
          static_cast<uint32_t>(timeout_ms));
        if (rc != ZLINK_SUBMIT_OK) {
            if (state->tsfn)
                (void) napi_release_threadsafe_function(state->tsfn, napi_tsfn_abort);
            return throw_last_error(env, "spotNodeActorDestroy failed");
        }
        napi_value ok;
        napi_get_undefined(env, &ok);
        return ok;
    }
    sync_request_state_t state;
    int rc = zlink_spot_node_actor_destroy(
      node, &ref, sync_request_callback, &state,
      static_cast<uint32_t>(timeout_ms));
    if (rc != ZLINK_SUBMIT_OK)
        return throw_last_error(env, "spotNodeActorDestroy failed");
    zlink_request_result_t request_rc = wait_sync_request(&state);
    if (request_rc != ZLINK_REQUEST_OK)
        return throw_last_error(env, "spotNodeActorDestroy failed");
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

napi_value spot_node_actor_lookup(napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *node = NULL;
    napi_get_value_external(env, argv[0], &node);
    std::string actor_id = get_string(env, argv[1]);
    zlink_actor_ref_t ref;
    int rc = zlink_spot_node_actor_lookup(node, actor_id.c_str(), &ref);
    if (rc != ZLINK_CONFIG_OK)
        return throw_last_error(env, "spotNodeActorLookup failed");
    return create_actor_ref_value(env, ref);
}

napi_value spot_node_actor_join_spot(napi_env env, napi_callback_info info)
{
    napi_value argv[8];
    size_t argc = 8;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *node = NULL;
    napi_get_value_external(env, argv[0], &node);
    zlink_actor_ref_t ref;
    zlink_routing_id_t node_rid;
    zlink_routing_id_t spot_rid;
    if (!parse_actor_ref_value(env, argv[1], &ref))
        return NULL;
    if (!parse_routing_id_value(env, argv[2], &node_rid))
        return NULL;
    if (!parse_routing_id_value(env, argv[3], &spot_rid))
        return NULL;
    std::vector<zlink_msg_t> parts;
    if (!build_msg_vector(env, argv[4], &parts))
        return NULL;
    napi_valuetype handler_type = napi_undefined;
    napi_typeof(env, argv[5], &handler_type);
    if (handler_type != napi_function) {
        close_msg_vector(parts);
        napi_throw_type_error(env, NULL, "actor join handler must be a function");
        return NULL;
    }
    int32_t flags = 0;
    napi_get_value_int32(env, argv[6], &flags);
    int32_t timeout_ms = 0;
    napi_get_value_int32(env, argv[7], &timeout_ms);
    request_js_state_t *state = create_request_js_state(env, argv[5]);
    if (!state) {
        close_msg_vector(parts);
        return NULL;
    }
    int rc = zlink_spot_node_actor_join_spot(
      node,
      &ref,
      &node_rid,
      &spot_rid,
      parts.empty() ? NULL : parts.data(),
      parts.size(),
      actor_join_callback_trampoline,
      state,
      static_cast<zlink_send_flags_t>(flags),
      static_cast<uint32_t>(timeout_ms));
    if (rc != ZLINK_SUBMIT_OK) {
        if (state->tsfn)
            (void) napi_release_threadsafe_function(state->tsfn, napi_tsfn_abort);
        return throw_last_error(env, "spotNodeActorJoinSpot failed");
    }
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

napi_value spot_node_actor_leave_spot(napi_env env, napi_callback_info info)
{
    napi_value argv[5];
    size_t argc = 5;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *node = NULL;
    napi_get_value_external(env, argv[0], &node);
    zlink_actor_ref_t ref;
    zlink_routing_id_t spot_rid;
    if (!parse_actor_ref_value(env, argv[1], &ref))
        return NULL;
    if (!parse_routing_id_value(env, argv[2], &spot_rid))
        return NULL;
    napi_valuetype handler_type = napi_undefined;
    if (argc >= 4)
        napi_typeof(env, argv[3], &handler_type);
    int32_t timeout_ms = 0;
    if (argc >= 5)
        napi_get_value_int32(env, argv[4], &timeout_ms);
    if (handler_type == napi_function) {
        request_js_state_t *state = create_request_js_state(env, argv[3]);
        if (!state)
            return NULL;
        int rc = zlink_spot_node_actor_leave_spot(
          node, &ref, &spot_rid, request_reply_callback_trampoline, state,
          static_cast<uint32_t>(timeout_ms));
        if (rc != ZLINK_SUBMIT_OK) {
            if (state->tsfn)
                (void) napi_release_threadsafe_function(state->tsfn, napi_tsfn_abort);
            return throw_last_error(env, "spotNodeActorLeaveSpot failed");
        }
        napi_value ok;
        napi_get_undefined(env, &ok);
        return ok;
    }
    sync_request_state_t state;
    int rc = zlink_spot_node_actor_leave_spot(
      node, &ref, &spot_rid, sync_request_callback, &state,
      static_cast<uint32_t>(timeout_ms));
    if (rc != ZLINK_SUBMIT_OK)
        return throw_last_error(env, "spotNodeActorLeaveSpot failed");
    zlink_request_result_t request_rc = wait_sync_request(&state);
    if (request_rc != ZLINK_REQUEST_OK)
        return throw_last_error(env, "spotNodeActorLeaveSpot failed");
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

napi_value spot_node_actor_recv_part(napi_env env, napi_callback_info info)
{
    napi_value argv[3];
    size_t argc = 3;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *node = NULL;
    napi_get_value_external(env, argv[0], &node);
    zlink_actor_ref_t ref;
    if (!parse_actor_ref_value(env, argv[1], &ref))
        return NULL;
    int32_t flags = 0;
    if (argc >= 3)
        napi_get_value_int32(env, argv[2], &flags);
    zlink_actor_recv_info_t recv_info;
    zlink_msg_t part;
    zlink_part_flag_t more = ZLINK_PART_FINAL;
    if (zlink_msg_init(&part) != 0)
        return throw_last_error(env, "spotNodeActorRecvPart failed");
    int rc = zlink_spot_node_actor_recv_part(
      node, &ref, &recv_info, &part, &more,
      static_cast<zlink_recv_flags_t>(flags));
    if (rc != ZLINK_RECV_OK) {
        zlink_msg_close(&part);
        if ((flags & ZLINK_RECV_FLAGS_DONTWAIT) && zlink_errno() == EAGAIN) {
            napi_value none;
            napi_get_null(env, &none);
            return none;
        }
        return throw_last_error(env, "spotNodeActorRecvPart failed");
    }
    const unsigned char *data =
      static_cast<const unsigned char *>(zlink_msg_data(&part));
    std::vector<unsigned char> copy;
    if (data && zlink_msg_size(&part) > 0)
        copy.assign(data, data + zlink_msg_size(&part));
    napi_value out = create_actor_part_value(env, recv_info, copy, more);
    zlink_msg_close(&part);
    return out;
}

napi_value spot_node_actor_send_bound_session_msg(napi_env env, napi_callback_info info)
{
    napi_value argv[4];
    size_t argc = 4;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *node = NULL;
    napi_get_value_external(env, argv[0], &node);
    zlink_actor_ref_t ref;
    if (!parse_actor_ref_value(env, argv[1], &ref))
        return NULL;
    std::vector<zlink_msg_t> parts;
    if (!build_msg_vector(env, argv[2], &parts))
        return NULL;
    if (parts.size() != 1) {
        close_msg_vector(parts);
        napi_throw_range_error(env, NULL, "actor send message must be single part");
        return NULL;
    }
    int32_t flags = 0;
    if (argc >= 4)
        napi_get_value_int32(env, argv[3], &flags);
    int rc = zlink_spot_node_actor_send_bound_session_msg(
      node, &ref, &parts[0], static_cast<zlink_send_flags_t>(flags));
    if (rc != ZLINK_SUBMIT_OK) {
        close_msg_vector(parts);
        return throw_last_error(env, "spotNodeActorSendBoundSessionMsg failed");
    }
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

napi_value spot_node_actor_close_bound_session(napi_env env, napi_callback_info info)
{
    napi_value argv[3];
    size_t argc = 3;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *node = NULL;
    napi_get_value_external(env, argv[0], &node);
    zlink_actor_ref_t ref;
    if (!parse_actor_ref_value(env, argv[1], &ref))
        return NULL;
    int32_t timeout_ms = 0;
    if (argc >= 3)
        napi_get_value_int32(env, argv[2], &timeout_ms);
    int rc = zlink_spot_node_actor_close_bound_session(
      node, &ref, static_cast<uint32_t>(timeout_ms));
    if (rc != ZLINK_REQUEST_OK)
        return throw_last_error(env, "spotNodeActorCloseBoundSession failed");
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

static int send_bound_actor_parts(void *stream,
                                  const zlink_routing_id_t *session_rid,
                                  const char *actor_id,
                                  zlink_msg_t *parts,
                                  size_t part_count,
                                  zlink_send_flags_t flags)
{
    for (size_t i = 0; i < part_count; ++i) {
        zlink_part_flag_t part_flag =
          (i + 1u < part_count) ? ZLINK_PART_MORE : ZLINK_PART_FINAL;
        int rc = zlink_stream_send_bound_actor_part(
          stream, session_rid, actor_id, &parts[i], flags, part_flag);
        if (rc != ZLINK_SUBMIT_OK) {
            for (size_t j = i + 1u; j < part_count; ++j)
                zlink_msg_close(&parts[j]);
            return rc;
        }
    }
    return ZLINK_SUBMIT_OK;
}

napi_value stream_bind_actor(napi_env env, napi_callback_info info)
{
    napi_value argv[5];
    size_t argc = 5;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *stream = NULL;
    napi_get_value_external(env, argv[0], &stream);
    zlink_routing_id_t session_rid;
    zlink_actor_ref_t actor;
    if (!parse_routing_id_value(env, argv[1], &session_rid))
        return NULL;
    if (!parse_actor_ref_value(env, argv[2], &actor))
        return NULL;
    napi_valuetype handler_type = napi_undefined;
    if (argc >= 4)
        napi_typeof(env, argv[3], &handler_type);
    int32_t timeout_ms = 0;
    if (argc >= 5)
        napi_get_value_int32(env, argv[4], &timeout_ms);
    if (handler_type == napi_function) {
        request_js_state_t *state = create_request_js_state(env, argv[3]);
        if (!state)
            return NULL;
        int rc = zlink_stream_bind_actor(
          stream, &session_rid, &actor, request_reply_callback_trampoline, state,
          static_cast<uint32_t>(timeout_ms));
        if (rc != ZLINK_SUBMIT_OK) {
            if (state->tsfn)
                (void) napi_release_threadsafe_function(state->tsfn, napi_tsfn_abort);
            return throw_last_error(env, "streamBindActor failed");
        }
        napi_value ok;
        napi_get_undefined(env, &ok);
        return ok;
    }
    sync_request_state_t state;
    int rc = zlink_stream_bind_actor(
      stream, &session_rid, &actor, sync_request_callback, &state,
      static_cast<uint32_t>(timeout_ms));
    if (rc != ZLINK_SUBMIT_OK)
        return throw_last_error(env, "streamBindActor failed");
    zlink_request_result_t request_rc = wait_sync_request(&state);
    if (request_rc != ZLINK_REQUEST_OK)
        return throw_last_error(env, "streamBindActor failed");
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

napi_value stream_unbind_actor(napi_env env, napi_callback_info info)
{
    napi_value argv[5];
    size_t argc = 5;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *stream = NULL;
    napi_get_value_external(env, argv[0], &stream);
    zlink_routing_id_t session_rid;
    if (!parse_routing_id_value(env, argv[1], &session_rid))
        return NULL;
    std::string actor_id = get_string(env, argv[2]);
    napi_valuetype handler_type = napi_undefined;
    if (argc >= 4)
        napi_typeof(env, argv[3], &handler_type);
    int32_t timeout_ms = 0;
    if (argc >= 5)
        napi_get_value_int32(env, argv[4], &timeout_ms);
    if (handler_type == napi_function) {
        request_js_state_t *state = create_request_js_state(env, argv[3]);
        if (!state)
            return NULL;
        int rc = zlink_stream_unbind_actor(
          stream, &session_rid, actor_id.c_str(), request_reply_callback_trampoline, state,
          static_cast<uint32_t>(timeout_ms));
        if (rc != ZLINK_SUBMIT_OK) {
            if (state->tsfn)
                (void) napi_release_threadsafe_function(state->tsfn, napi_tsfn_abort);
            return throw_last_error(env, "streamUnbindActor failed");
        }
        napi_value ok;
        napi_get_undefined(env, &ok);
        return ok;
    }
    sync_request_state_t state;
    int rc = zlink_stream_unbind_actor(
      stream, &session_rid, actor_id.c_str(), sync_request_callback, &state,
      static_cast<uint32_t>(timeout_ms));
    if (rc != ZLINK_SUBMIT_OK)
        return throw_last_error(env, "streamUnbindActor failed");
    zlink_request_result_t request_rc = wait_sync_request(&state);
    if (request_rc != ZLINK_REQUEST_OK)
        return throw_last_error(env, "streamUnbindActor failed");
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

napi_value stream_send_bound_actor_part(napi_env env, napi_callback_info info)
{
    napi_value argv[5];
    size_t argc = 5;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *stream = NULL;
    napi_get_value_external(env, argv[0], &stream);
    zlink_routing_id_t session_rid;
    if (!parse_routing_id_value(env, argv[1], &session_rid))
        return NULL;
    std::string actor_id = get_string(env, argv[2]);
    std::vector<zlink_msg_t> parts;
    if (!build_msg_vector(env, argv[3], &parts))
        return NULL;
    int32_t flags = 0;
    if (argc >= 5)
        napi_get_value_int32(env, argv[4], &flags);
    int rc = send_bound_actor_parts(stream,
                                    &session_rid,
                                    actor_id.c_str(),
                                    parts.data(),
                                    parts.size(),
                                    static_cast<zlink_send_flags_t>(flags));
    if (rc != ZLINK_SUBMIT_OK)
        return throw_last_error(env, "streamSendBoundActorPart failed");
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

napi_value stream_bound_actors(napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *stream = NULL;
    napi_get_value_external(env, argv[0], &stream);
    zlink_routing_id_t session_rid;
    if (!parse_routing_id_value(env, argv[1], &session_rid))
        return NULL;
    size_t count = 0;
    int rc = zlink_stream_bound_actors(stream, &session_rid, NULL, &count);
    if (rc != ZLINK_CONFIG_OK)
        return throw_last_error(env, "streamBoundActors failed");
    napi_value arr;
    napi_create_array_with_length(env, count, &arr);
    if (count == 0)
        return arr;
    std::vector<zlink_actor_ref_t> entries(count);
    rc = zlink_stream_bound_actors(stream, &session_rid, entries.data(), &count);
    if (rc != ZLINK_CONFIG_OK)
        return throw_last_error(env, "streamBoundActors failed");
    for (size_t i = 0; i < count; ++i) {
        napi_set_element(env, arr, static_cast<uint32_t>(i),
                         create_actor_ref_value(env, entries[i]));
    }
    return arr;
}

napi_value remote_actor_get_ref(napi_env env, napi_callback_info info)
{
    napi_value argv[5];
    size_t argc = 5;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *node = NULL;
    napi_get_value_external(env, argv[0], &node);
    zlink_routing_id_t target_node_rid;
    if (!parse_routing_id_value(env, argv[1], &target_node_rid))
        return NULL;
    std::string actor_id = get_string(env, argv[2]);
    napi_valuetype handler_type = napi_undefined;
    napi_typeof(env, argv[3], &handler_type);
    if (handler_type != napi_function) {
        napi_throw_type_error(env, NULL, "remoteActorGetRef handler must be a function");
        return NULL;
    }
    int32_t timeout_ms = 0;
    napi_get_value_int32(env, argv[4], &timeout_ms);
    request_js_state_t *state = create_request_js_state(env, argv[3]);
    if (!state)
        return NULL;
    int rc = zlink_remote_actor_get_ref(
      node, &target_node_rid, actor_id.c_str(),
      actor_lookup_callback_trampoline, state,
      static_cast<uint32_t>(timeout_ms));
    if (rc != ZLINK_SUBMIT_OK) {
        if (state->tsfn)
            (void) napi_release_threadsafe_function(state->tsfn, napi_tsfn_abort);
        return throw_last_error(env, "remoteActorGetRef failed");
    }
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

// --- Actor lifecycle handler ---

struct spot_actor_lifecycle_js_state_t
{
    spot_actor_lifecycle_js_state_t ()
      : env (NULL), join_tsfn (NULL), leave_tsfn (NULL) {}
    napi_env env;
    napi_threadsafe_function join_tsfn;
    napi_threadsafe_function leave_tsfn;
};

struct spot_actor_lifecycle_payload_t
{
    zlink_spot_actor_lifecycle_info_t info;
};

static std::mutex g_spot_actor_lifecycle_mu;
static std::unordered_map<void *, spot_actor_lifecycle_js_state_t *> g_spot_actor_lifecycle;

static void spot_actor_lifecycle_tsfn_finalize(napi_env env,
                                               void *finalize_data,
                                               void *finalize_hint)
{
    (void) env;
    (void) finalize_data;
    (void) finalize_hint;
    // Lifetime is owned by the lifecycle state map; finalize runs per-tsfn
    // and we tear the state down in release_spot_actor_lifecycle_handler.
}

static void spot_actor_lifecycle_tsfn_call_js(napi_env env,
                                              napi_value js_cb,
                                              void *context,
                                              void *data)
{
    (void) context;
    std::unique_ptr<spot_actor_lifecycle_payload_t> payload(
      static_cast<spot_actor_lifecycle_payload_t *>(data));
    if (!env || !js_cb || !payload)
        return;
    napi_value arg;
    napi_create_object(env, &arg);
    napi_set_named_property(env, arg, "previousActor",
                            create_actor_ref_value(env, payload->info.previous_actor));
    napi_set_named_property(env, arg, "currentActor",
                            create_actor_ref_value(env, payload->info.current_actor));
    napi_set_named_property(env, arg, "previousSpotRid",
                            create_routing_id_value(env, payload->info.previous_spot_rid));
    napi_set_named_property(env, arg, "currentSpotRid",
                            create_routing_id_value(env, payload->info.current_spot_rid));
    napi_value join_epoch;
    napi_create_bigint_uint64(env, payload->info.join_epoch, &join_epoch);
    napi_set_named_property(env, arg, "joinEpoch", join_epoch);
    napi_value flags;
    napi_create_uint32(env, payload->info.flags, &flags);
    napi_set_named_property(env, arg, "flags", flags);
    napi_value recv;
    napi_value this_arg;
    napi_get_undefined(env, &this_arg);
    (void) napi_call_function(env, this_arg, js_cb, 1, &arg, &recv);
}

static void spot_actor_lifecycle_dispatch(void *spot_,
                                          const zlink_spot_actor_lifecycle_info_t *info_,
                                          void *userdata_)
{
    (void) spot_;
    if (!userdata_ || !info_)
        return;
    napi_threadsafe_function tsfn = static_cast<napi_threadsafe_function>(userdata_);
    std::unique_ptr<spot_actor_lifecycle_payload_t> payload(
      new spot_actor_lifecycle_payload_t());
    payload->info = *info_;
    if (napi_call_threadsafe_function(tsfn, payload.get(), napi_tsfn_nonblocking)
        == napi_ok) {
        payload.release();
    }
}

void release_spot_actor_lifecycle_handler_slot(void *spot)
{
    spot_actor_lifecycle_js_state_t *state = NULL;
    {
        std::lock_guard<std::mutex> lock(g_spot_actor_lifecycle_mu);
        auto it = g_spot_actor_lifecycle.find(spot);
        if (it == g_spot_actor_lifecycle.end())
            return;
        state = it->second;
        g_spot_actor_lifecycle.erase(it);
    }
    if (state) {
        if (state->join_tsfn)
            (void) napi_release_threadsafe_function(state->join_tsfn, napi_tsfn_release);
        if (state->leave_tsfn)
            (void) napi_release_threadsafe_function(state->leave_tsfn, napi_tsfn_release);
        delete state;
    }
}

napi_value spot_actor_lifecycle_handler(napi_env env, napi_callback_info info)
{
    napi_value argv[3];
    size_t argc = 3;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    if (argc < 3) {
        napi_throw_type_error(env, NULL, "spotActorLifecycleHandler requires (spot, onJoin, onLeave)");
        return NULL;
    }
    void *spot = NULL;
    napi_get_value_external(env, argv[0], &spot);

    release_spot_actor_lifecycle_handler_slot(spot);

    spot_actor_lifecycle_js_state_t *state = new spot_actor_lifecycle_js_state_t();
    state->env = env;

    napi_value resource_name;
    napi_create_string_utf8(
      env, "zlink-spot-actor-lifecycle", NAPI_AUTO_LENGTH, &resource_name);

    napi_valuetype join_type = napi_undefined;
    napi_typeof(env, argv[1], &join_type);
    if (join_type == napi_function) {
        napi_status status = napi_create_threadsafe_function(
          env, argv[1], NULL, resource_name, 0, 1, NULL,
          spot_actor_lifecycle_tsfn_finalize, NULL,
          spot_actor_lifecycle_tsfn_call_js, &state->join_tsfn);
        if (status != napi_ok) {
            delete state;
            napi_throw_error(env, NULL, "spotActorLifecycleHandler setup failed");
            return NULL;
        }
        (void) napi_unref_threadsafe_function(env, state->join_tsfn);
    }
    napi_valuetype leave_type = napi_undefined;
    napi_typeof(env, argv[2], &leave_type);
    if (leave_type == napi_function) {
        napi_status status = napi_create_threadsafe_function(
          env, argv[2], NULL, resource_name, 0, 1, NULL,
          spot_actor_lifecycle_tsfn_finalize, NULL,
          spot_actor_lifecycle_tsfn_call_js, &state->leave_tsfn);
        if (status != napi_ok) {
            if (state->join_tsfn)
                (void) napi_release_threadsafe_function(state->join_tsfn, napi_tsfn_release);
            delete state;
            napi_throw_error(env, NULL, "spotActorLifecycleHandler setup failed");
            return NULL;
        }
        (void) napi_unref_threadsafe_function(env, state->leave_tsfn);
    }

    {
        std::lock_guard<std::mutex> lock(g_spot_actor_lifecycle_mu);
        g_spot_actor_lifecycle[spot] = state;
    }

    int rc = zlink_spot_actor_lifecycle_handler(
      spot,
      state->join_tsfn ? spot_actor_lifecycle_dispatch : NULL,
      state->leave_tsfn ? spot_actor_lifecycle_dispatch : NULL,
      // pass the appropriate tsfn through userdata: on_join uses join_tsfn,
      // on_leave uses leave_tsfn. The C API only allows a single userdata,
      // so register them with a small trick: pass join_tsfn as userdata if
      // only on_join is set; pass leave_tsfn if only on_leave; otherwise we
      // need two different dispatchers.
      NULL);
    // To support distinct handlers, register them sequentially with
    // separate userdata each. This single-call form is insufficient.
    if (rc != ZLINK_HANDLER_OK) {
        release_spot_actor_lifecycle_handler_slot(spot);
        return throw_last_error(env, "spotActorLifecycleHandler failed");
    }
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
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
    release_spot_routed_handler_slot(spot);
    release_spot_dispatch_event_handler_slot(spot);
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

    int rc = spot_publish_parts(spot,
                                topic.c_str(),
                                parts.data(),
                                parts.size(),
                                static_cast<zlink_send_flags_t>(flags));
    if (rc != ZLINK_SUBMIT_OK) {
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
            return throw_last_error(env, "publishNoWaitResult failed");
        if (len > 0)
            memcpy(zlink_msg_data(&parts[0]), data, len);
    } else {
        if (!build_msg_vector(env, argv[2], &parts))
            return NULL;
    }

    int rc = spot_publish_parts(
      spot, topic.c_str(), parts.data(), parts.size(),
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

napi_value spot_send_channel(napi_env env, napi_callback_info info)
{
    napi_value argv[4];
    size_t argc = 4;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *spot = NULL;
    napi_get_value_external(env, argv[0], &spot);
    std::string channel_name = get_string(env, argv[1]);
    std::vector<zlink_msg_t> parts;
    if (!build_msg_vector(env, argv[2], &parts))
        return NULL;
    int32_t flags = 0;
    napi_get_value_int32(env, argv[3], &flags);

    int rc = spot_send_channel_parts(spot,
                                     channel_name.c_str(),
                                     parts.data(),
                                     parts.size(),
                                     static_cast<zlink_send_flags_t>(flags));
    if (rc != ZLINK_SUBMIT_OK) {
        return throw_last_error(env, "spotSendChannel failed");
    }
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

napi_value spot_request_channel(napi_env env, napi_callback_info info)
{
    napi_value argv[6];
    size_t argc = 6;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    if (argc < 6) {
        napi_throw_type_error(
          env, NULL,
          "spotRequestChannel requires (spot, channelName, parts, handler, flags, timeoutMs)");
        return NULL;
    }
    void *spot = NULL;
    napi_get_value_external(env, argv[0], &spot);
    std::string channel_name = get_string(env, argv[1]);
    std::vector<zlink_msg_t> parts;
    if (!build_msg_vector(env, argv[2], &parts))
        return NULL;
    napi_valuetype handler_type = napi_undefined;
    napi_typeof(env, argv[3], &handler_type);
    if (handler_type != napi_function) {
        close_msg_vector(parts);
        napi_throw_type_error(env, NULL, "spotRequestChannel handler must be a function");
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
    int rc = spot_request_channel_parts(spot,
                                        channel_name.c_str(),
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
        return throw_last_error(env, "spotRequestChannel failed");
    }
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

napi_value spot_request_spot(napi_env env, napi_callback_info info)
{
    napi_value argv[7];
    size_t argc = 7;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    if (argc < 7) {
        napi_throw_type_error(
          env, NULL,
          "spotRequestSpot requires (spot, destNodeRid, destSpotRid, parts, handler, flags, timeoutMs)");
        return NULL;
    }
    void *spot = NULL;
    napi_get_value_external(env, argv[0], &spot);
    zlink_routing_id_t dest_node_rid;
    zlink_routing_id_t dest_spot_rid;
    if (!parse_routing_id_value(env, argv[1], &dest_node_rid))
        return NULL;
    if (!parse_routing_id_value(env, argv[2], &dest_spot_rid))
        return NULL;
    std::vector<zlink_msg_t> parts;
    if (!build_msg_vector(env, argv[3], &parts))
        return NULL;
    napi_valuetype handler_type = napi_undefined;
    napi_typeof(env, argv[4], &handler_type);
    if (handler_type != napi_function) {
        close_msg_vector(parts);
        napi_throw_type_error(env, NULL, "spotRequestSpot handler must be a function");
        return NULL;
    }
    int32_t flags = 0;
    napi_get_value_int32(env, argv[5], &flags);
    int32_t timeout_ms = 0;
    napi_get_value_int32(env, argv[6], &timeout_ms);
    request_js_state_t *state = create_request_js_state(env, argv[4]);
    if (!state) {
        close_msg_vector(parts);
        return NULL;
    }
    int rc = spot_request_spot_parts(spot, &dest_node_rid, &dest_spot_rid,
                                     parts.data(), parts.size(),
                                     request_reply_callback_trampoline, state,
                                     static_cast<zlink_send_flags_t>(flags),
                                     static_cast<uint32_t>(timeout_ms));
    if (rc != ZLINK_SUBMIT_OK) {
        if (state->tsfn) {
            (void) napi_release_threadsafe_function(state->tsfn, napi_tsfn_abort);
            state->tsfn = NULL;
        }
        return throw_last_error(env, "spotRequestSpot failed");
    }
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
}

napi_value spot_request_router(napi_env env, napi_callback_info info)
{
    napi_value argv[6];
    size_t argc = 6;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    if (argc < 6) {
        napi_throw_type_error(
          env, NULL,
          "spotRequestRouter requires (spot, peerRid, parts, handler, flags, timeoutMs)");
        return NULL;
    }
    void *spot = NULL;
    napi_get_value_external(env, argv[0], &spot);
    zlink_routing_id_t peer_rid;
    if (!parse_routing_id_value(env, argv[1], &peer_rid))
        return NULL;
    std::vector<zlink_msg_t> parts;
    if (!build_msg_vector(env, argv[2], &parts))
        return NULL;
    napi_valuetype handler_type = napi_undefined;
    napi_typeof(env, argv[3], &handler_type);
    if (handler_type != napi_function) {
        close_msg_vector(parts);
        napi_throw_type_error(env, NULL, "spotRequestRouter handler must be a function");
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
    int rc = spot_request_router_parts(spot, &peer_rid, parts.data(),
                                       parts.size(),
                                       request_reply_callback_trampoline, state,
                                       static_cast<zlink_send_flags_t>(flags),
                                       static_cast<uint32_t>(timeout_ms));
    if (rc != ZLINK_SUBMIT_OK) {
        if (state->tsfn) {
            (void) napi_release_threadsafe_function(state->tsfn, napi_tsfn_abort);
            state->tsfn = NULL;
        }
        return throw_last_error(env, "spotRequestRouter failed");
    }
    napi_value ok;
    napi_get_undefined(env, &ok);
    return ok;
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
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *spot = NULL;
    napi_get_value_external(env, argv[0], &spot);
    int32_t flags = 0;
    if (argc >= 2)
        napi_get_value_int32(env, argv[1], &flags);
    std::vector<char> topic(256, '\0');
    zlink_routing_id_t routing_id;
    std::vector<zlink_msg_t> parts;
    size_t topic_len = topic.size();

    for (;;) {
        memset(&routing_id, 0, sizeof(routing_id));
        int rc = spot_subscribe_recv_parts(spot,
                                           &routing_id,
                                           topic.data(),
                                           topic.size(),
                                           &topic_len,
                                           &parts,
                                           static_cast<zlink_recv_flags_t>(flags));
        if (rc == ZLINK_RECV_OK) {
            napi_value arr;
            napi_create_array_with_length(env, parts.size(), &arr);
            for (size_t i = 0; i < parts.size(); ++i) {
                napi_value part =
                  create_message_snapshot_value(env, &routing_id, &parts[i]);
                napi_set_element(env, arr, static_cast<uint32_t>(i), part);
            }
            close_msg_vector(parts);

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
    std::vector<zlink_msg_t> parts;
    size_t topic_len = topic.size();

    for (;;) {
        memset(&routing_id, 0, sizeof(routing_id));
        int rc = spot_subscribe_recv_parts(spot,
                                           &routing_id,
                                           topic.data(),
                                           topic.size(),
                                           &topic_len,
                                           &parts,
                                           ZLINK_RECV_FLAGS_DONTWAIT);
        if (rc == ZLINK_RECV_OK) {
            napi_value arr;
            napi_create_array_with_length(env, parts.size(), &arr);
            for (size_t i = 0; i < parts.size(); ++i) {
                napi_value part =
                  create_message_snapshot_value(env, &routing_id, &parts[i]);
                napi_set_element(env, arr, static_cast<uint32_t>(i), part);
            }
            close_msg_vector(parts);

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

napi_value spot_subscription_event(napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *spot = NULL;
    napi_get_value_external(env, argv[0], &spot);
    int32_t flags = 0;
    if (argc >= 2)
        napi_get_value_int32(env, argv[1], &flags);

    zlink_routing_id_t routing_id;
    int subscribed = 0;
    std::vector<char> topic(256, '\0');
    size_t topic_len = topic.size();
    memset(&routing_id, 0, sizeof(routing_id));
    (void) spot;
    (void) flags;
    subscribed = 0;
    topic_len = 0;
    errno = ENOTSUP;
    int rc = ZLINK_RECV_NOT_SUPPORTED;
    if (rc != ZLINK_RECV_OK)
        return throw_last_error(env, "spotSubscriptionEvent failed");

    napi_value obj;
    napi_create_object(env, &obj);
    napi_value routing_id_value = create_routing_id_value(env, routing_id);
    napi_set_named_property(env, obj, "routingId", routing_id_value);
    set_string_property(env, obj, "topic", topic.data());
    napi_value subscribed_value;
    napi_get_boolean(env, subscribed != 0, &subscribed_value);
    napi_set_named_property(env, obj, "subscribed", subscribed_value);
    return obj;
}

napi_value spot_try_subscription_event(napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    void *spot = NULL;
    napi_get_value_external(env, argv[0], &spot);

    zlink_routing_id_t routing_id;
    int subscribed = 0;
    std::vector<char> topic(256, '\0');
    size_t topic_len = topic.size();
    memset(&routing_id, 0, sizeof(routing_id));
    (void) spot;
    subscribed = 0;
    topic_len = 0;
    errno = ENOTSUP;
    int rc = ZLINK_RECV_NOT_SUPPORTED;
    if (rc != ZLINK_RECV_OK) {
        if (zlink_errno() == EAGAIN) {
            napi_value none;
            napi_get_null(env, &none);
            return none;
        }
        return throw_last_error(env, "spotSubscriptionEventNoWait failed");
    }

    napi_value obj;
    napi_create_object(env, &obj);
    napi_value routing_id_value = create_routing_id_value(env, routing_id);
    napi_set_named_property(env, obj, "routingId", routing_id_value);
    set_string_property(env, obj, "topic", topic.data());
    napi_value subscribed_value;
    napi_get_boolean(env, subscribed != 0, &subscribed_value);
    napi_set_named_property(env, obj, "subscribed", subscribed_value);
    return obj;
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
