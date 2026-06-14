/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_RUNTIME_CORE_OPERATION_DETAIL_HPP_INCLUDED
#define ZLINK_CPP_RUNTIME_CORE_OPERATION_DETAIL_HPP_INCLUDED

#include <zlink/Contracts/Messaging/received.hpp>
#include "../Messaging/received_access.hpp"
#include "routing_id_access.hpp"
#include "../Native/native_message_parts.hpp"
#include "../Native/request_progress.hpp"
#include "duration_conversion.hpp"

#include <chrono>
#include <cstdint>
#include <functional>

namespace zlink
{
namespace detail
{

inline std::function<void ()> make_socket_request_progress (void *socket_)
{
    return zlink::detail::make_request_progress_callback (socket_);
}

inline std::chrono::milliseconds resolve_timeout (std::chrono::milliseconds requested_,
                                                  std::chrono::milliseconds fallback_) noexcept
{
    return requested_ == std::chrono::milliseconds () ? fallback_ : requested_;
}

// Resolves a requested request timeout against its fallback and converts the
// result to the native millisecond representation in one step. Centralizes the
// request-timeout encoding that the spot request surface applies identically.
inline uint32_t native_request_timeout_ms (std::chrono::milliseconds requested_,
                                           std::chrono::milliseconds fallback_)
{
    return native_timeout_ms (resolve_timeout (requested_, fallback_));
}

} // namespace detail
} // namespace zlink

#endif
