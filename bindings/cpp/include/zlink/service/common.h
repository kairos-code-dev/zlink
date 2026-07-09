/* SPDX-License-Identifier: MPL-2.0 */

#ifndef ZLINK_SERVICE_COMMON_H_INCLUDED
#define ZLINK_SERVICE_COMMON_H_INCLUDED

#include <zlink/common.h>
#include <zlink/message/api.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum zlink_spot_kind_t
{
    ZLINK_SPOT_KIND_INVALID = 0,
    ZLINK_SPOT_KIND_ENTRY = 1,
    ZLINK_SPOT_KIND_USER = 2
} zlink_spot_kind_t;

#ifdef __cplusplus
}
#endif

#endif
