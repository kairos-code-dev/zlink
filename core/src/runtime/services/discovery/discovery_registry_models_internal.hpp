/* SPDX-License-Identifier: MPL-2.0 */

#ifndef ZLINK_DISCOVERY_REGISTRY_MODELS_INTERNAL_HPP_INCLUDED
#define ZLINK_DISCOVERY_REGISTRY_MODELS_INTERNAL_HPP_INCLUDED

#include <zlink/service_common.h>
#include <zlink/service/actor.h>

#include <stdint.h>

typedef enum zlink_internal_discovery_option_t
{
    ZLINK_OPT_DISCOVERY_SPOT_OWNER_SYNC = 0x3035,
    ZLINK_OPT_DISCOVERY_ACTOR_ROUTE_SYNC = 0x3036
} zlink_internal_discovery_option_t;

typedef enum zlink_auto_connect_type_t
{
    ZLINK_AUTO_CONNECT_INVALID = 0,
    ZLINK_AUTO_CONNECT_ROUTE_MESH = 1,
    ZLINK_AUTO_CONNECT_CLIENT_SERVER = 2,
    ZLINK_AUTO_CONNECT_DEALER_MESH = 3,
    ZLINK_AUTO_CONNECT_FANOUT = 4,
    ZLINK_AUTO_CONNECT_SPOT_MESH = 5
} zlink_auto_connect_type_t;

typedef uint32_t zlink_route_kind_t;

#define ZLINK_ROUTE_KIND_INVALID 0u
#define ZLINK_ROUTE_KIND_ACTOR 1u
#define ZLINK_ROUTE_KIND_SPOT_NAME 2u
#define ZLINK_ROUTE_KIND_ACTOR_SESSION 3u

typedef struct zlink_spot_route_t
{
    zlink_routing_id_t spot_rid;
    zlink_routing_id_t owner_node_rid;
    zlink_spot_kind_t spot_kind;
} zlink_spot_route_t;

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

typedef enum zlink_registry_option_t
{
    ZLINK_REGISTRY_OPT_ID = 0x3801,
    ZLINK_REGISTRY_OPT_HEARTBEAT_INTERVAL_MS = 0x3802,
    ZLINK_REGISTRY_OPT_HEARTBEAT_TIMEOUT_MS = 0x3803,
    ZLINK_REGISTRY_OPT_BROADCAST_INTERVAL_MS = 0x3804
} zlink_registry_option_t;

typedef enum zlink_registry_state_t
{
    ZLINK_REGISTRY_STATE_IDLE = 1,
    ZLINK_REGISTRY_STATE_ACTIVE = 2,
    ZLINK_REGISTRY_STATE_DEGRADED = 3,
    ZLINK_REGISTRY_STATE_ERROR = 4
} zlink_registry_state_t;

typedef enum zlink_service_kind_t
{
    ZLINK_SERVICE_KIND_DISCOVERY = 1,
    ZLINK_SERVICE_KIND_SPOT_SUB = 3,
    ZLINK_SERVICE_KIND_SPOT_PUB = 4,
    ZLINK_SERVICE_KIND_SOCKET = 5
} zlink_service_kind_t;

typedef enum zlink_topology_source_t
{
    ZLINK_TOPOLOGY_SOURCE_MANUAL = 1,
    ZLINK_TOPOLOGY_SOURCE_DISCOVERY = 2,
    ZLINK_TOPOLOGY_SOURCE_REGISTRY = 3
} zlink_topology_source_t;

typedef enum zlink_topology_state_t
{
    ZLINK_TOPOLOGY_STATE_DISCOVERED = 1,
    ZLINK_TOPOLOGY_STATE_CONNECTING = 2,
    ZLINK_TOPOLOGY_STATE_READY = 3,
    ZLINK_TOPOLOGY_STATE_LOST = 4,
    ZLINK_TOPOLOGY_STATE_ERROR = 5,
    ZLINK_TOPOLOGY_STATE_STOPPED = 6
} zlink_topology_state_t;

typedef struct zlink_registry_status_t
{
    uint32_t registry_id;
    char bind_endpoint[256];
    zlink_registry_state_t state;
    uint32_t topology_entry_count;
    uint32_t peer_registry_count;
    uint32_t connected_peer_registry_count;
    uint64_t list_seq;
    int32_t last_error;
    uint64_t last_changed_ms;
} zlink_registry_status_t;

typedef struct zlink_registry_service_summary_entry_t
{
    zlink_auto_connect_type_t auto_connect_type;
    zlink_service_role_t service_role;
    char channel_name[256];
    uint32_t total_count;
    uint32_t connecting_count;
    uint32_t ready_count;
    uint32_t error_count;
    uint32_t stopped_count;
    uint64_t last_reported_ms;
} zlink_registry_service_summary_entry_t;

typedef struct zlink_registry_service_summary_filter_t
{
    zlink_auto_connect_type_t auto_connect_type;
    zlink_service_role_t service_role;
    char channel_name[256];
} zlink_registry_service_summary_filter_t;

typedef struct zlink_registry_topology_entry_t
{
    zlink_auto_connect_type_t auto_connect_type;
    zlink_routing_id_t routing_id;
    zlink_service_kind_t service_kind;
    zlink_service_role_t service_role;
    char channel_name[256];
    char endpoint[256];
    zlink_topology_source_t source;
    zlink_topology_state_t state;
    uint32_t desired_count;
    uint32_t ready_count;
    uint32_t error_code;
    uint64_t last_reported_ms;
    zlink_spot_kind_t spot_kind;
} zlink_registry_topology_entry_t;

typedef struct zlink_registry_topology_filter_t
{
    zlink_auto_connect_type_t auto_connect_type;
    zlink_service_kind_t service_kind;
    zlink_service_role_t service_role;
    char channel_name[256];
    zlink_routing_id_t routing_id;
    zlink_topology_state_t state;
    zlink_topology_source_t source;
} zlink_registry_topology_filter_t;

#endif
