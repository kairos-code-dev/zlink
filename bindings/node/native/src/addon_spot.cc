/* SPDX-License-Identifier: MPL-2.0 */

#include "addon_spot_api.h"
#include "addon_core_perf.h"
#include "addon_message_values.h"
#include "addon_message_parts.h"
#include "addon_spot_actor_values.h"
#include "addon_spot_request_callbacks.h"
#include "addon_tsfn_slots.h"
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <errno.h>

extern "C" int zlink_spot_drain_routed_router_ingress (void *node_);
extern "C" int zlink_spot_try_process_routed_router_parts (void *node_,
                                                             zlink_msg_t *parts_,
                                                             size_t part_count_,
                                                             int *processed_out_);

namespace
{

static napi_value create_actor_join_info_value (napi_env env, const zlink_actor_join_info_t &info);
static bool
parse_actor_join_info_value (napi_env env, napi_value value, zlink_actor_join_info_t *out);

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
static spot_send_ready_js_state_t g_spot_send_ready_slots[k_spot_send_ready_slot_count];

static napi_value create_spot_message_snapshot_value (napi_env env,
                                                      const zlink_routing_id_t *routing_id,
                                                      zlink_msg_t *msg)
{
    return create_message_snapshot_value (env, routing_id, msg);
}

static napi_value create_spot_topic_message_value (napi_env env,
                                                   const zlink_routing_id_t &routing_id,
                                                   const char *topic,
                                                   size_t topic_len,
                                                   zlink_msg_t *parts,
                                                   size_t part_count)
{
    napi_value obj;
    napi_create_object (env, &obj);

    if (part_count == 1) {
        // Hot path: SPOT subscribe receive normally carries one payload part.
        // Return the owned Buffer directly so the public TopicMessage facade
        // can be materialized without allocating a native parts array and
        // snapshot object for every message.
        napi_value data = create_message_data_buffer (env, &parts[0]);
        if (!data)
            return NULL;
        napi_set_named_property (env, obj, "data", data);
    } else {
        napi_value parts_array;
        napi_create_array_with_length (env, part_count, &parts_array);
        for (size_t i = 0; i < part_count; ++i) {
            napi_value part = create_spot_message_snapshot_value (env, &routing_id, &parts[i]);
            if (!part)
                return NULL;
            napi_set_element (env, parts_array, static_cast<uint32_t> (i), part);
        }
        napi_set_named_property (env, obj, "parts", parts_array);
    }

    napi_value topic_value;
    napi_create_string_utf8 (env, topic ? topic : "", topic ? topic_len : 0, &topic_value);
    napi_set_named_property (env, obj, "topic", topic_value);

    if (routing_id.size > 0) {
        // Hot path: unrouted SPOT subscribe traffic has no source routing id.
        // Leaving the raw field absent preserves the public null routing id
        // after TypeScript materialization and avoids a per-message property.
        napi_value rid = create_routing_id_value (env, routing_id);
        napi_set_named_property (env, obj, "routingId", rid);
    }

    return obj;
}

static bool get_optional_routing_id_property (napi_env env,
                                              napi_value obj,
                                              const char *name,
                                              zlink_routing_id_t *out)
{
    memset (out, 0, sizeof (*out));
    napi_value prop;
    bool has_prop = false;
    if (napi_has_named_property (env, obj, name, &has_prop) != napi_ok || !has_prop
        || napi_get_named_property (env, obj, name, &prop) != napi_ok) {
        return true;
    }
    napi_valuetype type = napi_undefined;
    napi_typeof (env, prop, &type);
    if (type == napi_null || type == napi_undefined)
        return true;
    return parse_routing_id_value (env, prop, out);
}

static napi_value create_actor_join_info_value (napi_env env, const zlink_actor_join_info_t &info)
{
    napi_value obj;
    napi_create_object (env, &obj);
    napi_set_named_property (env, obj, "sourceActor",
                             create_actor_ref_value (env, info.source_actor));
    napi_set_named_property (env, obj, "targetActor",
                             create_actor_ref_value (env, info.target_actor));
    napi_set_named_property (env, obj, "actor", create_actor_ref_value (env, info.source_actor));
    napi_set_named_property (env, obj, "sourceNodeRid",
                             create_routing_id_value (env, info.source_node_rid));
    napi_set_named_property (env, obj, "sourceSpotRid",
                             create_routing_id_value (env, info.source_spot_rid));
    napi_set_named_property (env, obj, "targetNodeRid",
                             create_routing_id_value (env, info.target_node_rid));
    napi_set_named_property (env, obj, "targetSpotRid",
                             create_routing_id_value (env, info.target_spot_rid));
    napi_value join_epoch;
    napi_create_bigint_uint64 (env, info.join_epoch, &join_epoch);
    napi_set_named_property (env, obj, "joinEpoch", join_epoch);
    napi_value request;
    napi_create_bigint_uint64 (
      env, static_cast<uint64_t> (reinterpret_cast<uintptr_t> (info.request)), &request);
    napi_set_named_property (env, obj, "requestHandle", request);
    set_uint32_property (env, obj, "flags", info.flags);
    return obj;
}

static bool
parse_actor_join_info_value (napi_env env, napi_value value, zlink_actor_join_info_t *out)
{
    memset (out, 0, sizeof (*out));
    napi_valuetype type = napi_undefined;
    if (napi_typeof (env, value, &type) != napi_ok || type != napi_object) {
        napi_throw_type_error (env, NULL, "join info must be an object");
        return false;
    }
    napi_value actor_value;
    if (napi_get_named_property (env, value, "sourceActor", &actor_value) == napi_ok) {
        if (!parse_actor_ref_value (env, actor_value, &out->source_actor))
            return false;
    } else if (napi_get_named_property (env, value, "actor", &actor_value) != napi_ok
               || !parse_actor_ref_value (env, actor_value, &out->source_actor)) {
        return false;
    }
    if (napi_get_named_property (env, value, "targetActor", &actor_value) == napi_ok) {
        if (!parse_actor_ref_value (env, actor_value, &out->target_actor))
            return false;
    } else {
        out->target_actor = out->source_actor;
    }
    if (!get_optional_routing_id_property (env, value, "sourceNodeRid", &out->source_node_rid)) {
        return false;
    }
    if (!get_optional_routing_id_property (env, value, "sourceSpotRid", &out->source_spot_rid)) {
        return false;
    }
    if (!get_optional_routing_id_property (env, value, "targetNodeRid", &out->target_node_rid)) {
        return false;
    }
    if (!get_optional_routing_id_property (env, value, "targetSpotRid", &out->target_spot_rid)) {
        return false;
    }
    napi_value join_epoch_value;
    bool has_join_epoch = false;
    if (napi_has_named_property (env, value, "joinEpoch", &has_join_epoch) == napi_ok
        && has_join_epoch
        && napi_get_named_property (env, value, "joinEpoch", &join_epoch_value) == napi_ok) {
        bool lossless = false;
        napi_get_value_bigint_uint64 (env, join_epoch_value, &out->join_epoch, &lossless);
    }
    napi_value request_value;
    bool has_request = false;
    if (napi_has_named_property (env, value, "requestHandle", &has_request) != napi_ok
        || !has_request
        || napi_get_named_property (env, value, "requestHandle", &request_value) != napi_ok) {
        napi_throw_type_error (env, NULL, "join info requestHandle is required");
        return false;
    }
    uint64_t request_raw = 0;
    bool lossless = false;
    napi_get_value_bigint_uint64 (env, request_value, &request_raw, &lossless);
    out->request = reinterpret_cast<void *> (static_cast<uintptr_t> (request_raw));
    napi_value flags_value;
    bool has_flags = false;
    if (napi_has_named_property (env, value, "flags", &has_flags) == napi_ok && has_flags
        && napi_get_named_property (env, value, "flags", &flags_value) == napi_ok) {
        napi_get_value_uint32 (env, flags_value, &out->flags);
    }
    return true;
}

static napi_value create_actor_route_value (napi_env env, const zlink_actor_route_t &route)
{
    napi_value obj;
    napi_create_object (env, &obj);
    napi_set_named_property (env, obj, "actor", create_actor_ref_value (env, route.actor));
    napi_set_named_property (env, obj, "currentSpotRid",
                             create_routing_id_value (env, route.current_spot_rid));
    set_uint32_property (env, obj, "currentSpotKind",
                         static_cast<uint32_t> (route.current_spot_kind));
    return obj;
}

static const size_t k_spot_dispatch_event_slot_count = 256;

struct spot_dispatch_event_js_payload_t
{
    spot_dispatch_event_js_payload_t () :
        event (0), subject_kind (0), subject_handle (0), part_count (0), routed_part_count (0),
        routed_request_seq (0)
    {
        memset (&routed_source_rid, 0, sizeof (routed_source_rid));
        memset (&routed_spot_rid, 0, sizeof (routed_spot_rid));
    }
    ~spot_dispatch_event_js_payload_t ()
    {
        if (part_count > 0)
            close_recv_parts (actor_parts.data (), part_count);
        if (routed_part_count > 0)
            close_recv_parts (routed_parts.data (), routed_part_count);
    }

    int event;
    uint32_t subject_kind;
    uint64_t subject_handle;
    std::vector<zlink_actor_recv_info_t> actor_infos;
    std::vector<zlink_msg_t> actor_parts;
    std::vector<int> actor_more;
    size_t part_count;
    zlink_routing_id_t routed_source_rid;
    zlink_routing_id_t routed_spot_rid;
    uint64_t routed_request_seq;
    std::vector<zlink_msg_t> routed_parts;
    size_t routed_part_count;
};

struct spot_dispatch_event_js_state_t
{
    spot_dispatch_event_js_state_t () :
        used (false), spot (NULL), node (NULL), env (NULL), tsfn (NULL)
    {
    }

    bool used;
    void *spot;
    void *node;
    napi_env env;
    napi_threadsafe_function tsfn;
};

static std::mutex g_spot_dispatch_event_slots_mu;
static spot_dispatch_event_js_state_t g_spot_dispatch_event_slots[k_spot_dispatch_event_slot_count];

static int router_send_spot_parts (void *router,
                                   const zlink_routing_id_t *dest_node_rid,
                                   const zlink_routing_id_t *dest_spot_rid,
                                   zlink_msg_t *parts,
                                   size_t part_count,
                                   zlink_send_flags_t flags)
{
    return submit_msg_parts (parts, part_count, [router, dest_node_rid, dest_spot_rid, flags] (
                                                  zlink_msg_t *part,
                                                  zlink_part_flag_t part_flag, bool) {
        return zlink_router_send_spot_part (router, dest_node_rid, dest_spot_rid, part, flags,
                                            part_flag);
    });
}

static int spot_reply_spot_parts (void *spot,
                                  const zlink_routing_id_t *dest_node_rid,
                                  const zlink_routing_id_t *dest_spot_rid,
                                  uint64_t request_seq,
                                  zlink_msg_t *parts,
                                  size_t part_count)
{
    return submit_msg_parts (parts, part_count, [spot, dest_node_rid, dest_spot_rid,
                                                 request_seq] (zlink_msg_t *part,
                                                               zlink_part_flag_t part_flag,
                                                               bool) {
        return zlink_spot_reply_spot_part (spot, dest_node_rid, dest_spot_rid, request_seq, part,
                                           part_flag);
    });
}

static int spot_send_spot_parts (void *spot,
                                 const zlink_routing_id_t *dest_node_rid,
                                 const zlink_routing_id_t *dest_spot_rid,
                                 zlink_msg_t *parts,
                                 size_t part_count,
                                 zlink_send_flags_t flags)
{
    return submit_msg_parts (parts, part_count, [spot, dest_node_rid, dest_spot_rid, flags] (
                                                  zlink_msg_t *part,
                                                  zlink_part_flag_t part_flag, bool) {
        return zlink_spot_send_spot_part (spot, dest_node_rid, dest_spot_rid, part, flags,
                                          part_flag);
    });
}

static int spot_reply_router_parts (void *spot,
                                    const zlink_routing_id_t *peer_rid,
                                    uint64_t request_seq,
                                    zlink_msg_t *parts,
                                    size_t part_count)
{
    return submit_msg_parts (parts, part_count, [spot, peer_rid, request_seq] (
                                                  zlink_msg_t *part,
                                                  zlink_part_flag_t part_flag, bool) {
        return zlink_spot_reply_router_part (spot, peer_rid, request_seq, part, part_flag);
    });
}

static int spot_recv_parts (void *spot,
                            zlink_routing_id_t *source_rid,
                            zlink_routing_id_t *spot_rid,
                            uint64_t *request_seq,
                            std::vector<zlink_msg_t> *parts,
                            zlink_recv_flags_t flags)
{
    const zlink_routing_id_t *source_rid_ptr = NULL;
    const zlink_routing_id_t *spot_rid_ptr = NULL;
    zlink_msg_t first_part;
    if (zlink_msg_init (&first_part) != 0)
        return ZLINK_RECV_INTERNAL_ERROR;
    zlink_part_flag_t has_more = ZLINK_PART_FINAL;

    copy_routing_id (source_rid, NULL);
    copy_routing_id (spot_rid, NULL);
    if (request_seq)
        *request_seq = 0;
    if (parts)
        parts->clear ();

    int rc = zlink_spot_recv_part (spot, &source_rid_ptr, &spot_rid_ptr, request_seq, &first_part,
                                   &has_more, flags);
    if (rc != ZLINK_RECV_OK) {
        zlink_msg_close (&first_part);
        return rc;
    }

    copy_routing_id (source_rid, source_rid_ptr);
    copy_routing_id (spot_rid, spot_rid_ptr);
    if (!append_msg_move (parts, &first_part)) {
        zlink_msg_close (&first_part);
        errno = ENOMEM;
        return ZLINK_RECV_INTERNAL_ERROR;
    }

    while (has_more) {
        const zlink_routing_id_t *next_source_rid = NULL;
        const zlink_routing_id_t *next_spot_rid = NULL;
        uint64_t next_request_seq = 0;
        zlink_msg_t next_part;
        if (zlink_msg_init (&next_part) != 0) {
            close_msg_vector (*parts);
            parts->clear ();
            return ZLINK_RECV_INTERNAL_ERROR;
        }
        zlink_part_flag_t more = ZLINK_PART_FINAL;
        rc = zlink_spot_recv_part (spot, &next_source_rid, &next_spot_rid, &next_request_seq,
                                   &next_part, &more, ZLINK_RECV_FLAGS_DONTWAIT);
        if (rc != ZLINK_RECV_OK) {
            zlink_msg_close (&next_part);
            close_msg_vector (*parts);
            parts->clear ();
            return rc;
        }
        if (!append_msg_move (parts, &next_part)) {
            zlink_msg_close (&next_part);
            close_msg_vector (*parts);
            parts->clear ();
            errno = ENOMEM;
            return ZLINK_RECV_INTERNAL_ERROR;
        }
        has_more = more;
    }

    return ZLINK_RECV_OK;
}

static int router_request_spot_parts (void *router,
                                      const zlink_routing_id_t *dest_node_rid,
                                      const zlink_routing_id_t *dest_spot_rid,
                                      zlink_msg_t *parts,
                                      size_t part_count,
                                      zlink_reply_handler_fn handler,
                                      void *userdata,
                                      zlink_send_flags_t flags,
                                      uint32_t timeout_ms)
{
    return submit_msg_parts (parts, part_count, [router, dest_node_rid, dest_spot_rid, handler,
                                                 userdata, flags, timeout_ms] (
                                                  zlink_msg_t *part,
                                                  zlink_part_flag_t part_flag, bool is_final) {
        return zlink_router_request_spot_part (
          router, dest_node_rid, dest_spot_rid, part, is_final ? handler : NULL,
          is_final ? userdata : NULL, flags, part_flag, is_final ? timeout_ms : 0u);
    });
}

static int router_reply_spot_parts (void *router,
                                    const zlink_routing_id_t *dest_node_rid,
                                    const zlink_routing_id_t *dest_spot_rid,
                                    uint64_t request_seq,
                                    zlink_msg_t *parts,
                                    size_t part_count)
{
    return submit_msg_parts (parts, part_count, [router, dest_node_rid, dest_spot_rid,
                                                 request_seq] (zlink_msg_t *part,
                                                               zlink_part_flag_t part_flag,
                                                               bool) {
        return zlink_router_reply_spot_part (router, dest_node_rid, dest_spot_rid, request_seq,
                                             part, part_flag);
    });
}

static int spot_publish_parts (
  void *spot, const char *topic, zlink_msg_t *parts, size_t part_count, zlink_send_flags_t flags)
{
    return submit_msg_parts (parts, part_count, [spot, topic, flags] (zlink_msg_t *part,
                                                                      zlink_part_flag_t part_flag,
                                                                      bool) {
        return zlink_spot_publish_part (spot, topic, part, flags, part_flag);
    });
}

static int spot_send_channel_parts (void *spot,
                                    const char *channel_name,
                                    zlink_msg_t *parts,
                                    size_t part_count,
                                    zlink_send_flags_t flags)
{
    return submit_msg_parts (parts, part_count, [spot, channel_name, flags] (
                                                  zlink_msg_t *part,
                                                  zlink_part_flag_t part_flag, bool) {
        return zlink_spot_send_channel_part (spot, channel_name, part, flags, part_flag);
    });
}

static int spot_request_channel_parts (void *spot,
                                       const char *channel_name,
                                       zlink_msg_t *parts,
                                       size_t part_count,
                                       zlink_reply_handler_fn handler,
                                       void *userdata,
                                       zlink_send_flags_t flags,
                                       uint32_t timeout_ms)
{
    return submit_msg_parts (parts, part_count, [spot, channel_name, handler, userdata, flags,
                                                 timeout_ms] (zlink_msg_t *part,
                                                              zlink_part_flag_t part_flag,
                                                              bool is_final) {
        return zlink_spot_request_channel_part (
          spot, channel_name, part, is_final ? handler : NULL, is_final ? userdata : NULL, flags,
          part_flag, is_final ? timeout_ms : 0u);
    });
}

static int spot_request_spot_parts (void *spot,
                                    const zlink_routing_id_t *dest_node_rid,
                                    const zlink_routing_id_t *dest_spot_rid,
                                    zlink_msg_t *parts,
                                    size_t part_count,
                                    zlink_reply_handler_fn handler,
                                    void *userdata,
                                    zlink_send_flags_t flags,
                                    uint32_t timeout_ms)
{
    return submit_msg_parts (parts, part_count, [spot, dest_node_rid, dest_spot_rid, handler,
                                                 userdata, flags, timeout_ms] (
                                                  zlink_msg_t *part,
                                                  zlink_part_flag_t part_flag, bool is_final) {
        return zlink_spot_request_spot_part (
          spot, dest_node_rid, dest_spot_rid, part, is_final ? handler : NULL,
          is_final ? userdata : NULL, flags, part_flag, is_final ? timeout_ms : 0u);
    });
}

static int spot_request_router_parts (void *spot,
                                      const zlink_routing_id_t *peer_rid,
                                      zlink_msg_t *parts,
                                      size_t part_count,
                                      zlink_reply_handler_fn handler,
                                      void *userdata,
                                      zlink_send_flags_t flags,
                                      uint32_t timeout_ms)
{
    return submit_msg_parts (parts, part_count, [spot, peer_rid, handler, userdata, flags,
                                                 timeout_ms] (zlink_msg_t *part,
                                                              zlink_part_flag_t part_flag,
                                                              bool is_final) {
        return zlink_spot_request_router_part (
          spot, peer_rid, part, is_final ? handler : NULL, is_final ? userdata : NULL, flags,
          part_flag, is_final ? timeout_ms : 0u);
    });
}

static int spot_subscribe_recv_parts (void *spot,
                                      zlink_routing_id_t *source_rid,
                                      char *topic,
                                      size_t topic_capacity,
                                      size_t *topic_len,
                                      std::vector<zlink_msg_t> *parts,
                                      zlink_recv_flags_t flags)
{
    const zlink_routing_id_t *source_rid_ptr = NULL;
    zlink_msg_t first_part;
    if (zlink_msg_init (&first_part) != 0)
        return ZLINK_RECV_INTERNAL_ERROR;
    zlink_part_flag_t has_more = ZLINK_PART_FINAL;

    copy_routing_id (source_rid, NULL);
    if (parts)
        parts->clear ();

    int rc = zlink_spot_subscribe_part (spot, &source_rid_ptr, topic, topic_capacity, topic_len,
                                        &first_part, &has_more, flags);
    if (rc != ZLINK_RECV_OK) {
        zlink_msg_close (&first_part);
        return rc;
    }

    copy_routing_id (source_rid, source_rid_ptr);
    if (!append_msg_move (parts, &first_part)) {
        zlink_msg_close (&first_part);
        errno = ENOMEM;
        return ZLINK_RECV_INTERNAL_ERROR;
    }

    while (has_more) {
        const zlink_routing_id_t *next_source_rid = NULL;
        zlink_msg_t next_part;
        if (zlink_msg_init (&next_part) != 0) {
            close_msg_vector (*parts);
            parts->clear ();
            return ZLINK_RECV_INTERNAL_ERROR;
        }
        zlink_part_flag_t more = ZLINK_PART_FINAL;
        size_t next_topic_len = 0;
        rc = zlink_spot_subscribe_part (spot, &next_source_rid, NULL, 0, &next_topic_len,
                                        &next_part, &more, ZLINK_RECV_FLAGS_DONTWAIT);
        if (rc != ZLINK_RECV_OK) {
            zlink_msg_close (&next_part);
            close_msg_vector (*parts);
            parts->clear ();
            return rc;
        }
        if (!append_msg_move (parts, &next_part)) {
            zlink_msg_close (&next_part);
            close_msg_vector (*parts);
            parts->clear ();
            errno = ENOMEM;
            return ZLINK_RECV_INTERNAL_ERROR;
        }
        has_more = more;
    }

    return ZLINK_RECV_OK;
}

static napi_value create_spot_routed_value (napi_env env,
                                            const zlink_routing_id_t *source_rid,
                                            const zlink_routing_id_t *spot_rid,
                                            uint64_t request_seq,
                                            zlink_msg_t *parts,
                                            size_t part_count)
{
    napi_value obj;
    napi_create_object (env, &obj);

    napi_value source_value =
      source_rid ? create_routing_id_value (env, *source_rid) : (napi_value) NULL;
    if (!source_rid || source_rid->size == 0) {
        napi_get_null (env, &source_value);
    }
    napi_set_named_property (env, obj, "sourceRid", source_value);
    napi_set_named_property (env, obj, "sourceNodeRid", source_value);

    napi_value spot_value = spot_rid ? create_routing_id_value (env, *spot_rid) : (napi_value) NULL;
    if (!spot_rid || spot_rid->size == 0) {
        napi_get_null (env, &spot_value);
    }
    napi_set_named_property (env, obj, "spotRid", spot_value);
    napi_set_named_property (env, obj, "sourceSpotRid", spot_value);

    napi_value request_seq_value;
    napi_create_bigint_uint64 (env, request_seq, &request_seq_value);
    napi_set_named_property (env, obj, "requestSeq", request_seq_value);

    napi_value parts_array;
    napi_create_array_with_length (env, part_count, &parts_array);
    for (size_t i = 0; i < part_count; ++i) {
        napi_value part = create_spot_message_snapshot_value (env, source_rid, &parts[i]);
        napi_set_element (env, parts_array, static_cast<uint32_t> (i), part);
    }
    napi_set_named_property (env, obj, "parts", parts_array);
    return obj;
}

static napi_value create_spot_routed_event_value (napi_env env,
                                                  const zlink_routing_id_t *source_rid,
                                                  const zlink_routing_id_t *spot_rid,
                                                  uint64_t request_seq,
                                                  zlink_msg_t *parts,
                                                  size_t part_count)
{
    return create_spot_routed_value (env, source_rid, spot_rid, request_seq, parts, part_count);
}

static spot_dispatch_event_js_state_t *find_spot_dispatch_event_slot_by_spot_unsafe (void *spot)
{
    return find_tsfn_slot_by_subject (g_spot_dispatch_event_slots, k_spot_dispatch_event_slot_count,
                                      &spot_dispatch_event_js_state_t::spot, spot);
}

static spot_dispatch_event_js_state_t *find_free_spot_dispatch_event_slot_unsafe ()
{
    return find_free_tsfn_slot (g_spot_dispatch_event_slots, k_spot_dispatch_event_slot_count);
}

static void reset_spot_dispatch_event_slot_unsafe (spot_dispatch_event_js_state_t *state)
{
    if (!state)
        return;
    reset_tsfn_slot_base (state);
    state->spot = NULL;
    state->node = NULL;
}

static void
spot_dispatch_event_tsfn_finalize (napi_env env, void *finalize_data, void *finalize_hint)
{
    (void) env;
    (void) finalize_hint;
    spot_dispatch_event_js_state_t *state =
      static_cast<spot_dispatch_event_js_state_t *> (finalize_data);
    if (!state)
        return;
    std::lock_guard<std::mutex> lock (g_spot_dispatch_event_slots_mu);
    reset_spot_dispatch_event_slot_unsafe (state);
}

static void
spot_dispatch_event_tsfn_call_js (napi_env env, napi_value js_cb, void *context, void *data)
{
    (void) context;
    std::unique_ptr<spot_dispatch_event_js_payload_t> payload (
      static_cast<spot_dispatch_event_js_payload_t *> (data));
    if (!env || !js_cb || !payload)
        return;

    napi_value argv[1];
    napi_value info;
    napi_create_object (env, &info);
    set_uint32_property (env, info, "event", static_cast<uint32_t> (payload->event));
    set_uint32_property (env, info, "subjectKind", payload->subject_kind);
    napi_value subject_handle;
    napi_create_bigint_uint64 (env, payload->subject_handle, &subject_handle);
    napi_set_named_property (env, info, "subjectHandle", subject_handle);
    napi_value actor_parts;
    napi_create_array_with_length (env, payload->actor_parts.size (), &actor_parts);
    for (size_t i = 0; i < payload->actor_parts.size (); ++i) {
        napi_value part = create_actor_part_value (
          env, payload->actor_infos[i], &payload->actor_parts[i], payload->actor_more[i]);
        if (!part)
            return;
        napi_set_element (env, actor_parts, static_cast<uint32_t> (i), part);
    }
	    napi_set_named_property (env, info, "actorParts", actor_parts);
	    if (payload->routed_part_count > 0) {
	        napi_value routed = create_spot_routed_value (
	          env, payload->routed_source_rid.size > 0 ? &payload->routed_source_rid : NULL,
	          payload->routed_spot_rid.size > 0 ? &payload->routed_spot_rid : NULL,
	          payload->routed_request_seq, payload->routed_parts.data (), payload->routed_part_count);
	        if (!routed)
	            return;
	        napi_set_named_property (env, info, "routed", routed);
	    }
	    argv[0] = info;
    napi_value recv;
    napi_value this_arg;
    napi_get_undefined (env, &this_arg);
    (void) napi_call_function (env, this_arg, js_cb, 1, argv, &recv);
}

static void release_spot_dispatch_event_handler_slot (void *spot)
{
    napi_threadsafe_function tsfn = NULL;
    {
        std::lock_guard<std::mutex> lock (g_spot_dispatch_event_slots_mu);
        spot_dispatch_event_js_state_t *state = find_spot_dispatch_event_slot_by_spot_unsafe (spot);
        if (!state)
            return;
        tsfn = state->tsfn;
        reset_spot_dispatch_event_slot_unsafe (state);
    }
    if (tsfn)
        (void) napi_release_threadsafe_function (tsfn, napi_tsfn_abort);
}

static void
spot_dispatch_event_dispatch (void *spot_, const zlink_spot_dispatch_info_t *info, void *userdata)
{
    (void) spot_;
    spot_dispatch_event_js_state_t *state =
      static_cast<spot_dispatch_event_js_state_t *> (userdata);
    if (!state || !info)
        return;

    napi_threadsafe_function tsfn = NULL;
    {
        std::lock_guard<std::mutex> lock (g_spot_dispatch_event_slots_mu);
        if (!state->used || !state->tsfn)
            return;
        tsfn = state->tsfn;
    }

    std::unique_ptr<spot_dispatch_event_js_payload_t> payload (
      new spot_dispatch_event_js_payload_t ());
    payload->event = static_cast<int> (info->event);
    payload->subject_kind = static_cast<uint32_t> (info->subject_kind);
    payload->subject_handle = static_cast<uint64_t> (reinterpret_cast<uintptr_t> (info->subject));
	    if (info->event == ZLINK_SPOT_DISPATCH_EVENT_ACTOR_READABLE
	        && info->subject_kind == ZLINK_SPOT_DISPATCH_SUBJECT_ACTOR && info->subject
	        && state->node) {
        for (;;) {
            zlink_actor_recv_info_t recv_info;
            zlink_msg_t part;
            zlink_part_flag_t more = ZLINK_PART_FINAL;
            if (zlink_msg_init (&part) != 0)
                break;
            int rc = zlink_spot_node_actor_recv_part (
              state->node, static_cast<const zlink_actor_ref_t *> (info->subject), &recv_info,
              &part, &more, ZLINK_RECV_FLAGS_DONTWAIT);
            if (rc != ZLINK_RECV_OK) {
                zlink_msg_close (&part);
                break;
            }
            const size_t part_index = payload->actor_parts.size ();
            payload->actor_parts.resize (part_index + 1);
            if (zlink_msg_init (&payload->actor_parts[part_index]) != 0) {
                payload->actor_parts.resize (part_index);
                zlink_msg_close (&part);
                break;
            }
            payload->part_count = part_index + 1;
            if (zlink_msg_move (&payload->actor_parts[part_index], &part) != 0) {
                zlink_msg_close (&payload->actor_parts[part_index]);
                payload->actor_parts.resize (part_index);
                payload->part_count = part_index;
                zlink_msg_close (&part);
                break;
            }
            payload->actor_infos.push_back (recv_info);
            payload->actor_more.push_back (static_cast<int> (more));
            zlink_msg_close (&part);
	            if (more == ZLINK_PART_FINAL)
	                break;
	        }
	    }
	    if (info->event == ZLINK_SPOT_DISPATCH_EVENT_ROUTED_READABLE) {
	        zlink_routing_id_t source_rid;
	        zlink_routing_id_t spot_rid;
	        uint64_t request_seq = 0;
	        std::vector<zlink_msg_t> parts;
	        int rc = spot_recv_parts (spot_, &source_rid, &spot_rid, &request_seq, &parts,
	                                  ZLINK_RECV_FLAGS_DONTWAIT);
	        if (rc == ZLINK_RECV_OK && !parts.empty ()) {
	            payload->routed_source_rid = source_rid;
	            payload->routed_spot_rid = spot_rid;
	            payload->routed_request_seq = request_seq;
	            payload->routed_parts.resize (parts.size ());
	            for (size_t i = 0; i < parts.size (); ++i) {
	                if (zlink_msg_init (&payload->routed_parts[i]) != 0)
	                    break;
	                if (zlink_msg_move (&payload->routed_parts[i], &parts[i]) != 0) {
	                    zlink_msg_close (&payload->routed_parts[i]);
	                    break;
	                }
	                payload->routed_part_count = i + 1;
	            }
	        }
	        close_msg_vector (parts);
	    }
    if (napi_call_threadsafe_function (tsfn, payload.get (), napi_tsfn_nonblocking) != napi_ok) {
        return;
    }
    (void) payload.release ();
}

spot_send_ready_js_state_t *find_spot_send_ready_slot_by_spot_unsafe (void *spot)
{
    return find_tsfn_slot_by_subject (g_spot_send_ready_slots, k_spot_send_ready_slot_count,
                                      &spot_send_ready_js_state_t::spot, spot);
}

spot_send_ready_js_state_t *find_free_spot_send_ready_slot_unsafe ()
{
    return find_free_tsfn_slot (g_spot_send_ready_slots, k_spot_send_ready_slot_count);
}

void reset_spot_send_ready_slot_unsafe (spot_send_ready_js_state_t *state)
{
    if (!state)
        return;
    reset_tsfn_slot_base (state);
    state->spot = NULL;
}

void spot_send_ready_tsfn_finalize (napi_env env, void *finalize_data, void *finalize_hint)
{
    (void) env;
    (void) finalize_hint;
    spot_send_ready_js_state_t *state = static_cast<spot_send_ready_js_state_t *> (finalize_data);
    if (!state)
        return;
    std::lock_guard<std::mutex> lock (g_spot_send_ready_slots_mu);
    reset_spot_send_ready_slot_unsafe (state);
}

void spot_send_ready_tsfn_call_js (napi_env env, napi_value js_cb, void *context, void *data)
{
    (void) context;
    std::unique_ptr<int> payload (static_cast<int *> (data));
    if (!env || !js_cb || !payload)
        return;

    napi_value recv;
    napi_value this_arg;
    napi_get_undefined (env, &this_arg);
    (void) napi_call_function (env, this_arg, js_cb, 0, NULL, &recv);
}

void release_spot_send_ready_handler_slot (void *spot)
{
    napi_threadsafe_function tsfn = NULL;
    {
        std::lock_guard<std::mutex> lock (g_spot_send_ready_slots_mu);
        spot_send_ready_js_state_t *state = find_spot_send_ready_slot_by_spot_unsafe (spot);
        if (!state)
            return;
        tsfn = state->tsfn;
        reset_spot_send_ready_slot_unsafe (state);
    }
    if (tsfn)
        (void) napi_release_threadsafe_function (tsfn, napi_tsfn_abort);
}

void spot_send_ready_dispatch (void *closure, void *)
{
    spot_send_ready_js_state_t *state = static_cast<spot_send_ready_js_state_t *> (closure);
    if (!state)
        return;

    napi_threadsafe_function tsfn = NULL;
    {
        std::lock_guard<std::mutex> lock (g_spot_send_ready_slots_mu);
        if (!state->used || !state->tsfn)
            return;
        tsfn = state->tsfn;
    }

    std::unique_ptr<int> payload (new int (1));
    if (napi_call_threadsafe_function (tsfn, payload.get (), napi_tsfn_nonblocking) != napi_ok) {
        return;
    }
    (void) payload.release ();
}

bool attach_spot_send_ready_handler (napi_env env, void *spot, napi_value handler)
{
    spot_send_ready_js_state_t *slot = NULL;
    {
        std::lock_guard<std::mutex> lock (g_spot_send_ready_slots_mu);
        if (find_spot_send_ready_slot_by_spot_unsafe (spot)) {
            napi_throw_error (env, NULL, "sendReadyHandler already attached");
            return false;
        }
        slot = find_free_spot_send_ready_slot_unsafe ();
        if (!slot) {
            napi_throw_error (env, NULL, "no free sendReadyHandler slot");
            return false;
        }
    }

    napi_value resource_name;
    napi_create_string_utf8 (env, "zlink-spot-send-ready-handler", NAPI_AUTO_LENGTH,
                             &resource_name);
    napi_threadsafe_function tsfn = NULL;
    napi_status tsfn_status = napi_create_threadsafe_function (
      env, handler, NULL, resource_name, 0, 1, slot, spot_send_ready_tsfn_finalize, slot,
      spot_send_ready_tsfn_call_js, &tsfn);
    if (tsfn_status != napi_ok) {
        napi_throw_error (env, NULL, "sendReadyHandler failed to create callback queue");
        return false;
    }

    {
        std::lock_guard<std::mutex> lock (g_spot_send_ready_slots_mu);
        slot->used = true;
        slot->spot = spot;
        slot->env = env;
        slot->tsfn = tsfn;
    }

    int rc = zlink_send_ready_handler (spot, &spot_send_ready_dispatch, slot);
    if (rc != 0) {
        release_spot_send_ready_handler_slot (spot);
        throw_last_error (env, "sendReadyHandler failed");
        return false;
    }
    return true;
}

} // namespace

napi_value router_spot_send (napi_env env, napi_callback_info info)
{
    napi_value argv[5];
    size_t argc = 5;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    if (argc < 4) {
        napi_throw_type_error (
          env, NULL, "routerSpotSend requires (socket, destNodeRid, destSpotRid, parts, flags)");
        return NULL;
    }
    void *router = NULL;
    napi_get_value_external (env, argv[0], &router);
    zlink_routing_id_t dest_node_rid;
    zlink_routing_id_t dest_spot_rid;
    if (!parse_routing_id_value (env, argv[1], &dest_node_rid))
        return NULL;
    if (!parse_routing_id_value (env, argv[2], &dest_spot_rid))
        return NULL;
    std::vector<zlink_msg_t> parts;
    if (!build_msg_vector_or_single (env, argv[3], &parts))
        return NULL;
    int32_t flags = 0;
    if (argc >= 5)
        napi_get_value_int32 (env, argv[4], &flags);
    int rc = router_send_spot_parts (router, &dest_node_rid, &dest_spot_rid, parts.data (),
                                     parts.size (), static_cast<zlink_send_flags_t> (flags));
    if (rc != ZLINK_SUBMIT_OK) {
        return throw_last_error (env, "routerSpotSend failed");
    }
    napi_value ok;
    napi_get_undefined (env, &ok);
    return ok;
}

napi_value spot_reply_spot (napi_env env, napi_callback_info info)
{
    napi_value argv[5];
    size_t argc = 5;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *spot = NULL;
    napi_get_value_external (env, argv[0], &spot);
    zlink_routing_id_t dest_node_rid;
    zlink_routing_id_t dest_spot_rid;
    if (!parse_routing_id_value (env, argv[1], &dest_node_rid))
        return NULL;
    if (!parse_routing_id_value (env, argv[2], &dest_spot_rid))
        return NULL;
    bool lossless = false;
    uint64_t request_seq = 0;
    napi_get_value_bigint_uint64 (env, argv[3], &request_seq, &lossless);
    std::vector<zlink_msg_t> parts;
    if (!build_msg_vector_or_single (env, argv[4], &parts))
        return NULL;
    int rc = spot_reply_spot_parts (spot, &dest_node_rid, &dest_spot_rid, request_seq,
                                    parts.data (), parts.size ());
    if (rc != ZLINK_SUBMIT_OK) {
        return throw_last_error (env, "spotReplySpot failed");
    }
    napi_value ok;
    napi_get_undefined (env, &ok);
    return ok;
}

napi_value spot_send_spot (napi_env env, napi_callback_info info)
{
    napi_value argv[5];
    size_t argc = 5;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *spot = NULL;
    napi_get_value_external (env, argv[0], &spot);
    zlink_routing_id_t dest_node_rid;
    zlink_routing_id_t dest_spot_rid;
    if (!parse_routing_id_value (env, argv[1], &dest_node_rid))
        return NULL;
    if (!parse_routing_id_value (env, argv[2], &dest_spot_rid))
        return NULL;
    std::vector<zlink_msg_t> parts;
    if (!build_msg_vector_or_single (env, argv[3], &parts))
        return NULL;
    int32_t flags = 0;
    if (argc >= 5)
        napi_get_value_int32 (env, argv[4], &flags);
    int rc = spot_send_spot_parts (spot, &dest_node_rid, &dest_spot_rid, parts.data (),
                                   parts.size (), static_cast<zlink_send_flags_t> (flags));
    if (rc != ZLINK_SUBMIT_OK)
        return throw_last_error (env, "spotSendToSpot failed");
    napi_value ok;
    napi_get_undefined (env, &ok);
    return ok;
}

napi_value spot_send_spot_no_wait_result (napi_env env, napi_callback_info info)
{
    napi_value argv[4];
    size_t argc = 4;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *spot = NULL;
    napi_get_value_external (env, argv[0], &spot);
    zlink_routing_id_t dest_node_rid;
    zlink_routing_id_t dest_spot_rid;
    if (!parse_routing_id_value (env, argv[1], &dest_node_rid))
        return NULL;
    if (!parse_routing_id_value (env, argv[2], &dest_spot_rid))
        return NULL;
    std::vector<zlink_msg_t> parts;
    zlink_msg_t single_part;
    bool use_single_part = false;
    bool is_array = false;
    if (napi_is_array (env, argv[3], &is_array) == napi_ok && is_array) {
        if (!build_msg_vector (env, argv[3], &parts))
            return NULL;
    } else {
        if (!init_msg_from_value (env, argv[3], &single_part))
            return NULL;
        use_single_part = true;
    }
    int rc = spot_send_spot_parts (spot, &dest_node_rid, &dest_spot_rid,
                                   use_single_part ? &single_part : parts.data (),
                                   use_single_part ? 1 : parts.size (), ZLINK_SEND_FLAGS_DONTWAIT);
    napi_value out;
    napi_create_int32 (env, rc, &out);
    return out;
}

napi_value spot_reply_router (napi_env env, napi_callback_info info)
{
    napi_value argv[4];
    size_t argc = 4;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *spot = NULL;
    napi_get_value_external (env, argv[0], &spot);
    zlink_routing_id_t peer_rid;
    if (!parse_routing_id_value (env, argv[1], &peer_rid))
        return NULL;
    bool lossless = false;
    uint64_t request_seq = 0;
    napi_get_value_bigint_uint64 (env, argv[2], &request_seq, &lossless);
    std::vector<zlink_msg_t> parts;
    if (!build_msg_vector_or_single (env, argv[3], &parts))
        return NULL;
    int rc = spot_reply_router_parts (spot, &peer_rid, request_seq, parts.data (), parts.size ());
    if (rc != ZLINK_SUBMIT_OK) {
        return throw_last_error (env, "spotReplyRouter failed");
    }
    napi_value ok;
    napi_get_undefined (env, &ok);
    return ok;
}

napi_value spot_drain_reply (napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *spot = NULL;
    napi_get_value_external (env, argv[0], &spot);
    const int drained = zlink_spot_drain_reply (spot);
    if (drained < 0)
        return throw_last_error (env, "spotDrainReply failed");
    napi_value result;
    napi_create_int32 (env, drained, &result);
    return result;
}

napi_value spot_drain_channel_reply (napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    if (argc < 2) {
        napi_throw_type_error (env, NULL,
                               "spotDrainChannelReply requires (spot, subjectHandle)");
        return NULL;
    }
    void *spot = NULL;
    napi_get_value_external (env, argv[0], &spot);
    uint64_t subject_handle = 0;
    bool lossless = false;
    napi_get_value_bigint_uint64 (env, argv[1], &subject_handle, &lossless);
    if (!lossless) {
        napi_throw_range_error (env, NULL, "subjectHandle is out of range");
        return NULL;
    }
    void *subject = reinterpret_cast<void *> (static_cast<uintptr_t> (subject_handle));
    const int drained = zlink_spot_drain_channel_reply (spot, subject);
    if (drained < 0)
        return throw_last_error (env, "spotDrainChannelReply failed");
    napi_value result;
    napi_create_int32 (env, drained, &result);
    return result;
}

napi_value spot_dispatch_event_handler (napi_env env, napi_callback_info info)
{
    napi_value argv[3];
    size_t argc = 3;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    if (argc < 3) {
        napi_throw_type_error (env, NULL,
                               "spotDispatchEventHandler requires (spot, node, handler)");
        return NULL;
    }
    void *spot = NULL;
    void *node = NULL;
    napi_get_value_external (env, argv[0], &spot);
    napi_get_value_external (env, argv[1], &node);
    napi_valuetype handler_type = napi_undefined;
    napi_typeof (env, argv[2], &handler_type);
    if (handler_type != napi_function) {
        napi_throw_type_error (env, NULL, "spotDispatchEventHandler handler must be a function");
        return NULL;
    }
    release_spot_dispatch_event_handler_slot (spot);

    spot_dispatch_event_js_state_t *state = NULL;
    {
        std::lock_guard<std::mutex> lock (g_spot_dispatch_event_slots_mu);
        state = find_free_spot_dispatch_event_slot_unsafe ();
        if (!state) {
            napi_throw_error (env, NULL, "spot dispatch handler slot exhausted");
            return NULL;
        }
        state->used = true;
        state->spot = spot;
        state->node = node;
        state->env = env;
        state->tsfn = NULL;
    }

    napi_value resource_name;
    napi_create_string_utf8 (env, "zlink-spot-dispatch-handler", NAPI_AUTO_LENGTH, &resource_name);
    napi_threadsafe_function tsfn = NULL;
    napi_status status = napi_create_threadsafe_function (
      env, argv[2], NULL, resource_name, 0, 1, state, spot_dispatch_event_tsfn_finalize, state,
      spot_dispatch_event_tsfn_call_js, &tsfn);
    if (status != napi_ok) {
        std::lock_guard<std::mutex> lock (g_spot_dispatch_event_slots_mu);
        reset_spot_dispatch_event_slot_unsafe (state);
        napi_throw_error (env, NULL, "spot dispatch handler setup failed");
        return NULL;
    }
    (void) napi_unref_threadsafe_function (env, tsfn);
    {
        std::lock_guard<std::mutex> lock (g_spot_dispatch_event_slots_mu);
        state->tsfn = tsfn;
    }

    int rc = zlink_spot_dispatch_event_handler (spot, spot_dispatch_event_dispatch, state);
    if (rc != 0) {
        release_spot_dispatch_event_handler_slot (spot);
        return throw_last_error (env, "spotDispatchEventHandler failed");
    }
    napi_value ok;
    napi_get_undefined (env, &ok);
    return ok;
}

napi_value spot_recv_routed (napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *spot = NULL;
    napi_get_value_external (env, argv[0], &spot);
    int32_t flags = 0;
    if (argc >= 2)
        napi_get_value_int32 (env, argv[1], &flags);
    zlink_routing_id_t source_rid;
    zlink_routing_id_t spot_rid;
    uint64_t request_seq = 0;
    std::vector<zlink_msg_t> parts;
    int rc = spot_recv_parts (spot, &source_rid, &spot_rid, &request_seq, &parts,
                              static_cast<zlink_recv_flags_t> (flags));
    if (rc != ZLINK_RECV_OK)
        return throw_last_error (env, "spotRecvRouted failed");
    napi_value out = create_spot_routed_event_value (env, source_rid.size > 0 ? &source_rid : NULL,
                                                     spot_rid.size > 0 ? &spot_rid : NULL,
                                                     request_seq, parts.data (), parts.size ());
    close_msg_vector (parts);
    return out;
}

napi_value spot_recv_routed_no_wait (napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *spot = NULL;
    napi_get_value_external (env, argv[0], &spot);
    zlink_routing_id_t source_rid;
    zlink_routing_id_t spot_rid;
    uint64_t request_seq = 0;
    std::vector<zlink_msg_t> parts;
    int rc = spot_recv_parts (spot, &source_rid, &spot_rid, &request_seq, &parts,
                              ZLINK_RECV_FLAGS_DONTWAIT);
    if (rc != ZLINK_RECV_OK) {
        if (zlink_errno () == EAGAIN) {
            napi_value none;
            napi_get_null (env, &none);
            return none;
        }
        return throw_last_error (env, "spotRecvRoutedNoWait failed");
    }
    napi_value out = create_spot_routed_event_value (env, source_rid.size > 0 ? &source_rid : NULL,
                                                     spot_rid.size > 0 ? &spot_rid : NULL,
                                                     request_seq, parts.data (), parts.size ());
    close_msg_vector (parts);
    return out;
}

napi_value spot_recv_routed_metric_latency (napi_env env, napi_callback_info info)
{
    napi_value argv[6];
    size_t argc = 6;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *spot = NULL;
    napi_get_value_external (env, argv[0], &spot);
    uint32_t run_id = 0;
    uint32_t msg_size = 0;
    uint32_t expected_size = 0;
    napi_get_value_uint32 (env, argv[1], &run_id);
    napi_get_value_uint32 (env, argv[2], &msg_size);
    napi_get_value_uint32 (env, argv[3], &expected_size);
    uint64_t active_start_ns = 0;
    uint64_t active_stop_ns = UINT64_MAX;
    bool lossless = false;
    napi_get_value_bigint_uint64 (env, argv[4], &active_start_ns, &lossless);
    napi_get_value_bigint_uint64 (env, argv[5], &active_stop_ns, &lossless);

    const zlink_routing_id_t *source_rid = NULL;
    const zlink_routing_id_t *spot_rid = NULL;
    uint64_t request_seq = 0;
    zlink_msg_t part;
    if (zlink_msg_init (&part) != 0)
        return throw_last_error (env, "spotRecvRoutedMetricLatency failed");
    zlink_part_flag_t has_more = ZLINK_PART_FINAL;

    int rc = zlink_spot_recv_part (spot, &source_rid, &spot_rid, &request_seq, &part, &has_more,
                                   ZLINK_RECV_FLAGS_DONTWAIT);
    if (rc != ZLINK_RECV_OK) {
        zlink_msg_close (&part);
        if (zlink_errno () == EAGAIN) {
            napi_value none;
            napi_get_null (env, &none);
            return none;
        }
        return throw_last_error (env, "spotRecvRoutedMetricLatency failed");
    }

    if (has_more != ZLINK_PART_FINAL) {
        std::vector<zlink_msg_t> rest;
        int collect_rc = collect_recv_parts (spot, &part, has_more, &rest);
        close_msg_vector (rest);
        if (collect_rc != ZLINK_RECV_OK)
            return throw_last_error (env, "spotRecvRoutedMetricLatency failed");
        napi_throw_error (env, NULL, "spotRecvRoutedMetricLatency requires single-part messages");
        return NULL;
    }

    uint64_t sent_ts_ns = 0;
    const uint64_t received_at_ns = perf_now_ns ();
    const bool valid = source_rid && source_rid->size > 0 && spot_rid && spot_rid->size > 0
                       && request_seq == 0 && zlink_msg_size (&part) == expected_size
                       && perf_decode_payload_header (&part, 1, run_id, msg_size, &sent_ts_ns)
                       && received_at_ns >= active_start_ns && received_at_ns <= active_stop_ns
                       && received_at_ns >= sent_ts_ns;
    zlink_msg_close (&part);

    if (!valid) {
        napi_value rejected;
        napi_get_boolean (env, false, &rejected);
        return rejected;
    }

    napi_value latency;
    napi_create_double (env, static_cast<double> (received_at_ns - sent_ts_ns), &latency);
    return latency;
}

napi_value spot_actor_join_recv (napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *spot = NULL;
    napi_get_value_external (env, argv[0], &spot);
    int32_t flags = 0;
    if (argc >= 2)
        napi_get_value_int32 (env, argv[1], &flags);
    zlink_actor_join_info_t join_info;
    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    int rc = zlink_spot_actor_join_recv (spot, &join_info, &parts, &part_count,
                                         static_cast<zlink_recv_flags_t> (flags));
    if (rc != ZLINK_RECV_OK) {
        if ((flags & ZLINK_RECV_FLAGS_DONTWAIT) && zlink_errno () == EAGAIN) {
            napi_value none;
            napi_get_null (env, &none);
            return none;
        }
        return throw_last_error (env, "spotActorJoinRecv failed");
    }
    napi_value out;
    napi_create_object (env, &out);
    napi_set_named_property (env, out, "info", create_actor_join_info_value (env, join_info));
    napi_value parts_array;
    napi_create_array_with_length (env, part_count, &parts_array);
    for (size_t i = 0; i < part_count; ++i) {
        napi_value part = create_spot_message_snapshot_value (env, NULL, &parts[i]);
        napi_set_element (env, parts_array, static_cast<uint32_t> (i), part);
    }
    napi_set_named_property (env, out, "parts", parts_array);
    napi_value message;
    if (part_count > 0) {
        napi_get_element (env, parts_array, 0, &message);
    } else {
        zlink_msg_t empty;
        if (zlink_msg_init (&empty) != 0) {
            zlink_multipart_close (parts, part_count);
            return throw_last_error (env, "spotActorJoinRecv failed");
        }
        message = create_spot_message_snapshot_value (env, NULL, &empty);
        zlink_msg_close (&empty);
    }
    napi_set_named_property (env, out, "message", message);
    zlink_multipart_close (parts, part_count);
    return out;
}

static napi_value
create_spot_actor_lifecycle_info_value (napi_env env, const zlink_spot_actor_lifecycle_info_t &info)
{
    napi_value obj;
    napi_create_object (env, &obj);
    napi_set_named_property (env, obj, "previousActor",
                             create_actor_ref_value (env, info.previous_actor));
    napi_set_named_property (env, obj, "currentActor",
                             create_actor_ref_value (env, info.current_actor));
    napi_set_named_property (env, obj, "previousSpotRid",
                             create_routing_id_value (env, info.previous_spot_rid));
    napi_set_named_property (env, obj, "currentSpotRid",
                             create_routing_id_value (env, info.current_spot_rid));
    napi_value epoch;
    napi_create_bigint_uint64 (env, info.join_epoch, &epoch);
    napi_set_named_property (env, obj, "joinEpoch", epoch);
    set_uint32_property (env, obj, "flags", info.flags);
    return obj;
}

napi_value spot_recv_actor_lifecycle (napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *spot = NULL;
    napi_get_value_external (env, argv[0], &spot);
    int32_t flags = 0;
    if (argc >= 2)
        napi_get_value_int32 (env, argv[1], &flags);

    zlink_spot_actor_lifecycle_event_t event;
    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    int rc = zlink_spot_recv_actor_lifecycle_with_request (
      spot, &event, &parts, &part_count, static_cast<zlink_recv_flags_t> (flags));
    if (rc != ZLINK_RECV_OK) {
        if ((flags & ZLINK_RECV_FLAGS_DONTWAIT) && zlink_errno () == EAGAIN) {
            napi_value none;
            napi_get_null (env, &none);
            return none;
        }
        return throw_last_error (env, "spotRecvActorLifecycle failed");
    }

    napi_value out;
    napi_create_object (env, &out);
    set_uint32_property (env, out, "kind", static_cast<uint32_t> (event.kind));
    napi_set_named_property (env, out, "info",
                             create_spot_actor_lifecycle_info_value (env, event.info));
    napi_value parts_array;
    napi_create_array_with_length (env, part_count, &parts_array);
    for (size_t i = 0; i < part_count; ++i) {
        napi_value part = create_spot_message_snapshot_value (env, NULL, &parts[i]);
        napi_set_element (env, parts_array, static_cast<uint32_t> (i), part);
    }
    napi_set_named_property (env, out, "parts", parts_array);
    napi_value message;
    if (part_count > 0) {
        napi_get_element (env, parts_array, 0, &message);
    } else {
        zlink_msg_t empty;
        if (zlink_msg_init (&empty) != 0) {
            zlink_multipart_close (parts, part_count);
            return throw_last_error (env, "spotRecvActorLifecycle failed");
        }
        message = create_spot_message_snapshot_value (env, NULL, &empty);
        zlink_msg_close (&empty);
    }
    napi_set_named_property (env, out, "message", message);
    zlink_multipart_close (parts, part_count);
    return out;
}

napi_value spot_actor_join_reply (napi_env env, napi_callback_info info)
{
    napi_value argv[4];
    size_t argc = 4;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *spot = NULL;
    napi_get_value_external (env, argv[0], &spot);
    zlink_actor_join_info_t join_info;
    if (!parse_actor_join_info_value (env, argv[1], &join_info))
        return NULL;
    int32_t join_result_code = 0;
    napi_get_value_int32 (env, argv[2], &join_result_code);
    std::vector<zlink_msg_t> parts;
    if (!build_msg_vector_or_single (env, argv[3], &parts))
        return NULL;
    int rc = zlink_spot_actor_join_reply (spot, &join_info, join_result_code,
                                          parts.empty () ? NULL : parts.data (), parts.size ());
    if (rc != ZLINK_SUBMIT_OK) {
        close_msg_vector (parts);
        return throw_last_error (env, "spotActorJoinReply failed");
    }
    napi_value ok;
    napi_get_undefined (env, &ok);
    return ok;
}

napi_value spot_actors (napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *spot = NULL;
    napi_get_value_external (env, argv[0], &spot);
    size_t count = 0;
    int rc = zlink_spot_actors (spot, NULL, &count);
    if (rc != ZLINK_CONFIG_OK)
        return throw_last_error (env, "spotActors failed");
    napi_value arr;
    napi_create_array_with_length (env, count, &arr);
    if (count == 0)
        return arr;
    std::vector<zlink_actor_ref_t> entries (count);
    rc = zlink_spot_actors (spot, entries.data (), &count);
    if (rc != ZLINK_CONFIG_OK)
        return throw_last_error (env, "spotActors failed");
    for (size_t i = 0; i < count; ++i) {
        napi_set_element (env, arr, static_cast<uint32_t> (i),
                          create_actor_ref_value (env, entries[i]));
    }
    return arr;
}

napi_value router_spot_request (napi_env env, napi_callback_info info)
{
    napi_value argv[7];
    size_t argc = 7;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    if (argc < 7) {
        napi_throw_type_error (env, NULL,
                               "routerSpotRequest requires (socket, destNodeRid, destSpotRid, "
                               "parts, handler, flags, timeoutMs)");
        return NULL;
    }
    void *router = NULL;
    napi_get_value_external (env, argv[0], &router);
    zlink_routing_id_t dest_node_rid;
    zlink_routing_id_t dest_spot_rid;
    if (!parse_routing_id_value (env, argv[1], &dest_node_rid))
        return NULL;
    if (!parse_routing_id_value (env, argv[2], &dest_spot_rid))
        return NULL;
    std::vector<zlink_msg_t> parts;
    if (!build_msg_vector_or_single (env, argv[3], &parts))
        return NULL;
    napi_valuetype handler_type = napi_undefined;
    napi_typeof (env, argv[4], &handler_type);
    if (handler_type != napi_function) {
        close_msg_vector (parts);
        napi_throw_type_error (env, NULL, "routerSpotRequest handler must be a function");
        return NULL;
    }
    int32_t flags = 0;
    napi_get_value_int32 (env, argv[5], &flags);
    int32_t timeout_ms = 0;
    napi_get_value_int32 (env, argv[6], &timeout_ms);
    request_js_state_t *state = create_request_js_state (env, argv[4]);
    if (!state) {
        close_msg_vector (parts);
        return NULL;
    }
    int rc = router_request_spot_parts (router, &dest_node_rid, &dest_spot_rid, parts.data (),
                                        parts.size (), request_reply_callback_trampoline, state,
                                        static_cast<zlink_send_flags_t> (flags),
                                        static_cast<uint32_t> (timeout_ms));
    if (rc != ZLINK_SUBMIT_OK) {
        abort_request_js_state (state);
        return throw_last_error (env, "routerSpotRequest failed");
    }
    napi_value ok;
    napi_get_undefined (env, &ok);
    return ok;
}

napi_value router_spot_reply (napi_env env, napi_callback_info info)
{
    napi_value argv[5];
    size_t argc = 5;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    if (argc < 5) {
        napi_throw_type_error (
          env, NULL,
          "routerSpotReply requires (socket, destNodeRid, destSpotRid, requestSeq, parts)");
        return NULL;
    }
    void *router = NULL;
    napi_get_value_external (env, argv[0], &router);
    zlink_routing_id_t dest_node_rid;
    zlink_routing_id_t dest_spot_rid;
    if (!parse_routing_id_value (env, argv[1], &dest_node_rid))
        return NULL;
    if (!parse_routing_id_value (env, argv[2], &dest_spot_rid))
        return NULL;
    uint64_t request_seq = 0;
    bool lossless = false;
    if (napi_get_value_bigint_uint64 (env, argv[3], &request_seq, &lossless) != napi_ok
        || !lossless) {
        napi_throw_type_error (env, NULL, "requestSeq must be uint64");
        return NULL;
    }
    std::vector<zlink_msg_t> parts;
    if (!build_msg_vector_or_single (env, argv[4], &parts))
        return NULL;
    int rc = router_reply_spot_parts (router, &dest_node_rid, &dest_spot_rid, request_seq,
                                      parts.data (), parts.size ());
    if (rc != ZLINK_SUBMIT_OK) {
        return throw_last_error (env, "routerSpotReply failed");
    }
    napi_value ok;
    napi_get_undefined (env, &ok);
    return ok;
}


napi_value spot_node_new (napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *ctx = NULL;
    napi_get_value_external (env, argv[0], &ctx);
    zlink_spot_node_options_t options;
    zlink_spot_node_options_t *options_ptr = NULL;
    memset (&options, 0, sizeof (options));
    if (argc >= 2) {
        napi_valuetype type = napi_undefined;
        if (napi_typeof (env, argv[1], &type) == napi_ok && type != napi_undefined
            && type != napi_null) {
            napi_value mode_value;
            bool has_mode = false;
            if (napi_has_named_property (env, argv[1], "mode", &has_mode) == napi_ok && has_mode
                && napi_get_named_property (env, argv[1], "mode", &mode_value) == napi_ok) {
                uint32_t raw_mode = 0;
                napi_get_value_uint32 (env, mode_value, &raw_mode);
                options.mode = static_cast<zlink_spot_node_mode_t> (raw_mode);
                options_ptr = &options;
            }
        }
    }
    void *node = zlink_spot_node_new (ctx, options_ptr);
    if (!node)
        return throw_last_error (env, "spot_node_new failed");
    napi_value ext;
    napi_create_external (env, node, NULL, NULL, &ext);
    return ext;
}

napi_value spot_node_destroy (napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *node = NULL;
    napi_get_value_external (env, argv[0], &node);
    void *tmp = node;
    int rc = zlink_spot_node_destroy (&tmp);
    if (rc != 0)
        return throw_last_error (env, "spot_node_destroy failed");
    napi_value ok;
    napi_get_undefined (env, &ok);
    return ok;
}

napi_value spot_node_set_pub_bind (napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *node = NULL;
    napi_get_value_external (env, argv[0], &node);
    std::string ep = get_string (env, argv[1]);
    int rc = zlink_spot_node_set_pub_bind (node, ep.c_str ());
    if (rc != 0)
        return throw_last_error (env, "spot_node_set_pub_bind failed");
    napi_value ok;
    napi_get_undefined (env, &ok);
    return ok;
}

napi_value spot_node_set_router_bind (napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *node = NULL;
    napi_get_value_external (env, argv[0], &node);
    std::string ep = get_string (env, argv[1]);
    int rc = zlink_spot_node_set_router_bind (node, ep.c_str ());
    if (rc != 0)
        return throw_last_error (env, "spot_node_set_router_bind failed");
    napi_value ok;
    napi_get_undefined (env, &ok);
    return ok;
}

napi_value spot_node_set_pub_routing_id (napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *node = NULL;
    napi_get_value_external (env, argv[0], &node);
    zlink_routing_id_t routing_id;
    if (!parse_routing_id_value (env, argv[1], &routing_id))
        return NULL;
    int rc = zlink_spot_node_set_pub_routing_id (node, routing_id.data, routing_id.size);
    if (rc != 0)
        return throw_last_error (env, "spot_node_set_pub_routing_id failed");
    napi_value ok;
    napi_get_undefined (env, &ok);
    return ok;
}

napi_value spot_node_set_sub_routing_id (napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *node = NULL;
    napi_get_value_external (env, argv[0], &node);
    zlink_routing_id_t routing_id;
    if (!parse_routing_id_value (env, argv[1], &routing_id))
        return NULL;
    int rc = zlink_spot_node_set_sub_routing_id (node, routing_id.data, routing_id.size);
    if (rc != 0)
        return throw_last_error (env, "spot_node_set_sub_routing_id failed");
    napi_value ok;
    napi_get_undefined (env, &ok);
    return ok;
}

napi_value spot_node_connect_peer (napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *node = NULL;
    napi_get_value_external (env, argv[0], &node);
    std::string ep = get_string (env, argv[1]);
    int rc = zlink_spot_node_connect_peer (node, ep.c_str ());
    if (rc != 0)
        return throw_last_error (env, "spot_node_connect_peer failed");
    napi_value ok;
    napi_get_undefined (env, &ok);
    return ok;
}

napi_value spot_node_connect_peer_rid (napi_env env, napi_callback_info info)
{
    napi_value argv[3];
    size_t argc = 3;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *node = NULL;
    napi_get_value_external (env, argv[0], &node);
    zlink_routing_id_t peer_rid;
    if (!parse_routing_id_value (env, argv[1], &peer_rid))
        return NULL;
    std::string ep = get_string (env, argv[2]);
    int rc = zlink_spot_node_connect_peer_rid (node, &peer_rid, ep.c_str ());
    if (rc != 0)
        return throw_last_error (env, "spot_node_connect_peer_rid failed");
    napi_value ok;
    napi_get_undefined (env, &ok);
    return ok;
}

napi_value spot_node_disconnect_peer (napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *node = NULL;
    napi_get_value_external (env, argv[0], &node);
    std::string ep = get_string (env, argv[1]);
    int rc = zlink_spot_node_disconnect_peer (node, ep.c_str ());
    if (rc != 0)
        return throw_last_error (env, "spot_node_disconnect_peer failed");
    napi_value ok;
    napi_get_undefined (env, &ok);
    return ok;
}

napi_value spot_node_disconnect_peer_rid (napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *node = NULL;
    napi_get_value_external (env, argv[0], &node);
    zlink_routing_id_t peer_rid;
    if (!parse_routing_id_value (env, argv[1], &peer_rid))
        return NULL;
    int rc = zlink_spot_node_disconnect_peer_rid (node, &peer_rid);
    if (rc != 0)
        return throw_last_error (env, "spot_node_disconnect_peer_rid failed");
    napi_value ok;
    napi_get_undefined (env, &ok);
    return ok;
}


napi_value spot_route_bridge_new (napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *ctx = NULL;
    void *node = NULL;
    napi_get_value_external (env, argv[0], &ctx);
    napi_get_value_external (env, argv[1], &node);
    void *bridge = zlink_spot_route_bridge_new (ctx, node, NULL);
    if (!bridge)
        return throw_last_error (env, "spotRouteBridgeNew failed");
    napi_value ext;
    napi_create_external (env, bridge, NULL, NULL, &ext);
    return ext;
}

napi_value spot_route_bridge_close (napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *bridge = NULL;
    napi_get_value_external (env, argv[0], &bridge);
    int rc = zlink_spot_route_bridge_close (bridge);
    if (rc != 0)
        return throw_last_error (env, "spotRouteBridgeClose failed");
    napi_value ok;
    napi_get_undefined (env, &ok);
    return ok;
}

napi_value spot_route_bridge_attach_router_channel (napi_env env, napi_callback_info info)
{
    napi_value argv[4];
    size_t argc = 4;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *bridge = NULL;
    void *router = NULL;
    napi_get_value_external (env, argv[0], &bridge);
    std::string channel_name = get_string (env, argv[1]);
    napi_get_value_external (env, argv[2], &router);
    zlink_spot_route_bridge_endpoint_options_t options {};
    options.struct_size = sizeof (options);
    options.capabilities = ZLINK_SPOT_ROUTE_BRIDGE_ROUTE_ONLY;
    if (argc >= 4) {
        uint32_t capabilities = 0;
        napi_get_value_uint32 (env, argv[3], &capabilities);
        options.capabilities = capabilities;
    }
    int rc = zlink_spot_route_bridge_attach_router_channel (bridge, channel_name.c_str (),
                                                            router, &options);
    if (rc != 0)
        return throw_last_error (env, "spotRouteBridgeAttachRouterChannel failed");
    napi_value ok;
    napi_get_undefined (env, &ok);
    return ok;
}

napi_value spot_route_bridge_send (napi_env env, napi_callback_info info)
{
    napi_value argv[6];
    size_t argc = 6;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *bridge = NULL;
    napi_get_value_external (env, argv[0], &bridge);
    std::string channel_name = get_string (env, argv[1]);
    zlink_routing_id_t target_node;
    if (!parse_routing_id_value (env, argv[2], &target_node))
        return NULL;
    zlink_routing_id_t target_spot;
    if (!parse_routing_id_value (env, argv[3], &target_spot))
        return NULL;
    std::vector<zlink_msg_t> parts;
    if (!build_msg_vector_or_single (env, argv[4], &parts))
        return NULL;
    int32_t flags = 0;
    napi_get_value_int32 (env, argv[5], &flags);
    int rc = zlink_spot_route_bridge_send (bridge, channel_name.c_str (), &target_node,
                                           &target_spot,
                                           parts.data (), parts.size (),
                                           static_cast<zlink_send_flags_t> (flags));
    if (rc != 0) {
        close_msg_vector (parts);
        return throw_last_error (env, "spotRouteBridgeSend failed");
    }
    napi_value ok;
    napi_get_boolean (env, true, &ok);
    return ok;
}

napi_value spot_route_bridge_request (napi_env env, napi_callback_info info)
{
    napi_value argv[8];
    size_t argc = 8;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    if (argc < 8) {
        napi_throw_type_error (
          env, NULL,
          "spotRouteBridgeRequest requires (bridge, channelName, targetNodeRid, targetSpotRid, "
          "parts, handler, flags, timeoutMs)");
        return NULL;
    }
    void *bridge = NULL;
    napi_get_value_external (env, argv[0], &bridge);
    std::string channel_name = get_string (env, argv[1]);
    zlink_routing_id_t target_node;
    if (!parse_routing_id_value (env, argv[2], &target_node))
        return NULL;
    zlink_routing_id_t target_spot;
    if (!parse_routing_id_value (env, argv[3], &target_spot))
        return NULL;
    std::vector<zlink_msg_t> parts;
    if (!build_msg_vector_or_single (env, argv[4], &parts))
        return NULL;
    napi_valuetype handler_type = napi_undefined;
    napi_typeof (env, argv[5], &handler_type);
    if (handler_type != napi_function) {
        close_msg_vector (parts);
        napi_throw_type_error (env, NULL, "spotRouteBridgeRequest handler must be a function");
        return NULL;
    }
    int32_t flags = 0;
    napi_get_value_int32 (env, argv[6], &flags);
    int32_t timeout_ms = 0;
    napi_get_value_int32 (env, argv[7], &timeout_ms);
    request_js_state_t *state = create_request_js_state (env, argv[5]);
    if (!state) {
        close_msg_vector (parts);
        return NULL;
    }
    int rc = zlink_spot_route_bridge_request (
      bridge, channel_name.c_str (), &target_node, &target_spot, parts.data (), parts.size (),
      request_reply_callback_trampoline, state, static_cast<zlink_send_flags_t> (flags),
      static_cast<uint32_t> (timeout_ms));
    if (rc != 0) {
        abort_request_js_state (state);
        return throw_last_error (env, "spotRouteBridgeRequest failed");
    }
    napi_value ok;
    napi_get_undefined (env, &ok);
    return ok;
}

napi_value spot_route_bridge_handle_router_received (napi_env env, napi_callback_info info)
{
    napi_value argv[5];
    size_t argc = 5;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    if (argc < 5) {
        napi_throw_type_error (
          env, NULL,
          "spotRouteBridgeHandleRouterReceived requires (bridge, channelName, sourceNodeRid, "
          "requestSeq, parts)");
        return NULL;
    }
    void *bridge = NULL;
    napi_get_value_external (env, argv[0], &bridge);
    std::string channel_name = get_string (env, argv[1]);
    zlink_routing_id_t source_node_rid;
    if (!parse_routing_id_value (env, argv[2], &source_node_rid))
        return NULL;
    uint64_t request_seq = 0;
    bool lossless = false;
    if (napi_get_value_bigint_uint64 (env, argv[3], &request_seq, &lossless) != napi_ok
        || !lossless)
        return NULL;
    std::vector<zlink_msg_t> parts;
    if (!build_msg_vector_or_single (env, argv[4], &parts))
        return NULL;
    bool handled = false;
    int rc = zlink_spot_route_bridge_handle_router_received (
      bridge, channel_name.c_str (), &source_node_rid, request_seq, parts.data (), parts.size (),
      &handled);
    if (rc != 0) {
        close_msg_vector (parts);
        return throw_last_error (env, "spotRouteBridgeHandleRouterReceived failed");
    }
    if (!handled)
        close_msg_vector (parts);
    napi_value out;
    napi_get_boolean (env, handled, &out);
    return out;
}

napi_value spot_route_bridge_drain (napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *bridge = NULL;
    napi_get_value_external (env, argv[0], &bridge);
    int rc = zlink_spot_route_bridge_drain (bridge);
    if (rc < 0)
        return throw_last_error (env, "spotRouteBridgeDrain failed");
    napi_value out;
    napi_create_int32 (env, rc, &out);
    return out;
}

napi_value spot_node_publisher_new (napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *node = NULL;
    napi_get_value_external (env, argv[0], &node);
    void *publisher = zlink_spot_node_publisher_new (node);
    if (!publisher)
        return throw_last_error (env, "spotNodePublisherNew failed");
    napi_value ext;
    napi_create_external (env, publisher, NULL, NULL, &ext);
    return ext;
}

napi_value spot_node_publisher_publish (napi_env env, napi_callback_info info)
{
    napi_value argv[4];
    size_t argc = 4;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *publisher = NULL;
    napi_get_value_external (env, argv[0], &publisher);
    std::string topic = get_string (env, argv[1]);
    std::vector<zlink_msg_t> parts;
    if (!build_msg_vector_or_single (env, argv[2], &parts))
        return NULL;
    int32_t flags = 0;
    napi_get_value_int32 (env, argv[3], &flags);
    int rc = zlink_spot_node_publisher_publish (publisher, topic.c_str (), parts.data (),
                                                parts.size (),
                                                static_cast<zlink_send_flags_t> (flags));
    if (rc != 0) {
        close_msg_vector (parts);
        return throw_last_error (env, "spotNodePublisherPublish failed");
    }
    napi_value ok;
    napi_get_boolean (env, true, &ok);
    return ok;
}

napi_value spot_node_publisher_close (napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *publisher = NULL;
    napi_get_value_external (env, argv[0], &publisher);
    int rc = zlink_spot_node_publisher_close (publisher);
    if (rc != 0)
        return throw_last_error (env, "spotNodePublisherClose failed");
    napi_value ok;
    napi_get_undefined (env, &ok);
    return ok;
}

napi_value spot_node_process_routed_router (napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *node = NULL;
    napi_get_value_external (env, argv[0], &node);
    int rc = zlink_spot_drain_routed_router_ingress (node);
    if (rc != 0)
        return throw_last_error (env, "spotNodeProcessRoutedRouter failed");
    napi_value ok;
    napi_get_undefined (env, &ok);
    return ok;
}

napi_value spot_node_try_process_routed_router_parts (napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    if (argc < 2) {
        napi_throw_type_error (env, NULL,
                               "spotNodeTryProcessRoutedRouterParts requires (node, parts)");
        return NULL;
    }
    void *node = NULL;
    napi_get_value_external (env, argv[0], &node);
    std::vector<zlink_msg_t> parts;
    if (!build_msg_vector_or_single (env, argv[1], &parts))
        return NULL;
    int processed = 0;
    int rc = zlink_spot_try_process_routed_router_parts (
      node, parts.data (), parts.size (), &processed);
    if (rc != 0) {
        close_msg_vector (parts);
        return throw_last_error (env, "spotNodeTryProcessRoutedRouterParts failed");
    }
    if (!processed)
        close_msg_vector (parts);
    napi_value out;
    napi_get_boolean (env, processed != 0, &out);
    return out;
}

napi_value spot_node_set_tls_server (napi_env env, napi_callback_info info)
{
    napi_value argv[4];
    size_t argc = 4;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *node = NULL;
    napi_get_value_external (env, argv[0], &node);
    std::string cert = get_string (env, argv[1]);
    std::string key = get_string (env, argv[2]);
    int32_t require_client = 0;
    if (argc >= 4)
        napi_get_value_int32 (env, argv[3], &require_client);
    int rc = zlink_set_tls_server (node, cert.c_str (), key.c_str (), require_client);
    if (rc != 0)
        return throw_last_error (env, "spot_node_set_tls_server failed");
    napi_value ok;
    napi_get_undefined (env, &ok);
    return ok;
}

napi_value spot_node_set_tls_client (napi_env env, napi_callback_info info)
{
    napi_value argv[4];
    size_t argc = 4;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *node = NULL;
    napi_get_value_external (env, argv[0], &node);
    std::string ca = get_string (env, argv[1]);
    std::string host = get_string (env, argv[2]);
    int32_t trust = 0;
    napi_get_value_int32 (env, argv[3], &trust);
    int rc = zlink_set_tls_client (node, ca.c_str (), host.c_str (), trust);
    if (rc != 0)
        return throw_last_error (env, "spot_node_set_tls_client failed");
    napi_value ok;
    napi_get_undefined (env, &ok);
    return ok;
}

napi_value spot_node_setsockopt (napi_env env, napi_callback_info info)
{
    napi_value argv[3];
    size_t argc = 3;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    if (argc < 3) {
        napi_throw_type_error (env, NULL, "spotNodeSetOption expects node, option, value");
        return NULL;
    }

    void *node = NULL;
    napi_get_value_external (env, argv[0], &node);
    int32_t opt = 0;
    if (napi_get_value_int32 (env, argv[1], &opt) != napi_ok) {
        napi_throw_type_error (env, NULL, "option must be an integer");
        return NULL;
    }
    void *data = NULL;
    size_t len = 0;
    if (napi_get_buffer_info (env, argv[2], &data, &len) != napi_ok) {
        napi_throw_type_error (env, NULL, "value must be Buffer");
        return NULL;
    }

    int rc =
      zlink_set_spot_node_option (node, static_cast<zlink_spot_node_option_t> (opt), data, len);
    if (rc != 0)
        return throw_last_error (env, "spotNodeSetOption failed");

    napi_value ok;
    napi_get_undefined (env, &ok);
    return ok;
}

napi_value spot_node_getsockopt (napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    if (argc < 2) {
        napi_throw_type_error (env, NULL, "spotNodeGetOption expects node, option");
        return NULL;
    }

    void *node = NULL;
    napi_get_value_external (env, argv[0], &node);
    int32_t opt = 0;
    if (napi_get_value_int32 (env, argv[1], &opt) != napi_ok) {
        napi_throw_type_error (env, NULL, "option must be an integer");
        return NULL;
    }

    size_t len = sizeof (int);
    void *data = NULL;
    napi_value buf;
    napi_create_buffer (env, len, &data, &buf);
    int rc =
      zlink_get_spot_node_option (node, static_cast<zlink_spot_node_option_t> (opt), data, &len);
    if (rc != 0)
        return throw_last_error (env, "spotNodeGetOption failed");
    return buf;
}

napi_value spot_node_entry_spot (napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    if (argc < 1) {
        napi_throw_type_error (env, NULL, "spotNodeEntrySpot expects node");
        return NULL;
    }

    void *node = NULL;
    napi_get_value_external (env, argv[0], &node);
    void *spot = NULL;
    zlink_config_result_t rc = zlink_spot_node_entry_spot (node, &spot);
    if (rc != ZLINK_CONFIG_OK)
        return throw_last_error (env, "spotNodeEntrySpot failed");

    napi_value out;
    napi_create_external (env, spot, NULL, NULL, &out);
    return out;
}

napi_value spot_node_spot_lookup (napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    if (argc < 2) {
        napi_throw_type_error (env, NULL, "spotNodeSpotLookup expects node, spotRid");
        return NULL;
    }

    void *node = NULL;
    napi_get_value_external (env, argv[0], &node);
    zlink_routing_id_t spot_rid;
    if (!parse_routing_id_value (env, argv[1], &spot_rid))
        return NULL;

    void *spot = NULL;
    zlink_config_result_t rc = zlink_spot_node_spot_lookup (node, &spot_rid, &spot);
    if (rc == ZLINK_CONFIG_NOT_FOUND) {
        napi_value none;
        napi_get_null (env, &none);
        return none;
    }
    if (rc != ZLINK_CONFIG_OK)
        return throw_last_error (env, "spotNodeSpotLookup failed");

    napi_value out;
    napi_create_external (env, spot, NULL, NULL, &out);
    return out;
}

napi_value spot_node_spot_get_or_new (napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    if (argc < 2) {
        napi_throw_type_error (env, NULL, "spotNodeSpotGetOrNew expects node, spotRid");
        return NULL;
    }

    void *node = NULL;
    napi_get_value_external (env, argv[0], &node);
    zlink_routing_id_t spot_rid;
    if (!parse_routing_id_value (env, argv[1], &spot_rid))
        return NULL;

    void *spot = NULL;
    uint32_t created = 0;
    zlink_config_result_t rc = zlink_spot_node_spot_get_or_new (node, &spot_rid, &spot, &created);
    if (rc != ZLINK_CONFIG_OK)
        return throw_last_error (env, "spotNodeSpotGetOrNew failed");

    napi_value out;
    napi_create_object (env, &out);
    napi_value native_spot;
    napi_create_external (env, spot, NULL, NULL, &native_spot);
    napi_set_named_property (env, out, "spot", native_spot);
    napi_value created_value;
    napi_get_boolean (env, created != 0, &created_value);
    napi_set_named_property (env, out, "created", created_value);
    return out;
}

napi_value spot_set_option (napi_env env, napi_callback_info info)
{
    napi_value argv[3];
    size_t argc = 3;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    if (argc < 3) {
        napi_throw_type_error (env, NULL, "spotSetOption expects spot, option, value");
        return NULL;
    }

    void *spot = NULL;
    napi_get_value_external (env, argv[0], &spot);
    int32_t opt = 0;
    if (napi_get_value_int32 (env, argv[1], &opt) != napi_ok) {
        napi_throw_type_error (env, NULL, "option must be an integer");
        return NULL;
    }
    void *data = NULL;
    size_t len = 0;
    if (napi_get_buffer_info (env, argv[2], &data, &len) != napi_ok) {
        napi_throw_type_error (env, NULL, "value must be Buffer");
        return NULL;
    }

    int rc = zlink_set_spot_option (spot, static_cast<zlink_spot_option_t> (opt), data, len);
    if (rc != 0)
        return throw_last_error (env, "spotSetOption failed");

    napi_value ok;
    napi_get_undefined (env, &ok);
    return ok;
}

napi_value spot_get_option (napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    if (argc < 2) {
        napi_throw_type_error (env, NULL, "spotGetOption expects spot, option");
        return NULL;
    }

    void *spot = NULL;
    napi_get_value_external (env, argv[0], &spot);
    int32_t opt = 0;
    if (napi_get_value_int32 (env, argv[1], &opt) != napi_ok) {
        napi_throw_type_error (env, NULL, "option must be an integer");
        return NULL;
    }

    size_t len = sizeof (int);
    void *data = NULL;
    napi_value buf;
    napi_create_buffer (env, len, &data, &buf);
    int rc = zlink_get_spot_option (spot, static_cast<zlink_spot_option_t> (opt), data, &len);
    if (rc != 0)
        return throw_last_error (env, "spotGetOption failed");
    return buf;
}

napi_value spot_new (napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *node = NULL;
    napi_get_value_external (env, argv[0], &node);
    void *spot = zlink_spot_new (node);
    if (!spot)
        return throw_last_error (env, "spot_new failed");
    napi_value ext;
    napi_create_external (env, spot, NULL, NULL, &ext);
    return ext;
}

napi_value spot_destroy (napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *spot = NULL;
    napi_get_value_external (env, argv[0], &spot);
    release_spot_send_ready_handler_slot (spot);
    release_spot_dispatch_event_handler_slot (spot);
    void *tmp = spot;
    int rc = zlink_spot_destroy (&tmp);
    if (rc != 0)
        return throw_last_error (env, "spot_destroy failed");
    napi_value ok;
    napi_get_undefined (env, &ok);
    return ok;
}

napi_value spot_publish (napi_env env, napi_callback_info info)
{
    napi_value argv[4];
    size_t argc = 4;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *spot = NULL;
    napi_get_value_external (env, argv[0], &spot);
    char topic_stack[256];
    std::string topic_heap;
    const char *topic =
      get_c_string_arg (env, argv[1], topic_stack, sizeof (topic_stack), &topic_heap);
    int32_t flags = 0;
    napi_get_value_int32 (env, argv[3], &flags);

    bool is_buffer = false;
    napi_is_buffer (env, argv[2], &is_buffer);
    std::vector<zlink_msg_t> parts;
    if (is_buffer) {
        void *data = NULL;
        size_t len = 0;
        if (napi_get_buffer_info (env, argv[2], &data, &len) != napi_ok) {
            napi_throw_type_error (env, NULL, "payload must be Buffer");
            return NULL;
        }
        parts.resize (1);
        if (zlink_msg_init_size (&parts[0], len) != 0)
            return throw_last_error (env, "spot_publish failed");
        if (len > 0)
            memcpy (zlink_msg_data (&parts[0]), data, len);
    } else {
        if (!build_msg_vector_or_single (env, argv[2], &parts))
            return NULL;
    }

    int rc = spot_publish_parts (spot, topic, parts.data (), parts.size (),
                                 static_cast<zlink_send_flags_t> (flags));
    if (rc != ZLINK_SUBMIT_OK) {
        return throw_last_error (env, "spot_publish failed");
    }

    napi_value ok;
    napi_get_undefined (env, &ok);
    return ok;
}

napi_value spot_send_channel (napi_env env, napi_callback_info info)
{
    napi_value argv[4];
    size_t argc = 4;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *spot = NULL;
    napi_get_value_external (env, argv[0], &spot);
    char channel_stack[256];
    std::string channel_heap;
    const char *channel_name =
      get_c_string_arg (env, argv[1], channel_stack, sizeof (channel_stack), &channel_heap);
    std::vector<zlink_msg_t> parts;
    if (!build_msg_vector_or_single (env, argv[2], &parts))
        return NULL;
    int32_t flags = 0;
    napi_get_value_int32 (env, argv[3], &flags);

    int rc = spot_send_channel_parts (spot, channel_name, parts.data (), parts.size (),
                                      static_cast<zlink_send_flags_t> (flags));
    if (rc != ZLINK_SUBMIT_OK) {
        return throw_last_error (env, "spotSendChannel failed");
    }
    napi_value ok;
    napi_get_undefined (env, &ok);
    return ok;
}

napi_value spot_request_channel (napi_env env, napi_callback_info info)
{
    napi_value argv[6];
    size_t argc = 6;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    if (argc < 6) {
        napi_throw_type_error (
          env, NULL,
          "spotRequestChannel requires (spot, channelName, parts, handler, flags, timeoutMs)");
        return NULL;
    }
    void *spot = NULL;
    napi_get_value_external (env, argv[0], &spot);
    char channel_stack[256];
    std::string channel_heap;
    const char *channel_name =
      get_c_string_arg (env, argv[1], channel_stack, sizeof (channel_stack), &channel_heap);
    std::vector<zlink_msg_t> parts;
    if (!build_msg_vector_or_single (env, argv[2], &parts))
        return NULL;
    napi_valuetype handler_type = napi_undefined;
    napi_typeof (env, argv[3], &handler_type);
    if (handler_type != napi_function) {
        close_msg_vector (parts);
        napi_throw_type_error (env, NULL, "spotRequestChannel handler must be a function");
        return NULL;
    }
    int32_t flags = 0;
    napi_get_value_int32 (env, argv[4], &flags);
    int32_t timeout_ms = 0;
    napi_get_value_int32 (env, argv[5], &timeout_ms);
    request_js_state_t *state = create_request_js_state (env, argv[3]);
    if (!state) {
        close_msg_vector (parts);
        return NULL;
    }
    int rc = spot_request_channel_parts (
      spot, channel_name, parts.data (), parts.size (), request_reply_callback_trampoline, state,
      static_cast<zlink_send_flags_t> (flags), static_cast<uint32_t> (timeout_ms));
    if (rc != ZLINK_SUBMIT_OK) {
        abort_request_js_state (state);
        return throw_last_error (env, "spotRequestChannel failed");
    }
    napi_value ok;
    napi_get_undefined (env, &ok);
    return ok;
}

napi_value spot_request_spot (napi_env env, napi_callback_info info)
{
    napi_value argv[7];
    size_t argc = 7;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    if (argc < 7) {
        napi_throw_type_error (env, NULL,
                               "spotRequestSpot requires (spot, destNodeRid, destSpotRid, parts, "
                               "handler, flags, timeoutMs)");
        return NULL;
    }
    void *spot = NULL;
    napi_get_value_external (env, argv[0], &spot);
    zlink_routing_id_t dest_node_rid;
    zlink_routing_id_t dest_spot_rid;
    if (!parse_routing_id_value (env, argv[1], &dest_node_rid))
        return NULL;
    if (!parse_routing_id_value (env, argv[2], &dest_spot_rid))
        return NULL;
    std::vector<zlink_msg_t> parts;
    if (!build_msg_vector_or_single (env, argv[3], &parts))
        return NULL;
    napi_valuetype handler_type = napi_undefined;
    napi_typeof (env, argv[4], &handler_type);
    if (handler_type != napi_function) {
        close_msg_vector (parts);
        napi_throw_type_error (env, NULL, "spotRequestSpot handler must be a function");
        return NULL;
    }
    int32_t flags = 0;
    napi_get_value_int32 (env, argv[5], &flags);
    int32_t timeout_ms = 0;
    napi_get_value_int32 (env, argv[6], &timeout_ms);
    request_js_state_t *state = create_request_js_state (env, argv[4]);
    if (!state) {
        close_msg_vector (parts);
        return NULL;
    }
    int rc = spot_request_spot_parts (spot, &dest_node_rid, &dest_spot_rid, parts.data (),
                                      parts.size (), request_reply_callback_trampoline, state,
                                      static_cast<zlink_send_flags_t> (flags),
                                      static_cast<uint32_t> (timeout_ms));
    if (rc != ZLINK_SUBMIT_OK) {
        abort_request_js_state (state);
        return throw_last_error (env, "spotRequestSpot failed");
    }
    napi_value ok;
    napi_get_undefined (env, &ok);
    return ok;
}

napi_value spot_request_router (napi_env env, napi_callback_info info)
{
    napi_value argv[6];
    size_t argc = 6;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    if (argc < 6) {
        napi_throw_type_error (
          env, NULL,
          "spotRequestRouter requires (spot, peerRid, parts, handler, flags, timeoutMs)");
        return NULL;
    }
    void *spot = NULL;
    napi_get_value_external (env, argv[0], &spot);
    zlink_routing_id_t peer_rid;
    if (!parse_routing_id_value (env, argv[1], &peer_rid))
        return NULL;
    std::vector<zlink_msg_t> parts;
    if (!build_msg_vector_or_single (env, argv[2], &parts))
        return NULL;
    napi_valuetype handler_type = napi_undefined;
    napi_typeof (env, argv[3], &handler_type);
    if (handler_type != napi_function) {
        close_msg_vector (parts);
        napi_throw_type_error (env, NULL, "spotRequestRouter handler must be a function");
        return NULL;
    }
    int32_t flags = 0;
    napi_get_value_int32 (env, argv[4], &flags);
    int32_t timeout_ms = 0;
    napi_get_value_int32 (env, argv[5], &timeout_ms);
    request_js_state_t *state = create_request_js_state (env, argv[3]);
    if (!state) {
        close_msg_vector (parts);
        return NULL;
    }
    int rc = spot_request_router_parts (
      spot, &peer_rid, parts.data (), parts.size (), request_reply_callback_trampoline, state,
      static_cast<zlink_send_flags_t> (flags), static_cast<uint32_t> (timeout_ms));
    if (rc != ZLINK_SUBMIT_OK) {
        abort_request_js_state (state);
        return throw_last_error (env, "spotRequestRouter failed");
    }
    napi_value ok;
    napi_get_undefined (env, &ok);
    return ok;
}

napi_value spot_send_ready_handler (napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    if (argc < 2) {
        napi_throw_type_error (env, NULL, "sendReadyHandler requires (spot, handler)");
        return NULL;
    }
    void *spot = NULL;
    napi_get_value_external (env, argv[0], &spot);
    napi_valuetype handler_type = napi_undefined;
    napi_typeof (env, argv[1], &handler_type);
    if (handler_type != napi_function) {
        napi_throw_type_error (env, NULL, "sendReadyHandler handler must be a function");
        return NULL;
    }
    if (!attach_spot_send_ready_handler (env, spot, argv[1]))
        return NULL;
    napi_value ok;
    napi_get_undefined (env, &ok);
    return ok;
}

napi_value spot_subscribe (napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *spot = NULL;
    napi_get_value_external (env, argv[0], &spot);
    std::string topic = get_string (env, argv[1]);
    int rc = zlink_set_subscription (spot, topic.c_str ());
    if (rc != 0)
        return throw_last_error (env, "spot_subscribe failed");
    napi_value ok;
    napi_get_undefined (env, &ok);
    return ok;
}

napi_value spot_unsubscribe (napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *spot = NULL;
    napi_get_value_external (env, argv[0], &spot);
    std::string topic = get_string (env, argv[1]);
    int rc = zlink_unset_subscription (spot, topic.c_str ());
    if (rc != 0)
        return throw_last_error (env, "spot_unsubscribe failed");
    napi_value ok;
    napi_get_undefined (env, &ok);
    return ok;
}

napi_value spot_recv (napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *spot = NULL;
    napi_get_value_external (env, argv[0], &spot);
    int32_t flags = 0;
    if (argc >= 2)
        napi_get_value_int32 (env, argv[1], &flags);
    std::vector<char> topic (256, '\0');
    zlink_routing_id_t routing_id;
    std::vector<zlink_msg_t> parts;
    size_t topic_len = topic.size ();

    for (;;) {
        memset (&routing_id, 0, sizeof (routing_id));
        int rc =
          spot_subscribe_recv_parts (spot, &routing_id, topic.data (), topic.size (), &topic_len,
                                     &parts, static_cast<zlink_recv_flags_t> (flags));
        if (rc == ZLINK_RECV_OK) {
            napi_value obj = create_spot_topic_message_value (env, routing_id, topic.data (),
                                                              topic_len, parts.data (),
                                                              parts.size ());
            close_msg_vector (parts);
            if (!obj)
                return NULL;
            return obj;
        }
        if (zlink_errno () != EMSGSIZE)
            return throw_last_error (env, "spot_recv failed");
        topic.assign (topic_len > 0 ? topic_len : 1, '\0');
    }
}

napi_value spot_try_recv (napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *spot = NULL;
    napi_get_value_external (env, argv[0], &spot);

    std::vector<char> topic (256, '\0');
    zlink_routing_id_t routing_id;
    std::vector<zlink_msg_t> parts;
    size_t topic_len = topic.size ();

    for (;;) {
        memset (&routing_id, 0, sizeof (routing_id));
        int rc = spot_subscribe_recv_parts (spot, &routing_id, topic.data (), topic.size (),
                                            &topic_len, &parts, ZLINK_RECV_FLAGS_DONTWAIT);
        if (rc == ZLINK_RECV_OK) {
            napi_value obj = create_spot_topic_message_value (env, routing_id, topic.data (),
                                                              topic_len, parts.data (),
                                                              parts.size ());
            close_msg_vector (parts);
            if (!obj)
                return NULL;
            return obj;
        }
        const int err = zlink_errno ();
        if (err == EAGAIN) {
            napi_value none;
            napi_get_null (env, &none);
            return none;
        }
        if (err != EMSGSIZE)
            return throw_last_error (env, "spot_try_recv failed");
        topic.assign (topic_len > 0 ? topic_len : 1, '\0');
    }
}
