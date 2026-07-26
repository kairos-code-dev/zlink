/* SPDX-License-Identifier: FSL-1.1-ALv2 */
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

class location_readiness_t
{
  public:
    virtual ~location_readiness_t () = default;
    virtual task_t<bool> is_peer_ready (std::string mesh_name,
                                        location_role_t role,
                                        std::optional<zlink::routing_id_t> node_rid = std::nullopt) = 0;
};

} // namespace zlink::framework
