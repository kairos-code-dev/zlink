/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_RUNTIME_SERVICE_SPOT_ACCESS_HPP_INCLUDED
#define ZLINK_CPP_RUNTIME_SERVICE_SPOT_ACCESS_HPP_INCLUDED

#include <zlink/Contracts/Service/spot.hpp>

namespace zlink
{
namespace detail
{

struct spot_node_access_t
{
    static void *native_handle (service::spot_node_t &node_) noexcept;
    static const void *
    native_handle (const service::spot_node_t &node_) noexcept;
};

struct spot_access_t
{
    static void *native_handle (service::spot_t &spot_) noexcept;
    static const void *native_handle (const service::spot_t &spot_) noexcept;
    static service::spot_t adopt_native_handle (void *handle_) noexcept;
};

inline void *native_handle (service::spot_node_t &node_) noexcept
{
    return spot_node_access_t::native_handle (node_);
}

inline const void *native_handle (const service::spot_node_t &node_) noexcept
{
    return spot_node_access_t::native_handle (node_);
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
