/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_RUNTIME_CORE_RUNTIME_HELPERS_HPP_INCLUDED
#define ZLINK_CPP_RUNTIME_CORE_RUNTIME_HELPERS_HPP_INCLUDED

#include <zlink/Contracts/Core/routing_id.hpp>

namespace zlink
{
namespace detail
{

inline void sleep_seconds (int seconds_)
{
    zlink_sleep (seconds_);
}

} // namespace detail
} // namespace zlink

#endif
