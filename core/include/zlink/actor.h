/* SPDX-License-Identifier: MPL-2.0 */

#ifndef ZLINK_ACTOR_H_INCLUDED
#define ZLINK_ACTOR_H_INCLUDED

#include <zlink/common.h>
#include <zlink/message.h>
#include <zlink/service_common.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ZLINK_ACTOR_ID_MAX 256
#define ZLINK_ACTOR_JOIN_INFO_REMOTE 1u

typedef struct zlink_actor_ref_t
{
    zlink_routing_id_t node_rid;
    char actor_id[ZLINK_ACTOR_ID_MAX];
    uint64_t generation;
} zlink_actor_ref_t;

typedef struct zlink_actor_recv_info_t
{
    zlink_actor_ref_t actor;
    zlink_routing_id_t source_node_rid;
    zlink_routing_id_t source_session_rid;
    uint32_t flags;
} zlink_actor_recv_info_t;

typedef struct zlink_actor_join_info_t
{
    zlink_actor_ref_t source_actor;
    zlink_actor_ref_t target_actor;
    zlink_routing_id_t source_node_rid;
    zlink_routing_id_t source_spot_rid;
    zlink_routing_id_t target_node_rid;
    zlink_routing_id_t target_spot_rid;
    uint64_t join_epoch;
    void *request;
    uint32_t flags;
} zlink_actor_join_info_t;

typedef struct zlink_actor_route_t
{
    zlink_actor_ref_t actor;
    zlink_routing_id_t current_spot_rid;
    zlink_spot_kind_t current_spot_kind;
} zlink_actor_route_t;

typedef struct zlink_actor_join_result_t
{
    zlink_request_result_t result;
    int32_t join_result_code;
    zlink_actor_ref_t actor;
    zlink_routing_id_t joined_spot_rid;
    uint64_t join_epoch;
    uint32_t flags;
} zlink_actor_join_result_t;

typedef struct zlink_actor_join_entry_spot_result_t
{
    zlink_request_result_t result;
    zlink_actor_ref_t actor;
    zlink_routing_id_t target_node_rid;
    uint64_t join_epoch;
    uint32_t flags;
} zlink_actor_join_entry_spot_result_t;

typedef struct zlink_actor_lookup_result_t
{
    zlink_request_result_t result;
    zlink_actor_ref_t actor;
    uint32_t flags;
} zlink_actor_lookup_result_t;

typedef struct zlink_spot_actor_lifecycle_info_t
{
    zlink_actor_ref_t previous_actor;
    zlink_actor_ref_t current_actor;
    zlink_routing_id_t previous_spot_rid;
    zlink_routing_id_t current_spot_rid;
    uint64_t join_epoch;
    uint32_t flags;
} zlink_spot_actor_lifecycle_info_t;

#ifdef __cplusplus
}
#endif

#endif
