/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <exception>
#include <optional>
#include <string>
#include <utility>

namespace zlink::framework
{

enum class framework_error_kind_t
{
    actor_route_not_found = 0,
    actor_create_failed = 1,
    actor_already_exists = 2,
    actor_type_mismatch = 3,
    spot_create_failed = 4,
    spot_route_not_found = 5,
    spot_type_mismatch = 6,
    actor_session_not_bound = 7,
    handler_not_found = 8,
    route_handler_not_found = 9,
    actor_dispatch_handler_not_found = 10,
    payload_decode_failed = 11,
    route_not_connected = 12,
    request_target_not_found = 13,
    request_rejected = 14,
    request_protocol_error = 15,
    request_failed = 16,
    worker_queue_full = 17,
    worker_timed_out = 18,
    worker_failed = 19,
    actor_location_stale = 20,
    actor_create_rejected = 21,
    // C++-only public values; common promotion is pending in the shared 5.2 table.
    actor_stale_generation = 22,
    timeout = 23,
    shutdown = 24,
    disconnected = 25,
    closed = 26
};

class framework_exception_t : public std::exception
{
  public:
    framework_exception_t (framework_error_kind_t kind,
                           std::string message,
                           std::optional<bool> retriable = std::nullopt) :
        _kind (kind),
        _message (std::move (message)),
        _retriable (retriable.value_or (is_retriable_by_default (kind)))
    {
    }

    framework_error_kind_t kind () const noexcept { return _kind; }

    bool is_retriable () const noexcept { return _retriable; }

    const char *what () const noexcept override { return _message.c_str (); }

  private:
    static bool is_retriable_by_default (framework_error_kind_t kind) noexcept
    {
        return kind == framework_error_kind_t::route_not_connected
               || kind == framework_error_kind_t::actor_location_stale;
    }

    framework_error_kind_t _kind;
    std::string _message;
    bool _retriable;
};

} // namespace zlink::framework
