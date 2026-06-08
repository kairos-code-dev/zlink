/* SPDX-License-Identifier: MPL-2.0 */

#ifndef ZLINK_SERVICE_COMMON_H_INCLUDED
#define ZLINK_SERVICE_COMMON_H_INCLUDED

#include <zlink/common.h>
#include <zlink/message/api.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct zlink_member_peer_entry_t
{
    zlink_auto_connect_type_t auto_connect_type;
    zlink_service_role_t service_role;
    char channel_name[256];
    char endpoint[256];
    uint32_t weight;
    zlink_routing_id_t routing_id;
    int64_t value;
} zlink_member_peer_entry_t;

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
