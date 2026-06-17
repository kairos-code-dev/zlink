/* SPDX-License-Identifier: MPL-2.0 */

#ifndef ZLINK_SERVICE_ACTOR_H_INCLUDED
#define ZLINK_SERVICE_ACTOR_H_INCLUDED

#include <zlink/common.h>
#include <zlink/message/api.h>
#include <zlink/service_common.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ZLINK_ACTOR_ID_MAX 256
#define ZLINK_ACTOR_JOIN_INFO_REMOTE 1u

/** @brief References an actor: the node hosting it, its id, and its generation. */
typedef struct zlink_actor_ref_t
{
    zlink_routing_id_t node_rid;
    char actor_id[ZLINK_ACTOR_ID_MAX];
    uint64_t generation;
} zlink_actor_ref_t;

/** @brief Metadata about a message received for an actor. */
typedef struct zlink_actor_recv_info_t
{
    zlink_actor_ref_t actor;
    zlink_routing_id_t source_node_rid;
    zlink_routing_id_t source_session_rid;
    uint32_t flags;
} zlink_actor_recv_info_t;

/** @brief Details of an actor-join request: the actors and spots on each side. */
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

/** @brief The resolved route to an actor: which spot it currently lives on. */
typedef struct zlink_actor_route_t
{
    zlink_actor_ref_t actor;
    zlink_routing_id_t current_spot_rid;
    zlink_spot_kind_t current_spot_kind;
} zlink_actor_route_t;

/** @brief The outcome of an actor join. */
typedef struct zlink_actor_join_result_t
{
    zlink_request_result_t result;
    int32_t join_result_code;
    zlink_actor_ref_t actor;
    zlink_routing_id_t joined_spot_rid;
    uint64_t join_epoch;
    uint32_t flags;
} zlink_actor_join_result_t;

/** @brief The outcome of an actor join to an entry spot. */
typedef struct zlink_actor_join_entry_spot_result_t
{
    zlink_request_result_t result;
    int32_t join_result_code;
    zlink_actor_ref_t actor;
    zlink_routing_id_t target_node_rid;
    zlink_routing_id_t joined_spot_rid;
    uint64_t join_epoch;
    uint32_t flags;
} zlink_actor_join_entry_spot_result_t;

/** @brief The outcome of an actor lookup. */
typedef struct zlink_actor_lookup_result_t
{
    zlink_request_result_t result;
    zlink_actor_ref_t actor;
    uint32_t flags;
} zlink_actor_lookup_result_t;

/** @brief Details of an actor lifecycle change, before and after. */
typedef struct zlink_spot_actor_lifecycle_info_t
{
    zlink_actor_ref_t previous_actor;
    zlink_actor_ref_t current_actor;
    zlink_routing_id_t previous_spot_rid;
    zlink_routing_id_t current_spot_rid;
    uint64_t join_epoch;
    uint32_t flags;
} zlink_spot_actor_lifecycle_info_t;

/** @brief The kind of an actor lifecycle transition event. */
typedef enum zlink_spot_actor_lifecycle_event_kind_t
{
    ZLINK_SPOT_ACTOR_LIFECYCLE_JOINED = 1,
    ZLINK_SPOT_ACTOR_LIFECYCLE_LEFT = 2
} zlink_spot_actor_lifecycle_event_kind_t;

/** @brief An actor lifecycle event delivered to a lifecycle subscriber. */
typedef struct zlink_spot_actor_lifecycle_event_t
{
    zlink_spot_actor_lifecycle_event_kind_t kind;
    zlink_spot_actor_lifecycle_info_t info;
} zlink_spot_actor_lifecycle_event_t;

#ifdef __cplusplus
}
#endif

#endif
