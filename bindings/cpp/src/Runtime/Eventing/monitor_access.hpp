/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_RUNTIME_EVENTING_MONITOR_ACCESS_HPP_INCLUDED
#define ZLINK_CPP_RUNTIME_EVENTING_MONITOR_ACCESS_HPP_INCLUDED

#include <zlink/Contracts/Eventing/monitor.hpp>

namespace zlink
{
namespace detail
{

struct monitor_access_t
{
    static void *native_handle (monitor_handle_t &monitor_) noexcept;
    static const void *
    native_handle (const monitor_handle_t &monitor_) noexcept;
};

inline void *native_handle (monitor_handle_t &monitor_) noexcept
{
    return monitor_access_t::native_handle (monitor_);
}

inline const void *native_handle (const monitor_handle_t &monitor_) noexcept
{
    return monitor_access_t::native_handle (monitor_);
}

} // namespace detail
} // namespace zlink

#endif
