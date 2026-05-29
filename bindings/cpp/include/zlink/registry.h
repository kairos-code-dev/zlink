/* SPDX-License-Identifier: MPL-2.0 */

#ifndef ZLINK_REGISTRY_H_INCLUDED
#define ZLINK_REGISTRY_H_INCLUDED

#include <zlink/service_common.h>

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
/*  Registry API                                                              */
/******************************************************************************/

/**
 * @brief Create a service registry.
 *
 * A registry accepts service registration/deregistration/heartbeat
 * requests and periodically broadcasts the service list.
 *
 * @param ctx  Context handle.
 * @return Registry handle, or NULL on failure.
 */
ZLINK_EXPORT void *zlink_registry_new (void *ctx);

/**
 * @brief Bind the registry PUB and ROUTER endpoints and start the registry.
 * @param pub_endpoint     PUB endpoint for broadcasting.
 * @param router_endpoint  ROUTER endpoint for receiving registrations.
 */
ZLINK_EXPORT zlink_bind_result_t zlink_registry_bind (void *registry,
                                      const char *pub_endpoint,
                                      const char *router_endpoint);

/** @brief Set the registry unique ID (used for cluster configuration). */
ZLINK_EXPORT zlink_config_result_t zlink_registry_set_id (void *registry, uint32_t registry_id);

ZLINK_EXPORT zlink_config_result_t zlink_registry_set (
  void *registry,
  zlink_registry_option_t option,
  uint32_t value);

ZLINK_EXPORT uint32_t zlink_registry_get (
  void *registry,
  zlink_registry_option_t option,
  zlink_config_result_t *error_out);

/** @brief Add a peer registry PUB endpoint (for cluster synchronization). */
ZLINK_EXPORT zlink_config_result_t zlink_registry_add_peer (void *registry,
                                      const char *peer_pub_endpoint);

/**
 * @brief Set heartbeat interval and timeout.
 * Defaults are 5000 ms for the heartbeat interval and 15000 ms for
 * the timeout.
 * @param interval_ms  Heartbeat send interval in milliseconds.
 * @param timeout_ms   Expiry time when no heartbeat is received, in
 *                     milliseconds.
 */
ZLINK_EXPORT zlink_config_result_t zlink_registry_set_heartbeat (void *registry,
                                           uint32_t interval_ms,
                                           uint32_t timeout_ms);

/**
 * @brief Set the service list broadcast interval in milliseconds.
 * Default is 30000 ms.
 */
ZLINK_EXPORT zlink_config_result_t zlink_registry_set_broadcast_interval (void *registry,
                                                    uint32_t interval_ms);

/** @brief Destroy the registry and release all resources. */
ZLINK_EXPORT zlink_close_result_t zlink_registry_destroy (void **registry_p);

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

ZLINK_EXPORT zlink_config_result_t zlink_registry_status (
  void *registry_,
  zlink_registry_status_t *out_);
ZLINK_EXPORT zlink_config_result_t zlink_registry_service_summary (
  void *registry_,
  const zlink_registry_service_summary_filter_t *filter_,
  zlink_registry_service_summary_entry_t *entries_,
  size_t *count_);
ZLINK_EXPORT zlink_config_result_t zlink_registry_member_peers (
  void *registry_,
  const char *channel_name_,
  zlink_member_peer_entry_t *entries_,
  size_t *count_);

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

ZLINK_EXPORT zlink_config_result_t zlink_registry_topology (
  void *registry,
  const zlink_registry_topology_filter_t *filter,
  zlink_registry_topology_entry_t *entries,
  size_t *count);

ZLINK_EXPORT void *zlink_registry_query_client_new (void *ctx);
ZLINK_EXPORT zlink_connect_result_t zlink_registry_query_client_connect (void *client,
                                                      const char *endpoint);
ZLINK_EXPORT zlink_config_result_t zlink_registry_query_client_topology (
  void *client,
  const zlink_registry_topology_filter_t *filter,
  zlink_registry_topology_entry_t *entries,
  size_t *count);

ZLINK_EXPORT zlink_close_result_t zlink_registry_query_client_destroy (void **client_p);

#ifdef __cplusplus
}
#endif

#endif
