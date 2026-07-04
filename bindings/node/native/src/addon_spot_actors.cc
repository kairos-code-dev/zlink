/* SPDX-License-Identifier: MPL-2.0 */

#include "addon_spot_api.h"
#include "addon_message_parts.h"
#include "addon_spot_actor_values.h"
#include "addon_spot_request_callbacks.h"

#include <errno.h>
#include <vector>

napi_value spot_node_actors (napi_env env, napi_callback_info info)
{
    napi_value argv[1];
    size_t argc = 1;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *node = NULL;
    napi_get_value_external (env, argv[0], &node);
    size_t count = 0;
    int rc = zlink_spot_node_actors (node, NULL, &count);
    if (rc != 0)
        return throw_last_error (env, "spotNodeActors failed");
    napi_value arr;
    napi_create_array_with_length (env, count, &arr);
    if (count == 0)
        return arr;
    std::vector<zlink_spot_node_actor_entry_t> entries (count);
    rc = zlink_spot_node_actors (node, entries.data (), &count);
    if (rc != 0)
        return throw_last_error (env, "spotNodeActors failed");
    for (size_t i = 0; i < count; ++i) {
        napi_set_element (env, arr, static_cast<uint32_t> (i),
                          create_spot_node_actor_entry_value (env, entries[i]));
    }
    return arr;
}

napi_value spot_node_actor_new (napi_env env, napi_callback_info info)
{
    napi_value argv[3];
    size_t argc = 3;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *node = NULL;
    napi_get_value_external (env, argv[0], &node);
    std::string actor_id = get_string (env, argv[1]);
    std::vector<zlink_msg_t> parts;
    napi_valuetype request_type = napi_undefined;
    if (argc >= 3)
        napi_typeof (env, argv[2], &request_type);
    if (request_type != napi_undefined && request_type != napi_null) {
        if (!build_msg_vector_or_single (env, argv[2], &parts))
            return NULL;
    }
    zlink_actor_ref_t ref;
    int rc = zlink_spot_node_actor_new_with_request (
      node, actor_id.c_str (), parts.empty () ? NULL : parts.data (), parts.size (), &ref);
    if (rc != ZLINK_CONFIG_OK) {
        close_msg_vector (parts);
        return throw_last_error (env, "spotNodeActorNew failed");
    }
    return create_actor_ref_value (env, ref);
}

napi_value spot_node_actor_destroy (napi_env env, napi_callback_info info)
{
    napi_value argv[4];
    size_t argc = 4;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *node = NULL;
    napi_get_value_external (env, argv[0], &node);
    zlink_actor_ref_t ref;
    if (!parse_actor_ref_value (env, argv[1], &ref))
        return NULL;
    napi_valuetype handler_type = napi_undefined;
    if (argc >= 3)
        napi_typeof (env, argv[2], &handler_type);
    int32_t timeout_ms = 0;
    if (argc >= 4)
        napi_get_value_int32 (env, argv[3], &timeout_ms);
    if (handler_type == napi_function) {
        request_js_state_t *state = create_request_js_state (env, argv[2]);
        if (!state)
            return NULL;
        int rc = zlink_spot_node_actor_destroy (node, &ref, request_reply_callback_trampoline,
                                                state, static_cast<uint32_t> (timeout_ms));
        if (rc != ZLINK_SUBMIT_OK) {
            abort_request_js_state (state);
            return throw_last_error (env, "spotNodeActorDestroy failed");
        }
        napi_value ok;
        napi_get_undefined (env, &ok);
        return ok;
    }
    sync_request_state_t state;
    int rc = zlink_spot_node_actor_destroy (node, &ref, sync_request_callback, &state,
                                            static_cast<uint32_t> (timeout_ms));
    if (rc != ZLINK_SUBMIT_OK)
        return throw_last_error (env, "spotNodeActorDestroy failed");
    zlink_request_result_t request_rc = wait_sync_request (&state);
    if (request_rc != ZLINK_REQUEST_OK)
        return throw_last_error (env, "spotNodeActorDestroy failed");
    napi_value ok;
    napi_get_undefined (env, &ok);
    return ok;
}

napi_value spot_node_actor_lookup (napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *node = NULL;
    napi_get_value_external (env, argv[0], &node);
    std::string actor_id = get_string (env, argv[1]);
    zlink_actor_ref_t ref;
    int rc = zlink_spot_node_actor_lookup (node, actor_id.c_str (), &ref);
    if (rc != ZLINK_CONFIG_OK)
        return throw_last_error (env, "spotNodeActorLookup failed");
    return create_actor_ref_value (env, ref);
}

napi_value spot_node_actor_join_spot (napi_env env, napi_callback_info info)
{
    napi_value argv[8];
    size_t argc = 8;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *node = NULL;
    napi_get_value_external (env, argv[0], &node);
    zlink_actor_ref_t ref;
    zlink_routing_id_t node_rid;
    zlink_routing_id_t spot_rid;
    if (!parse_actor_ref_value (env, argv[1], &ref))
        return NULL;
    if (!parse_routing_id_value (env, argv[2], &node_rid))
        return NULL;
    if (!parse_routing_id_value (env, argv[3], &spot_rid))
        return NULL;
    std::vector<zlink_msg_t> parts;
    if (!build_msg_vector_or_single (env, argv[4], &parts))
        return NULL;
    napi_valuetype handler_type = napi_undefined;
    napi_typeof (env, argv[5], &handler_type);
    if (handler_type != napi_function) {
        close_msg_vector (parts);
        napi_throw_type_error (env, NULL, "actor join handler must be a function");
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
    int rc = zlink_spot_node_actor_join_spot (
      node, &ref, &node_rid, &spot_rid, parts.empty () ? NULL : parts.data (), parts.size (),
      actor_join_callback_trampoline, state, static_cast<zlink_send_flags_t> (flags),
      static_cast<uint32_t> (timeout_ms));
    if (rc != ZLINK_SUBMIT_OK) {
        abort_request_js_state (state);
        return throw_last_error (env, "spotNodeActorJoinSpot failed");
    }
    napi_value ok;
    napi_get_undefined (env, &ok);
    return ok;
}

napi_value spot_node_actor_join_entry_spot (napi_env env, napi_callback_info info)
{
    napi_value argv[7];
    size_t argc = 7;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *node = NULL;
    napi_get_value_external (env, argv[0], &node);
    zlink_actor_ref_t ref;
    zlink_routing_id_t node_rid;
    if (!parse_actor_ref_value (env, argv[1], &ref))
        return NULL;
    if (!parse_routing_id_value (env, argv[2], &node_rid))
        return NULL;
    std::vector<zlink_msg_t> parts;
    if (!build_msg_vector_or_single (env, argv[3], &parts))
        return NULL;
    napi_valuetype handler_type = napi_undefined;
    napi_typeof (env, argv[4], &handler_type);
    if (handler_type != napi_function) {
        close_msg_vector (parts);
        napi_throw_type_error (env, NULL, "actor entry spot join handler must be a function");
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
    int rc = zlink_spot_node_actor_join_entry_spot (node, &ref, &node_rid,
                                                    parts.empty () ? NULL : parts.data (),
                                                    parts.size (),
                                                    actor_join_entry_spot_callback_trampoline,
                                                    state,
                                                    static_cast<zlink_send_flags_t> (flags),
                                                    static_cast<uint32_t> (timeout_ms));
    if (rc != ZLINK_SUBMIT_OK) {
        abort_request_js_state (state);
        close_msg_vector (parts);
        return throw_last_error (env, "spotNodeActorJoinEntrySpot failed");
    }
    napi_value ok;
    napi_get_undefined (env, &ok);
    return ok;
}

napi_value spot_node_actor_leave_spot (napi_env env, napi_callback_info info)
{
    napi_value argv[5];
    size_t argc = 5;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *node = NULL;
    napi_get_value_external (env, argv[0], &node);
    zlink_actor_ref_t ref;
    zlink_routing_id_t spot_rid;
    if (!parse_actor_ref_value (env, argv[1], &ref))
        return NULL;
    if (!parse_routing_id_value (env, argv[2], &spot_rid))
        return NULL;
    napi_valuetype handler_type = napi_undefined;
    if (argc >= 4)
        napi_typeof (env, argv[3], &handler_type);
    int32_t timeout_ms = 0;
    if (argc >= 5)
        napi_get_value_int32 (env, argv[4], &timeout_ms);
    if (handler_type == napi_function) {
        request_js_state_t *state = create_request_js_state (env, argv[3]);
        if (!state)
            return NULL;
        int rc = zlink_spot_node_actor_leave_spot (node, &ref, &spot_rid,
                                                   request_reply_callback_trampoline, state,
                                                   static_cast<uint32_t> (timeout_ms));
        if (rc != ZLINK_SUBMIT_OK) {
            abort_request_js_state (state);
            return throw_last_error (env, "spotNodeActorLeaveSpot failed");
        }
        napi_value ok;
        napi_get_undefined (env, &ok);
        return ok;
    }
    sync_request_state_t state;
    int rc = zlink_spot_node_actor_leave_spot (node, &ref, &spot_rid, sync_request_callback, &state,
                                               static_cast<uint32_t> (timeout_ms));
    if (rc != ZLINK_SUBMIT_OK)
        return throw_last_error (env, "spotNodeActorLeaveSpot failed");
    zlink_request_result_t request_rc = wait_sync_request (&state);
    if (request_rc != ZLINK_REQUEST_OK)
        return throw_last_error (env, "spotNodeActorLeaveSpot failed");
    napi_value ok;
    napi_get_undefined (env, &ok);
    return ok;
}

napi_value spot_node_actor_recv_part (napi_env env, napi_callback_info info)
{
    napi_value argv[3];
    size_t argc = 3;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *node = NULL;
    napi_get_value_external (env, argv[0], &node);
    zlink_actor_ref_t ref;
    if (!parse_actor_ref_value (env, argv[1], &ref))
        return NULL;
    int32_t flags = 0;
    if (argc >= 3)
        napi_get_value_int32 (env, argv[2], &flags);
    zlink_actor_recv_info_t recv_info;
    zlink_msg_t part;
    zlink_part_flag_t more = ZLINK_PART_FINAL;
    if (zlink_msg_init (&part) != 0)
        return throw_last_error (env, "spotNodeActorRecvPart failed");
    int rc = zlink_spot_node_actor_recv_part (node, &ref, &recv_info, &part, &more,
                                              static_cast<zlink_recv_flags_t> (flags));
    if (rc != ZLINK_RECV_OK) {
        zlink_msg_close (&part);
        if ((flags & ZLINK_RECV_FLAGS_DONTWAIT) && zlink_errno () == EAGAIN) {
            napi_value none;
            napi_get_null (env, &none);
            return none;
        }
        return throw_last_error (env, "spotNodeActorRecvPart failed");
    }
    napi_value out = create_actor_part_value (env, recv_info, &part, more);
    zlink_msg_close (&part);
    return out;
}

napi_value spot_node_actor_send_bound_session_msg (napi_env env, napi_callback_info info)
{
    napi_value argv[4];
    size_t argc = 4;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *node = NULL;
    napi_get_value_external (env, argv[0], &node);
    zlink_actor_ref_t ref;
    if (!parse_actor_ref_value (env, argv[1], &ref))
        return NULL;
    std::vector<zlink_msg_t> parts;
    if (!build_msg_vector_or_single (env, argv[2], &parts))
        return NULL;
    if (parts.size () != 1) {
        close_msg_vector (parts);
        napi_throw_range_error (env, NULL, "actor send message must be single part");
        return NULL;
    }
    int32_t flags = 0;
    if (argc >= 4)
        napi_get_value_int32 (env, argv[3], &flags);
    int rc = zlink_spot_node_actor_send_bound_session_msg (node, &ref, &parts[0],
                                                           static_cast<zlink_send_flags_t> (flags));
    if (rc != ZLINK_SUBMIT_OK) {
        close_msg_vector (parts);
        return throw_last_error (env, "spotNodeActorSendBoundSessionMsg failed");
    }
    napi_value ok;
    napi_get_undefined (env, &ok);
    return ok;
}

napi_value spot_node_send_to_actor (napi_env env, napi_callback_info info)
{
    napi_value argv[5];
    size_t argc = 5;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *node = NULL;
    napi_get_value_external (env, argv[0], &node);
    zlink_actor_ref_t ref;
    if (!parse_actor_ref_value (env, argv[1], &ref))
        return NULL;
    std::vector<zlink_msg_t> parts;
    if (!build_msg_vector_or_single (env, argv[2], &parts))
        return NULL;
    int32_t flags = 0;
    if (argc >= 4)
        napi_get_value_int32 (env, argv[3], &flags);
    int32_t timeout_ms = 0;
    if (argc >= 5)
        napi_get_value_int32 (env, argv[4], &timeout_ms);
    sync_request_state_t state;
    int rc = zlink_spot_node_send_to_actor (
      node, &ref, parts.empty () ? NULL : parts.data (), parts.size (), sync_request_callback,
      &state, static_cast<zlink_send_flags_t> (flags), static_cast<uint32_t> (timeout_ms));
    if (rc != ZLINK_SUBMIT_OK) {
        close_msg_vector (parts);
        return throw_last_error (env, "spotNodeSendToActor failed");
    }
    zlink_request_result_t request_rc = wait_sync_request (&state);
    if (request_rc != ZLINK_REQUEST_OK)
        return throw_last_error (env, "spotNodeSendToActor failed");
    napi_value ok;
    napi_get_undefined (env, &ok);
    return ok;
}

napi_value spot_node_request_to_actor (napi_env env, napi_callback_info info)
{
    napi_value argv[6];
    size_t argc = 6;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *node = NULL;
    napi_get_value_external (env, argv[0], &node);
    zlink_actor_ref_t ref;
    if (!parse_actor_ref_value (env, argv[1], &ref))
        return NULL;
    std::vector<zlink_msg_t> parts;
    if (!build_msg_vector_or_single (env, argv[2], &parts))
        return NULL;
    napi_valuetype handler_type = napi_undefined;
    napi_typeof (env, argv[3], &handler_type);
    if (handler_type != napi_function) {
        close_msg_vector (parts);
        napi_throw_type_error (env, NULL, "requestToActor handler must be a function");
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
    int rc = zlink_spot_node_request_to_actor (
      node, &ref, parts.empty () ? NULL : parts.data (), parts.size (),
      request_reply_callback_trampoline, state, static_cast<zlink_send_flags_t> (flags),
      static_cast<uint32_t> (timeout_ms));
    if (rc != ZLINK_SUBMIT_OK) {
        abort_request_js_state (state);
        close_msg_vector (parts);
        return throw_last_error (env, "spotNodeRequestToActor failed");
    }
    napi_value ok;
    napi_get_undefined (env, &ok);
    return ok;
}

napi_value spot_node_actor_reply_no_bind (napi_env env, napi_callback_info info)
{
    napi_value argv[4];
    size_t argc = 4;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *node = NULL;
    napi_get_value_external (env, argv[0], &node);
    zlink_actor_recv_info_t recv_info;
    if (!parse_actor_recv_info_value (env, argv[1], &recv_info))
        return NULL;
    std::vector<zlink_msg_t> parts;
    if (!build_msg_vector_or_single (env, argv[2], &parts))
        return NULL;
    int32_t result = ZLINK_REQUEST_OK;
    if (argc >= 4)
        napi_get_value_int32 (env, argv[3], &result);
    int rc = zlink_spot_node_actor_reply_no_bind (
      node, &recv_info, parts.empty () ? NULL : parts.data (), parts.size (),
      static_cast<zlink_request_result_t> (result));
    if (rc != ZLINK_SUBMIT_OK) {
        close_msg_vector (parts);
        return throw_last_error (env, "spotNodeActorReplyNoBind failed");
    }
    napi_value ok;
    napi_get_undefined (env, &ok);
    return ok;
}

napi_value spot_node_actor_bind_remote_session (napi_env env, napi_callback_info info)
{
    napi_value argv[4];
    size_t argc = 4;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *node = NULL;
    napi_get_value_external (env, argv[0], &node);
    zlink_actor_ref_t ref;
    zlink_routing_id_t source_node_rid;
    zlink_routing_id_t source_session_rid;
    if (!parse_actor_ref_value (env, argv[1], &ref))
        return NULL;
    if (!parse_routing_id_value (env, argv[2], &source_node_rid))
        return NULL;
    if (!parse_routing_id_value (env, argv[3], &source_session_rid))
        return NULL;
    int rc =
      zlink_spot_node_actor_bind_remote_session (node, &ref, &source_node_rid, &source_session_rid);
    if (rc != ZLINK_CONFIG_OK)
        return throw_last_error (env, "spotNodeActorBindRemoteSession failed");
    napi_value ok;
    napi_get_undefined (env, &ok);
    return ok;
}

napi_value spot_node_actor_close_bound_session (napi_env env, napi_callback_info info)
{
    napi_value argv[3];
    size_t argc = 3;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *node = NULL;
    napi_get_value_external (env, argv[0], &node);
    zlink_actor_ref_t ref;
    if (!parse_actor_ref_value (env, argv[1], &ref))
        return NULL;
    int32_t timeout_ms = 0;
    if (argc >= 3)
        napi_get_value_int32 (env, argv[2], &timeout_ms);
    int rc =
      zlink_spot_node_actor_close_bound_session (node, &ref, static_cast<uint32_t> (timeout_ms));
    if (rc != ZLINK_REQUEST_OK)
        return throw_last_error (env, "spotNodeActorCloseBoundSession failed");
    napi_value ok;
    napi_get_undefined (env, &ok);
    return ok;
}

static int send_bound_actor_parts (void *stream,
                                   const zlink_routing_id_t *session_rid,
                                   const char *actor_id,
                                   zlink_msg_t *parts,
                                   size_t part_count,
                                   zlink_send_flags_t flags)
{
    for (size_t i = 0; i < part_count; ++i) {
        zlink_part_flag_t part_flag = (i + 1u < part_count) ? ZLINK_PART_MORE : ZLINK_PART_FINAL;
        int rc = zlink_stream_send_bound_actor_part (stream, session_rid, actor_id, &parts[i],
                                                     flags, part_flag);
        if (rc != ZLINK_SUBMIT_OK) {
            for (size_t j = i + 1u; j < part_count; ++j)
                zlink_msg_close (&parts[j]);
            return rc;
        }
    }
    return ZLINK_SUBMIT_OK;
}

napi_value stream_bind_actor (napi_env env, napi_callback_info info)
{
    napi_value argv[5];
    size_t argc = 5;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *stream = NULL;
    napi_get_value_external (env, argv[0], &stream);
    zlink_routing_id_t session_rid;
    zlink_actor_ref_t actor;
    if (!parse_routing_id_value (env, argv[1], &session_rid))
        return NULL;
    if (!parse_actor_ref_value (env, argv[2], &actor))
        return NULL;
    napi_valuetype handler_type = napi_undefined;
    if (argc >= 4)
        napi_typeof (env, argv[3], &handler_type);
    int32_t timeout_ms = 0;
    if (argc >= 5)
        napi_get_value_int32 (env, argv[4], &timeout_ms);
    if (handler_type == napi_function) {
        request_js_state_t *state = create_request_js_state (env, argv[3]);
        if (!state)
            return NULL;
        int rc =
          zlink_stream_bind_actor (stream, &session_rid, &actor, request_reply_callback_trampoline,
                                   state, static_cast<uint32_t> (timeout_ms));
        if (rc != ZLINK_SUBMIT_OK) {
            abort_request_js_state (state);
            return throw_last_error (env, "streamBindActor failed");
        }
        napi_value ok;
        napi_get_undefined (env, &ok);
        return ok;
    }
    sync_request_state_t state;
    int rc = zlink_stream_bind_actor (stream, &session_rid, &actor, sync_request_callback, &state,
                                      static_cast<uint32_t> (timeout_ms));
    if (rc != ZLINK_SUBMIT_OK)
        return throw_last_error (env, "streamBindActor failed");
    zlink_request_result_t request_rc = wait_sync_request (&state);
    if (request_rc != ZLINK_REQUEST_OK)
        return throw_last_error (env, "streamBindActor failed");
    napi_value ok;
    napi_get_undefined (env, &ok);
    return ok;
}

napi_value stream_unbind_actor (napi_env env, napi_callback_info info)
{
    napi_value argv[5];
    size_t argc = 5;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *stream = NULL;
    napi_get_value_external (env, argv[0], &stream);
    zlink_routing_id_t session_rid;
    if (!parse_routing_id_value (env, argv[1], &session_rid))
        return NULL;
    std::string actor_id = get_string (env, argv[2]);
    napi_valuetype handler_type = napi_undefined;
    if (argc >= 4)
        napi_typeof (env, argv[3], &handler_type);
    int32_t timeout_ms = 0;
    if (argc >= 5)
        napi_get_value_int32 (env, argv[4], &timeout_ms);
    if (handler_type == napi_function) {
        request_js_state_t *state = create_request_js_state (env, argv[3]);
        if (!state)
            return NULL;
        int rc = zlink_stream_unbind_actor (stream, &session_rid, actor_id.c_str (),
                                            request_reply_callback_trampoline, state,
                                            static_cast<uint32_t> (timeout_ms));
        if (rc != ZLINK_SUBMIT_OK) {
            abort_request_js_state (state);
            return throw_last_error (env, "streamUnbindActor failed");
        }
        napi_value ok;
        napi_get_undefined (env, &ok);
        return ok;
    }
    sync_request_state_t state;
    int rc =
      zlink_stream_unbind_actor (stream, &session_rid, actor_id.c_str (), sync_request_callback,
                                 &state, static_cast<uint32_t> (timeout_ms));
    if (rc != ZLINK_SUBMIT_OK)
        return throw_last_error (env, "streamUnbindActor failed");
    zlink_request_result_t request_rc = wait_sync_request (&state);
    if (request_rc != ZLINK_REQUEST_OK)
        return throw_last_error (env, "streamUnbindActor failed");
    napi_value ok;
    napi_get_undefined (env, &ok);
    return ok;
}

napi_value stream_send_bound_actor_part (napi_env env, napi_callback_info info)
{
    napi_value argv[5];
    size_t argc = 5;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *stream = NULL;
    napi_get_value_external (env, argv[0], &stream);
    zlink_routing_id_t session_rid;
    if (!parse_routing_id_value (env, argv[1], &session_rid))
        return NULL;
    std::string actor_id = get_string (env, argv[2]);
    std::vector<zlink_msg_t> parts;
    if (!build_msg_vector_or_single (env, argv[3], &parts))
        return NULL;
    int32_t flags = 0;
    if (argc >= 5)
        napi_get_value_int32 (env, argv[4], &flags);
    int rc = send_bound_actor_parts (stream, &session_rid, actor_id.c_str (), parts.data (),
                                     parts.size (), static_cast<zlink_send_flags_t> (flags));
    if (rc != ZLINK_SUBMIT_OK)
        return throw_last_error (env, "streamSendBoundActorPart failed");
    napi_value ok;
    napi_get_undefined (env, &ok);
    return ok;
}

napi_value stream_bound_actors (napi_env env, napi_callback_info info)
{
    napi_value argv[2];
    size_t argc = 2;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *stream = NULL;
    napi_get_value_external (env, argv[0], &stream);
    zlink_routing_id_t session_rid;
    if (!parse_routing_id_value (env, argv[1], &session_rid))
        return NULL;
    size_t count = 0;
    int rc = zlink_stream_bound_actors (stream, &session_rid, NULL, &count);
    if (rc != ZLINK_CONFIG_OK)
        return throw_last_error (env, "streamBoundActors failed");
    napi_value arr;
    napi_create_array_with_length (env, count, &arr);
    if (count == 0)
        return arr;
    std::vector<zlink_actor_ref_t> entries (count);
    rc = zlink_stream_bound_actors (stream, &session_rid, entries.data (), &count);
    if (rc != ZLINK_CONFIG_OK)
        return throw_last_error (env, "streamBoundActors failed");
    for (size_t i = 0; i < count; ++i) {
        napi_set_element (env, arr, static_cast<uint32_t> (i),
                          create_actor_ref_value (env, entries[i]));
    }
    return arr;
}

napi_value remote_actor_get_ref (napi_env env, napi_callback_info info)
{
    napi_value argv[5];
    size_t argc = 5;
    napi_get_cb_info (env, info, &argc, argv, NULL, NULL);
    void *node = NULL;
    napi_get_value_external (env, argv[0], &node);
    zlink_routing_id_t target_node_rid;
    if (!parse_routing_id_value (env, argv[1], &target_node_rid))
        return NULL;
    std::string actor_id = get_string (env, argv[2]);
    napi_valuetype handler_type = napi_undefined;
    napi_typeof (env, argv[3], &handler_type);
    if (handler_type != napi_function) {
        napi_throw_type_error (env, NULL, "remoteActorGetRef handler must be a function");
        return NULL;
    }
    int32_t timeout_ms = 0;
    napi_get_value_int32 (env, argv[4], &timeout_ms);
    request_js_state_t *state = create_request_js_state (env, argv[3]);
    if (!state)
        return NULL;
    int rc = zlink_remote_actor_get_ref (node, &target_node_rid, actor_id.c_str (),
                                         actor_lookup_callback_trampoline, state,
                                         static_cast<uint32_t> (timeout_ms));
    if (rc != ZLINK_SUBMIT_OK) {
        abort_request_js_state (state);
        return throw_last_error (env, "remoteActorGetRef failed");
    }
    napi_value ok;
    napi_get_undefined (env, &ok);
    return ok;
}
