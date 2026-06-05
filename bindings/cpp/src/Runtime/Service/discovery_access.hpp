/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_RUNTIME_SERVICE_DISCOVERY_ACCESS_HPP_INCLUDED
#define ZLINK_CPP_RUNTIME_SERVICE_DISCOVERY_ACCESS_HPP_INCLUDED

#include <zlink/Contracts/Service/discovery.hpp>

namespace zlink
{
namespace detail
{

struct discovery_access_t
{
    static void *native_handle (service::discovery_t &discovery_) noexcept;
    static const void *native_handle (const service::discovery_t &discovery_) noexcept;
};

inline void *native_handle (service::discovery_t &discovery_) noexcept
{
    return discovery_access_t::native_handle (discovery_);
}

inline const void *native_handle (const service::discovery_t &discovery_) noexcept
{
    return discovery_access_t::native_handle (discovery_);
}

} // namespace detail
} // namespace zlink

#endif
