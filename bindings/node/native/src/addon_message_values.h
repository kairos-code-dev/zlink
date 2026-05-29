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

enum message_snapshot_flags_t
{
    MESSAGE_SNAPSHOT_DEFAULT = 0,
    MESSAGE_SNAPSHOT_ALWAYS_REF_COUNT = 1 << 0,
    MESSAGE_SNAPSHOT_ALWAYS_PROPERTIES = 1 << 1
};

inline bool message_has_snapshot_properties(const zlink_routing_id_t *routing_id,
                                            zlink_msg_t *msg)
{
    return zlink_msg_gets(msg, "Socket-Type")
        || zlink_msg_gets(msg, "User-Id")
        || zlink_msg_gets(msg, "Peer-Address")
        || zlink_msg_gets(msg, "Routing-Id")
        || zlink_msg_gets(msg, "Identity")
        || (routing_id && routing_id->size > 0);
}

inline napi_value create_message_properties_snapshot(
  napi_env env,
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

inline napi_value create_message_snapshot_value(
  napi_env env,
  const zlink_routing_id_t *routing_id,
  zlink_msg_t *msg,
  int flags = MESSAGE_SNAPSHOT_DEFAULT)
{
    napi_value obj;
    napi_create_object(env, &obj);

    zlink_config_result_t refcnt_err = ZLINK_CONFIG_OK;
    const int refcnt = zlink_msg_refcnt(msg, &refcnt_err);
    if (refcnt_err != ZLINK_CONFIG_OK)
        return throw_last_error(env, "message refcnt failed");

    napi_value data = create_message_data_buffer(env, msg);
    if (!data)
        return NULL;

    napi_set_named_property(env, obj, "data", data);
    if (refcnt != 1 || (flags & MESSAGE_SNAPSHOT_ALWAYS_REF_COUNT)) {
        napi_value ref_count;
        napi_create_int32(env, refcnt, &ref_count);
        napi_set_named_property(env, obj, "refCount", ref_count);
    }
    if (message_has_snapshot_properties(routing_id, msg)
        || (flags & MESSAGE_SNAPSHOT_ALWAYS_PROPERTIES)) {
        napi_value props = create_message_properties_snapshot(env, routing_id, msg);
        napi_set_named_property(env, obj, "properties", props);
    }
    return obj;
}
