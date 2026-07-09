/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/framework/contracts/dispatch/task.hpp>
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

class spot_location_resolver_t
{
  public:
    virtual ~spot_location_resolver_t () = default;
    virtual task_t<std::optional<spot_ref_t>>
    resolve_spot_ref (std::string mesh_name, zlink::routing_id_t spot_rid) = 0;
};

class actor_location_resolver_t
{
  public:
    virtual ~actor_location_resolver_t () = default;
    virtual task_t<std::optional<spot_ref_t>> resolve_actor_spot_ref (
      std::string actor_id) = 0;
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
