/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include "runtime/foundation/operation_registry.hpp"
#include "runtime/protocol/service_wire_codec.hpp"

#include <zlink/framework/contracts/errors/error.hpp>

namespace zlink::framework::runtime::user_spot_terminal
{

inline framework_error_kind_t map_user_spot_wire_failure (
  const protocol::reply_header_t &header,
  bool creation)
{
    if (header.terminal_result == 101)
        return framework_error_kind_t::deadline_exceeded;
    if (header.terminal_result == 109)
        return framework_error_kind_t::route_not_connected;
    if (creation
        && (header.terminal_result == 108
            || header.terminal_result == 113)
        && header.failure_code == 0)
        return framework_error_kind_t::placement_capacity_exhausted;

    switch (
      static_cast<protocol::framework_error_code> (
        header.failure_code)) {
        case protocol::framework_error_code::spotCreateFailed:
            return framework_error_kind_t::spot_create_failed;
        case protocol::framework_error_code::spotRouteNotFound:
            return framework_error_kind_t::spot_route_not_found;
        case protocol::framework_error_code::spotTypeMismatch:
            return framework_error_kind_t::spot_type_mismatch;
        case protocol::framework_error_code::requestRejected:
            return framework_error_kind_t::request_rejected;
        case protocol::framework_error_code::requestProtocolError:
            return framework_error_kind_t::request_protocol_error;
        case protocol::framework_error_code::requestFailed:
            return framework_error_kind_t::request_failed;
        case protocol::framework_error_code::workerQueueFull:
            return creation
                     ? framework_error_kind_t::
                        placement_capacity_exhausted
                     : framework_error_kind_t::worker_queue_full;
        case protocol::framework_error_code::workerTimedOut:
            return framework_error_kind_t::worker_timed_out;
        case protocol::framework_error_code::workerFailed:
            return framework_error_kind_t::worker_failed;
        case protocol::framework_error_code::spotGenerationStale:
            return framework_error_kind_t::spot_generation_stale;
        case protocol::framework_error_code::spotMoving:
            return framework_error_kind_t::spot_moving;
        case protocol::framework_error_code::relocationDataLost:
            return framework_error_kind_t::relocation_data_lost;
        default:
            break;
    }

    if (header.terminal_result == 103
        || header.terminal_result == 106)
        return framework_error_kind_t::request_rejected;
    return framework_error_kind_t::request_failed;
}

inline framework_error_kind_t map_user_spot_operation_failure (
  foundation::operation_terminal_t terminal,
  const protocol::reply_header_t &header,
  bool creation)
{
    switch (terminal) {
        case foundation::operation_terminal_t::completed:
            return map_user_spot_wire_failure (
              header, creation);
        case foundation::operation_terminal_t::timed_out:
            return framework_error_kind_t::deadline_exceeded;
        case foundation::operation_terminal_t::transport_failed:
            return framework_error_kind_t::route_not_connected;
        case foundation::operation_terminal_t::cancelled:
        case foundation::operation_terminal_t::shutdown:
            return framework_error_kind_t::request_rejected;
    }
    return framework_error_kind_t::request_failed;
}

} // namespace zlink::framework::runtime::user_spot_terminal
