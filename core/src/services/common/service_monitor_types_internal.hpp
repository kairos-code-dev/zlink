/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_SERVICES_COMMON_SERVICE_MONITOR_TYPES_INTERNAL_HPP_INCLUDED__
#define __ZLINK_SERVICES_COMMON_SERVICE_MONITOR_TYPES_INTERNAL_HPP_INCLUDED__

#include <zlink.h>

typedef uint32_t zlink_discovery_monitor_event_mask_t;
typedef uint32_t zlink_service_monitor_event_mask_t;

typedef enum zlink_discovery_monitor_event_e
{
    ZLINK_DISCOVERY_MONITOR_EVENT_ERROR = 1u << 4,
    ZLINK_DISCOVERY_MONITOR_EVENT_SERVICE_UP = 1u << 5,
    ZLINK_DISCOVERY_MONITOR_EVENT_SERVICE_DOWN = 1u << 6,
    ZLINK_DISCOVERY_MONITOR_EVENT_PROVIDERS_CHANGED = 1u << 7,
    ZLINK_DISCOVERY_MONITOR_EVENT_CLOSED = 1u << 17,

    ZLINK_MONITOR_EVENT_ERROR = ZLINK_DISCOVERY_MONITOR_EVENT_ERROR,
    ZLINK_DISCOVERY_SERVICE_UP = ZLINK_DISCOVERY_MONITOR_EVENT_SERVICE_UP,
    ZLINK_DISCOVERY_SERVICE_DOWN = ZLINK_DISCOVERY_MONITOR_EVENT_SERVICE_DOWN,
    ZLINK_DISCOVERY_PROVIDERS_CHANGED =
      ZLINK_DISCOVERY_MONITOR_EVENT_PROVIDERS_CHANGED,
    ZLINK_MONITOR_EVENT_CLOSED = ZLINK_DISCOVERY_MONITOR_EVENT_CLOSED
} zlink_discovery_monitor_event_e;

typedef enum zlink_monitor_target_kind_t
{
    ZLINK_MONITOR_TARGET_SOCKET = 1,
    ZLINK_MONITOR_TARGET_DISCOVERY = 2,
    ZLINK_MONITOR_TARGET_SPOT = 4,
    ZLINK_MONITOR_TARGET_SPOT_NODE = 5
} zlink_monitor_target_kind_t;

typedef enum zlink_service_monitor_event_e
{
    ZLINK_SERVICE_MONITOR_EVENT_ERROR = ZLINK_DISCOVERY_MONITOR_EVENT_ERROR,
    ZLINK_SERVICE_MONITOR_EVENT_CLOSED = ZLINK_DISCOVERY_MONITOR_EVENT_CLOSED,
    ZLINK_SERVICE_MONITOR_EVENT_DISCOVERY_SERVICE_UP =
      ZLINK_DISCOVERY_MONITOR_EVENT_SERVICE_UP,
    ZLINK_SERVICE_MONITOR_EVENT_DISCOVERY_SERVICE_DOWN =
      ZLINK_DISCOVERY_MONITOR_EVENT_SERVICE_DOWN,
    ZLINK_SERVICE_MONITOR_EVENT_DISCOVERY_PROVIDERS_CHANGED =
      ZLINK_DISCOVERY_MONITOR_EVENT_PROVIDERS_CHANGED,
    ZLINK_SERVICE_MONITOR_EVENT_PEER_WEIGHT_CHANGED = 1u << 8,
    ZLINK_SERVICE_MONITOR_EVENT_ALL =
      ZLINK_SERVICE_MONITOR_EVENT_ERROR
      | ZLINK_SERVICE_MONITOR_EVENT_CLOSED
      | ZLINK_SERVICE_MONITOR_EVENT_DISCOVERY_SERVICE_UP
      | ZLINK_SERVICE_MONITOR_EVENT_DISCOVERY_SERVICE_DOWN
      | ZLINK_SERVICE_MONITOR_EVENT_DISCOVERY_PROVIDERS_CHANGED
      | ZLINK_SERVICE_MONITOR_EVENT_PEER_WEIGHT_CHANGED
} zlink_service_monitor_event_e;

typedef struct zlink_service_event_t
{
    zlink_service_kind_t service_kind;
    uint32_t event_type;
    int32_t status;
    int32_t error_code;
    uint32_t value;
    zlink_service_event_detail_mask_t detail_flags;
    char service_name[256];
    char endpoint[256];
    zlink_routing_id_t routing_id;
    char subject[256];
    uint32_t subject_kind;
} zlink_service_event_t;

typedef void (*zlink_service_monitor_handler_fn) (
  const zlink_service_event_t *event_, void *userdata_);

typedef zlink_service_event_t zlink_service_monitor_event_t;
typedef zlink_service_event_detail_mask_t
  zlink_service_monitor_event_detail_mask_t;

typedef struct zlink_service_monitor_open_options_t
{
    zlink_service_monitor_event_mask_t events;
} zlink_service_monitor_open_options_t;

#endif
