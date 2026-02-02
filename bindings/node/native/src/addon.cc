#include <node_api.h>
#include "zlink.h"

static napi_value version (napi_env env, napi_callback_info info)
{
    int major = 0;
    int minor = 0;
    int patch = 0;
    zlink_version (&major, &minor, &patch);

    napi_value arr;
    if (napi_create_array_with_length (env, 3, &arr) != napi_ok)
        return NULL;

    napi_value v0, v1, v2;
    napi_create_int32 (env, major, &v0);
    napi_create_int32 (env, minor, &v1);
    napi_create_int32 (env, patch, &v2);

    napi_set_element (env, arr, 0, v0);
    napi_set_element (env, arr, 1, v1);
    napi_set_element (env, arr, 2, v2);

    return arr;
}

static napi_value init (napi_env env, napi_value exports)
{
    napi_property_descriptor desc = {"version", 0, version, 0, 0, 0, napi_default, 0};
    napi_define_properties (env, exports, 1, &desc);
    return exports;
}

NAPI_MODULE (NODE_GYP_MODULE_NAME, init)
