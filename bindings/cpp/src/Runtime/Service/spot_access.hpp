/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_RUNTIME_SERVICE_SPOT_ACCESS_HPP_INCLUDED
#define ZLINK_CPP_RUNTIME_SERVICE_SPOT_ACCESS_HPP_INCLUDED

#include <zlink/Contracts/Service/mesh_node.hpp>
#include <zlink/Contracts/Service/spot.hpp>

#include <zlink.h>

namespace zlink
{
namespace detail
{

struct mesh_node_access_t
{
    static void *native_handle (service::mesh_node_t &node_) noexcept;
    static const void *native_handle (const service::mesh_node_t &node_) noexcept;
};

struct spot_access_t
{
    static void *native_handle (service::spot_t &spot_) noexcept;
    static const void *native_handle (const service::spot_t &spot_) noexcept;
    static service::spot_t adopt_native_handle (void *handle_) noexcept;
    static service::spot_status_t status_from_native (const zlink_spot_status_t &native_);
};

inline void *native_handle (service::mesh_node_t &node_) noexcept
{
    return mesh_node_access_t::native_handle (node_);
}

inline const void *native_handle (const service::mesh_node_t &node_) noexcept
{
    return mesh_node_access_t::native_handle (node_);
}

inline void *native_handle (service::spot_t &spot_) noexcept
{
    return spot_access_t::native_handle (spot_);
}

inline const void *native_handle (const service::spot_t &spot_) noexcept
{
    return spot_access_t::native_handle (spot_);
}

} // namespace detail
} // namespace zlink

#endif
