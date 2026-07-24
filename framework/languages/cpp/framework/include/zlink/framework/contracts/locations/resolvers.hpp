/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include <zlink/framework/contracts/dispatch/task.hpp>
#include <zlink/framework/contracts/locations/spot_handle.hpp>
#include <zlink/framework/contracts/locations/stores.hpp>

namespace zlink::framework
{

class peer_location_resolver_t
{
  public:
    virtual ~peer_location_resolver_t () = default;
    virtual task_t<std::vector<peer_location_t>>
    list_live_peers (peer_location_filter_t filter) = 0;
};

class spot_handle_resolver_t
{
  public:
    virtual ~spot_handle_resolver_t () = default;
    virtual task_t<std::optional<spot_handle_t>>
    resolve_spot_handle (spot_id_t spot_id) = 0;
};

/* Resolves an actor id to an opaque handle for its current spot. The
 * framework keeps the handle's location snapshot current and applies the
 * same safe request refresh rule as a handle resolved directly by spot id. */
class actor_spot_handle_resolver_t
{
  public:
    virtual ~actor_spot_handle_resolver_t () = default;
    virtual task_t<std::optional<spot_handle_t>>
    resolve_actor_spot_handle (std::string actor_id) = 0;
};

class location_readiness_t
{
  public:
    virtual ~location_readiness_t () = default;
    virtual task_t<bool> is_peer_ready (std::string mesh_name,
                                        location_role_t role,
                                        std::optional<zlink::routing_id_t> node_rid = std::nullopt) = 0;
};

} // namespace zlink::framework
