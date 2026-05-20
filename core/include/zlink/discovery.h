/* SPDX-License-Identifier: MPL-2.0 */

#ifndef ZLINK_DISCOVERY_H_INCLUDED
#define ZLINK_DISCOVERY_H_INCLUDED

#include <zlink/service_common.h>
#include <zlink/actor.h>
#include <zlink/message.h>

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
/*  Discovery API                                                             */
/******************************************************************************/

typedef uint32_t zlink_route_kind_t;

#define ZLINK_ROUTE_KIND_INVALID 0u
#define ZLINK_ROUTE_KIND_ACTOR 1u
#define ZLINK_ROUTE_KIND_SPOT_NAME 2u
#define ZLINK_ROUTE_KIND_ACTOR_SESSION 3u

/**
 * @brief Create a Discovery instance with a fixed auto-connect channel view.
 *
 * The auto-connect type and channel name are fixed at creation time and cannot
 * be changed. All member queries operate within that one logical Discovery
 * channel.
 *
 * @param ctx                Context handle.
 * @param auto_connect_type  Auto-connect topology contract for this handle.
 * @param channel_name       Fixed logical Discovery channel name.
 * @return Discovery handle, or NULL on failure.
 */
ZLINK_EXPORT void *zlink_discovery_new (void *ctx,
                                        zlink_auto_connect_type_t auto_connect_type,
                                        const char *channel_name);

/**
 * @brief Connect Discovery to a Registry bootstrap/control endpoint.
 *
 * Discovery learns the Registry broadcast and topology-uplink endpoints from
 * this bootstrap connection and configures its internal sockets automatically.
 */
ZLINK_EXPORT zlink_connect_result_t zlink_discovery_connect_registry (
  void *discovery, const char *registry_endpoint);

/**
 * @brief Resolve the current owner node for a logical SPOT routing id.
 *
 * This lookup is scoped to the current Discovery service view. Discovery may
 * answer from its local cache or refresh against Registry as needed. On
 * success, the returned node routing id should be paired with the original
 * `spot_rid_` when using router-side SPOT addressing APIs.
 *
 * This helper is intended for send/request destination lookup. Reply paths
 * must continue to use the concrete source addresses that were delivered with
 * the incoming request.
 *
 * @param discovery_          Discovery handle.
 * @param spot_rid_           Logical SPOT routing id to resolve.
 * @param owner_node_rid_out_ Output buffer for the current owner node id.
 * @return `ZLINK_CONFIG_OK` on success, or another
 *         `zlink_config_result_t` value on failure (`zlink_errno()` is set).
 */
ZLINK_EXPORT zlink_config_result_t zlink_discovery_resolve_spot (
  void *discovery_,
  const zlink_routing_id_t *spot_rid_,
  zlink_routing_id_t *owner_node_rid_out_);

ZLINK_EXPORT zlink_config_result_t zlink_discovery_resolve_actor (
  void *discovery_,
  const char *actor_id_,
  zlink_actor_route_t *route_out_);

/**
 * @brief Bind an owner-scoped route row in the connected Registry.
 *
 * The route is owned by the currently registered service participant attached
 * to this Discovery handle. Registry removes the row when that owner
 * registration disappears or is replaced by a newer generation.
 *
 * @param discovery_  Discovery handle.
 * @param kind_       Route kind. `ZLINK_ROUTE_KIND_INVALID` is rejected.
 * @param key_        Route key bytes.
 * @param key_size_   Route key size in bytes.
 * @param value_      Route value bytes. A zero-size value is allowed.
 * @param value_size_ Route value size in bytes.
 * @return `ZLINK_CONFIG_OK` on success, or another result on failure.
 */
ZLINK_EXPORT zlink_config_result_t zlink_discovery_bind_route (
  void *discovery_,
  zlink_route_kind_t kind_,
  const void *key_,
  size_t key_size_,
  const void *value_,
  size_t value_size_);

/**
 * @brief Remove a route row owned by the current service registration.
 *
 * A stale owner registration cannot remove a route row claimed by a newer
 * owner generation for the same route identity.
 */
ZLINK_EXPORT zlink_config_result_t zlink_discovery_unbind_route (
  void *discovery_,
  zlink_route_kind_t kind_,
  const void *key_,
  size_t key_size_);

/**
 * @brief Resolve an owner-scoped route row from the connected Registry.
 *
 * On success `owner_rid_out_` receives the route owner routing id and
 * `value_out_` receives an initialized message containing the route value. The
 * caller must close `value_out_` with `zlink_msg_close`.
 */
ZLINK_EXPORT zlink_config_result_t zlink_discovery_resolve_route (
  void *discovery_,
  zlink_route_kind_t kind_,
  const void *key_,
  size_t key_size_,
  zlink_routing_id_t *owner_rid_out_,
  zlink_msg_t *value_out_);

ZLINK_EXPORT zlink_config_result_t zlink_discovery_set_value (void *discovery_, int64_t value_);
ZLINK_EXPORT zlink_config_result_t zlink_discovery_get_value (void *discovery_,
                                            int64_t *value_out_);
/**
 * @brief Destroy the discovery instance and release all resources.
 *
 * Destroying a discovery also shuts down every attached service participant
 * that delegated lifecycle ownership to this service view.
 */
ZLINK_EXPORT zlink_close_result_t zlink_discovery_destroy (void **discovery_p);

ZLINK_EXPORT zlink_config_result_t zlink_discovery_member_peers (void *discovery_,
                                               zlink_member_peer_entry_t *entries_,
                                               size_t *count_);

#ifdef __cplusplus
}
#endif

#endif
