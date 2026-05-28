/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_RUNTIME_SERVICE_REGISTRY_ACCESS_HPP_INCLUDED
#define ZLINK_CPP_RUNTIME_SERVICE_REGISTRY_ACCESS_HPP_INCLUDED

#include <zlink/Contracts/Service/registry.hpp>

namespace zlink
{
namespace detail
{

struct registry_access_t
{
    static void *native_handle (service::registry_t &registry_) noexcept;
    static const void *
    native_handle (const service::registry_t &registry_) noexcept;
    static void *
    native_handle (service::registry_query_client_t &client_) noexcept;
    static const void *
    native_handle (const service::registry_query_client_t &client_) noexcept;
};

inline void *native_handle (service::registry_t &registry_) noexcept
{
    return registry_access_t::native_handle (registry_);
}

inline const void *native_handle (const service::registry_t &registry_) noexcept
{
    return registry_access_t::native_handle (registry_);
}

inline void *native_handle (service::registry_query_client_t &client_) noexcept
{
    return registry_access_t::native_handle (client_);
}

inline const void *
native_handle (const service::registry_query_client_t &client_) noexcept
{
    return registry_access_t::native_handle (client_);
}

} // namespace detail
} // namespace zlink

#endif
