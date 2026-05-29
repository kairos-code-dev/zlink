/* SPDX-License-Identifier: MPL-2.0 */

#pragma once

#include "addon_common_api.h"

#include <new>

inline napi_value create_buffer_copy_or_empty(napi_env env,
                                              const void *data,
                                              size_t len)
{
    napi_value out;
    napi_create_buffer_copy(env, len, len == 0 ? NULL : data, NULL, &out);
    return out;
}

inline void finalize_external_msg_buffer(napi_env env, void *data, void *hint)
{
    (void) env;
    (void) data;
    zlink_msg_t *msg = static_cast<zlink_msg_t *>(hint);
    if (!msg)
        return;
    zlink_msg_close(msg);
    delete msg;
}

inline napi_value create_message_data_buffer(napi_env env, zlink_msg_t *msg)
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

inline napi_value create_routing_id_value(napi_env env,
                                          const zlink_routing_id_t &rid)
{
    if (rid.size == 0) {
        napi_value none;
        napi_get_null(env, &none);
        return none;
    }
    return create_buffer_copy_or_empty(env, rid.data, rid.size);
}

inline void set_uint32_property(napi_env env,
                                napi_value obj,
                                const char *name,
                                uint32_t value)
{
    napi_value out;
    napi_create_uint32(env, value, &out);
    napi_set_named_property(env, obj, name, out);
}

inline void set_int64_property(napi_env env,
                               napi_value obj,
                               const char *name,
                               int64_t value)
{
    napi_value out;
    napi_create_int64(env, value, &out);
    napi_set_named_property(env, obj, name, out);
}

inline void set_string_property(napi_env env,
                                napi_value obj,
                                const char *name,
                                const char *value)
{
    napi_value out;
    napi_create_string_utf8(env, value ? value : "", NAPI_AUTO_LENGTH, &out);
    napi_set_named_property(env, obj, name, out);
}
