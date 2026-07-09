/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/Contracts/Core/routing_id.hpp>

#include <cstdint>
#include <string>

namespace zlink::framework
{

struct spot_ref_t
{
    std::string mesh_name;
    zlink::routing_id_t node_rid = zlink::routing_id_t::from (std::uint32_t{0});
    zlink::routing_id_t spot_rid = zlink::routing_id_t::from (std::uint32_t{0});
};

} // namespace zlink::framework
