/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/Contracts/Core/routing_id.hpp>
#include <zlink/framework/contracts/dispatch/task.hpp>

#include <cstdint>
#include <optional>
#include <string>

namespace zlink::framework::runtime
{

/* Internal continuation of the address rows the public contract no longer
 * exposes: the opaque spot_handle_t owns the caller-facing surface while the
 * runtime keeps routing on full (mesh, node rid, spot id) addresses. */
struct spot_address_t
{
    std::string mesh_name;
    zlink::routing_id_t node_rid = zlink::routing_id_t::from (std::uint32_t{0});
    std::string spot_id;
    std::uint64_t spot_generation = 0;
};

class spot_address_resolver_t
{
  public:
    virtual ~spot_address_resolver_t () = default;
    virtual task_t<std::optional<spot_address_t>>
    resolve_spot_address (std::string mesh_name, std::string spot_id) = 0;
};

class actor_address_resolver_t
{
  public:
    virtual ~actor_address_resolver_t () = default;
    virtual task_t<std::optional<spot_address_t>>
    resolve_actor_address (std::string actor_id) = 0;
};

} // namespace zlink::framework::runtime
