/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_API_SERVICE_HANDLE_INTERNAL_HPP_INCLUDED__
#define __ZLINK_API_SERVICE_HANDLE_INTERNAL_HPP_INCLUDED__

struct spot_handle_t;

namespace zlink
{
class discovery_t;
class registry_t;
class spot_node_t;

enum service_handle_kind_t
{
    service_handle_unknown = 0,
    service_handle_discovery,
    service_handle_registry,
    service_handle_spot_pub_side,
    service_handle_spot_sub_side,
    service_handle_spot,
    service_handle_spot_node
};

struct service_handle_resolution_t
{
    service_handle_resolution_t ();

    service_handle_kind_t kind;
    discovery_t *discovery;
    registry_t *registry;
};

service_handle_resolution_t resolve_service_handle (void *handle_);
}

bool is_registered_discovery_handle (void *discovery_);
bool is_registered_registry_handle (void *registry_);
bool is_registered_spot_pub_side_handle (void *pub_);
bool is_registered_spot_sub_side_handle (void *sub_);
bool is_registered_spot_node_handle (void *node_);
bool is_registered_spot_handle (void *spot_);

void register_discovery_handle (void *discovery_);
void erase_discovery_handle (void *discovery_);
void register_registry_handle (void *registry_);
void erase_registry_handle (void *registry_);
void register_spot_pub_side_handle (void *pub_);
void erase_spot_pub_side_handle (void *pub_);
void register_spot_sub_side_handle (void *sub_);
void erase_spot_sub_side_handle (void *sub_);
void register_spot_node_mode_state (zlink::spot_node_t *node_);
void register_spot_mode_state (spot_handle_t *spot_);
void erase_spot_node_mode_state (zlink::spot_node_t *node_);
void erase_spot_mode_state (spot_handle_t *spot_);

#endif
