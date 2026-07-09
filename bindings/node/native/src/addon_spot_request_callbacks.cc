/* SPDX-License-Identifier: MPL-2.0 */

#include "addon_spot_request_callbacks.h"
#include "addon_message_parts.h"
#include "addon_message_values.h"
#include "addon_spot_actor_values.h"
#include "addon_tsfn_slots.h"

#include <chrono>
#include <memory>
#include <vector>

struct request_js_state_t
{
    request_js_state_t () : env (NULL), tsfn (NULL) {}

    napi_env env;
    napi_threadsafe_function tsfn;
};

namespace
{

struct request_result_js_payload_t
{
    request_result_js_payload_t () :
        errnum (0),
        has_actor_join (false),
        has_actor_lookup (false),
        has_actor_join_entry_spot (false),
        join_result_code (0),
        join_epoch (0),
        flags (0),
        part_count (0)
    {
        memset (&actor, 0, sizeof (actor));
        memset (&joined_spot_rid, 0, sizeof (joined_spot_rid));
        memset (&target_node_rid, 0, sizeof (target_node_rid));
    }

    ~request_result_js_payload_t ()
    {
        if (part_count > 0)
            close_recv_parts (parts.data (), part_count);
    }

    int errnum;
    std::vector<zlink_msg_t> parts;
    bool has_actor_join;
    bool has_actor_lookup;
    bool has_actor_join_entry_spot;
    int32_t join_result_code;
    zlink_actor_ref_t actor;
    zlink_routing_id_t joined_spot_rid;
    zlink_routing_id_t target_node_rid;
    uint64_t join_epoch;
    uint32_t flags;
    size_t part_count;
};

bool move_recv_parts_to_payload (zlink_msg_t *parts,
                                 size_t part_count,
                                 request_result_js_payload_t *payload)
{
    if (!payload)
        return false;
    payload->parts.resize (part_count);
    for (size_t i = 0; i < part_count; ++i) {
        if (zlink_msg_init (&payload->parts[i]) != 0)
            return false;
        payload->part_count = i + 1;
        if (zlink_msg_move (&payload->parts[i], &parts[i]) != 0)
            return false;
    }
    return true;
}

void request_tsfn_finalize (napi_env env, void *finalize_data, void *finalize_hint)
{
    (void) env;
    (void) finalize_hint;
    request_js_state_t *state = static_cast<request_js_state_t *> (finalize_data);
    delete state;
}

void request_tsfn_call_js (napi_env env, napi_value js_cb, void *context, void *data)
{
    (void) context;
    std::unique_ptr<request_result_js_payload_t> payload (
      static_cast<request_result_js_payload_t *> (data));
    if (!env || !js_cb || !payload)
        return;

    napi_value argv[2];
    if (payload->has_actor_join_entry_spot) {
        napi_value result_obj;
        napi_create_object (env, &result_obj);
        napi_value result_value;
        napi_create_int32 (env, payload->errnum, &result_value);
        napi_set_named_property (env, result_obj, "result", result_value);
        napi_value join_result_code_value;
        napi_create_int32 (env, payload->join_result_code, &join_result_code_value);
        napi_set_named_property (env, result_obj, "joinResultCode", join_result_code_value);
        napi_set_named_property (env, result_obj, "actor",
                                 create_actor_ref_value (env, payload->actor));
        napi_set_named_property (env, result_obj, "targetNodeRid",
                                 create_routing_id_value (env, payload->target_node_rid));
        napi_set_named_property (env, result_obj, "joinedSpotRid",
                                 create_routing_id_value (env, payload->joined_spot_rid));
        napi_value join_epoch;
        napi_create_bigint_uint64 (env, payload->join_epoch, &join_epoch);
        napi_set_named_property (env, result_obj, "joinEpoch", join_epoch);
        napi_value flags_value;
        napi_create_uint32 (env, payload->flags, &flags_value);
        napi_set_named_property (env, result_obj, "flags", flags_value);
        argv[0] = result_obj;
        if (payload->errnum != 0) {
            napi_get_null (env, &argv[1]);
        } else {
            napi_value parts_array;
            napi_create_array_with_length (env, payload->parts.size (), &parts_array);
            for (size_t i = 0; i < payload->parts.size (); ++i) {
                napi_value part_buf = create_message_data_buffer (env, &payload->parts[i]);
                if (!part_buf)
                    return;
                napi_set_element (env, parts_array, static_cast<uint32_t> (i), part_buf);
            }
            argv[1] = parts_array;
        }
    } else if (payload->has_actor_join) {
        napi_value result_obj;
        napi_create_object (env, &result_obj);
        napi_value result_value;
        napi_create_int32 (env, payload->errnum, &result_value);
        napi_set_named_property (env, result_obj, "result", result_value);
        napi_value join_result_code_value;
        napi_create_int32 (env, payload->join_result_code, &join_result_code_value);
        napi_set_named_property (env, result_obj, "joinResultCode", join_result_code_value);
        napi_set_named_property (env, result_obj, "actor",
                                 create_actor_ref_value (env, payload->actor));
        napi_set_named_property (env, result_obj, "joinedSpotRid",
                                 create_routing_id_value (env, payload->joined_spot_rid));
        napi_value join_epoch;
        napi_create_bigint_uint64 (env, payload->join_epoch, &join_epoch);
        napi_set_named_property (env, result_obj, "joinEpoch", join_epoch);
        napi_value flags_value;
        napi_create_uint32 (env, payload->flags, &flags_value);
        napi_set_named_property (env, result_obj, "flags", flags_value);
        argv[0] = result_obj;
        if (payload->errnum != 0) {
            napi_get_null (env, &argv[1]);
        } else {
            napi_value parts_array;
            napi_create_array_with_length (env, payload->parts.size (), &parts_array);
            for (size_t i = 0; i < payload->parts.size (); ++i) {
                napi_value part_buf = create_message_data_buffer (env, &payload->parts[i]);
                if (!part_buf)
                    return;
                napi_set_element (env, parts_array, static_cast<uint32_t> (i), part_buf);
            }
            argv[1] = parts_array;
        }
    } else if (payload->has_actor_lookup) {
        napi_value result_obj;
        napi_create_object (env, &result_obj);
        napi_value result_value;
        napi_create_int32 (env, payload->errnum, &result_value);
        napi_set_named_property (env, result_obj, "result", result_value);
        napi_set_named_property (env, result_obj, "actor",
                                 create_actor_ref_value (env, payload->actor));
        napi_value flags_value;
        napi_create_uint32 (env, payload->flags, &flags_value);
        napi_set_named_property (env, result_obj, "flags", flags_value);
        argv[0] = result_obj;
        napi_value recv;
        napi_value this_arg;
        napi_get_undefined (env, &this_arg);
        (void) napi_call_function (env, this_arg, js_cb, 1, argv, &recv);
        return;
    } else {
        napi_create_int32 (env, payload->errnum, &argv[0]);
        if (payload->errnum != 0) {
            napi_get_null (env, &argv[1]);
        } else {
            napi_value parts_array;
            napi_create_array_with_length (env, payload->parts.size (), &parts_array);
            for (size_t i = 0; i < payload->parts.size (); ++i) {
                napi_value part_buf = create_message_data_buffer (env, &payload->parts[i]);
                if (!part_buf)
                    return;
                napi_set_element (env, parts_array, static_cast<uint32_t> (i), part_buf);
            }
            argv[1] = parts_array;
        }
    }

    napi_value recv;
    napi_value this_arg;
    napi_get_undefined (env, &this_arg);
    (void) napi_call_function (env, this_arg, js_cb, 2, argv, &recv);
}

request_js_state_t *create_request_js_state_impl (napi_env env,
                                                  napi_value handler,
                                                  const char *resource_name_text,
                                                  const char *setup_error,
                                                  bool unref)
{
    request_js_state_t *state = new request_js_state_t ();
    state->env = env;

    napi_value resource_name;
    napi_create_string_utf8 (env, resource_name_text, NAPI_AUTO_LENGTH, &resource_name);
    napi_threadsafe_function tsfn = NULL;
    napi_status status =
      napi_create_threadsafe_function (env, handler, NULL, resource_name, 0, 1, state,
                                       request_tsfn_finalize, state, request_tsfn_call_js, &tsfn);
    if (status != napi_ok) {
        delete state;
        napi_throw_error (env, NULL, setup_error);
        return NULL;
    }
    if (unref)
        (void) napi_unref_threadsafe_function (env, tsfn);
    state->tsfn = tsfn;
    return state;
}

} // namespace

request_js_state_t *create_request_js_state (napi_env env, napi_value handler)
{
    return create_request_js_state_impl (env, handler, "zlink-spot-request-callback",
                                         "spot request callback setup failed", false);
}

request_js_state_t *create_core_request_js_state (napi_env env, napi_value handler)
{
    return create_request_js_state_impl (env, handler, "zlink-request-reply-callback",
                                         "request callback setup failed", true);
}

void abort_request_js_state (request_js_state_t *state)
{
    if (!state || !state->tsfn)
        return;
    (void) napi_release_threadsafe_function (state->tsfn, napi_tsfn_abort);
    state->tsfn = NULL;
}

void request_reply_callback_trampoline (zlink_request_result_t errnum_,
                                        zlink_msg_t *parts_,
                                        size_t part_count_,
                                        void *userdata_)
{
    request_js_state_t *state = static_cast<request_js_state_t *> (userdata_);
    if (!state || !state->tsfn) {
        close_recv_parts (parts_, part_count_);
        return;
    }

    std::unique_ptr<request_result_js_payload_t> payload (new request_result_js_payload_t ());
    payload->errnum = errnum_;
    if (errnum_ == 0 && !move_recv_parts_to_payload (parts_, part_count_, payload.get ())) {
        payload->errnum = ZLINK_REQUEST_INTERNAL_ERROR;
    }
    close_recv_parts (parts_, part_count_);

    if (napi_call_threadsafe_function (state->tsfn, payload.get (), napi_tsfn_nonblocking)
        == napi_ok) {
        payload.release ();
    }
    release_request_tsfn (state);
}

void actor_join_callback_trampoline (const zlink_actor_join_result_t *result_,
                                     zlink_msg_t *parts_,
                                     size_t part_count_,
                                     void *userdata_)
{
    request_js_state_t *state = static_cast<request_js_state_t *> (userdata_);
    if (!state || !state->tsfn) {
        close_recv_parts (parts_, part_count_);
        return;
    }

    std::unique_ptr<request_result_js_payload_t> payload (new request_result_js_payload_t ());
    payload->has_actor_join = true;
    payload->errnum = result_ ? result_->result : ZLINK_REQUEST_INTERNAL_ERROR;
    if (result_) {
        payload->join_result_code = result_->join_result_code;
        payload->actor = result_->actor;
        payload->joined_spot_rid = result_->joined_spot_rid;
        payload->join_epoch = result_->join_epoch;
        payload->flags = result_->flags;
    }
    if (payload->errnum == 0 && !move_recv_parts_to_payload (parts_, part_count_, payload.get ())) {
        payload->errnum = ZLINK_REQUEST_INTERNAL_ERROR;
    }
    close_recv_parts (parts_, part_count_);

    if (napi_call_threadsafe_function (state->tsfn, payload.get (), napi_tsfn_nonblocking)
        == napi_ok) {
        payload.release ();
    }
    release_request_tsfn (state);
}

void actor_join_entry_spot_callback_trampoline (
  const zlink_actor_join_entry_spot_result_t *result_,
  zlink_msg_t *parts_,
  size_t part_count_,
  void *userdata_)
{
    request_js_state_t *state = static_cast<request_js_state_t *> (userdata_);
    if (!state || !state->tsfn) {
        close_recv_parts (parts_, part_count_);
        return;
    }

    std::unique_ptr<request_result_js_payload_t> payload (new request_result_js_payload_t ());
    payload->has_actor_join_entry_spot = true;
    payload->errnum = result_ ? result_->result : ZLINK_REQUEST_INTERNAL_ERROR;
    if (result_) {
        payload->join_result_code = result_->join_result_code;
        payload->actor = result_->actor;
        payload->target_node_rid = result_->target_node_rid;
        payload->joined_spot_rid = result_->joined_spot_rid;
        payload->join_epoch = result_->join_epoch;
        payload->flags = result_->flags;
    }
    if (payload->errnum == 0 && !move_recv_parts_to_payload (parts_, part_count_, payload.get ())) {
        payload->errnum = ZLINK_REQUEST_INTERNAL_ERROR;
    }
    close_recv_parts (parts_, part_count_);

    if (napi_call_threadsafe_function (state->tsfn, payload.get (), napi_tsfn_nonblocking)
        == napi_ok) {
        payload.release ();
    }
    release_request_tsfn (state);
}

void actor_lookup_callback_trampoline (const zlink_actor_lookup_result_t *result_, void *userdata_)
{
    request_js_state_t *state = static_cast<request_js_state_t *> (userdata_);
    if (!state || !state->tsfn)
        return;

    std::unique_ptr<request_result_js_payload_t> payload (new request_result_js_payload_t ());
    payload->has_actor_lookup = true;
    payload->errnum = result_ ? result_->result : ZLINK_REQUEST_INTERNAL_ERROR;
    if (result_) {
        payload->actor = result_->actor;
        payload->flags = result_->flags;
    }

    if (napi_call_threadsafe_function (state->tsfn, payload.get (), napi_tsfn_nonblocking)
        == napi_ok) {
        payload.release ();
    }
    release_request_tsfn (state);
}

void sync_request_callback (zlink_request_result_t result_,
                            zlink_msg_t *parts_,
                            size_t part_count_,
                            void *userdata_)
{
    sync_request_state_t *state = static_cast<sync_request_state_t *> (userdata_);
    close_recv_parts (parts_, part_count_);
    if (!state)
        return;
    {
        std::lock_guard<std::mutex> lock (state->mu);
        state->result = result_;
        state->done = true;
    }
    state->cv.notify_one ();
}

zlink_request_result_t wait_sync_request (sync_request_state_t *state)
{
    std::unique_lock<std::mutex> lock (state->mu);
    if (!state->cv.wait_for (lock, std::chrono::seconds (5), [state] { return state->done; }))
        return ZLINK_REQUEST_TIMED_OUT;
    return state->result;
}
