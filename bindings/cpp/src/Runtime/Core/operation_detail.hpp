/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_RUNTIME_CORE_OPERATION_DETAIL_HPP_INCLUDED
#define ZLINK_CPP_RUNTIME_CORE_OPERATION_DETAIL_HPP_INCLUDED

#include <zlink/Contracts/Messaging/received.hpp>
#include "../Messaging/received_access.hpp"
#include "routing_id_access.hpp"
#include "../Native/native_parts.hpp"

#include <chrono>
#include <functional>

namespace zlink
{
namespace detail
{

inline std::function<void ()> make_socket_request_progress (void *socket_)
{
    return zlink::detail::make_request_progress_callback (socket_);
}

inline std::chrono::milliseconds
resolve_timeout (std::chrono::milliseconds requested_,
                 std::chrono::milliseconds fallback_) noexcept
{
    return requested_ == std::chrono::milliseconds () ? fallback_ : requested_;
}

inline received_t make_received (
  const zlink_routing_id_t *routing_id_,
  const zlink_routing_id_t *spot_rid_,
  uint64_t request_seq_,
  bool has_request_seq_,
  zlink_msg_t *parts_,
  size_t part_count_,
  std::function<void (std::vector<message_t> &, send_flags_t)> reply_fn_ =
    std::function<void (std::vector<message_t> &, send_flags_t)> ())
{
    return zlink::detail::received_access_t::make (
      (routing_id_ && routing_id_->size > 0)
        ? std::optional<routing_id_t> (
            zlink::detail::native_routing_id (*routing_id_))
        : std::nullopt,
      (spot_rid_ && spot_rid_->size > 0)
        ? std::optional<routing_id_t> (
            zlink::detail::native_routing_id (*spot_rid_))
        : std::nullopt,
      has_request_seq_ ? std::optional<uint64_t> (request_seq_) : std::nullopt,
      detail::take_parts_from_native (parts_, part_count_),
      std::move (reply_fn_));
}

} // namespace detail
} // namespace zlink

#endif
