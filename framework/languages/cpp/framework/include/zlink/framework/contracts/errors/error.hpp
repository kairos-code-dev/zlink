/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include <exception>
#include <optional>
#include <string>
#include <system_error>
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
    object_client_not_configured = 22,
    mesh_selection_required = 23,
    mesh_not_found = 24,
    invalid_configuration = 25,
    already_submitted = 26,
    actor_generation_stale = 27,
    actor_moving = 28,
    deadline_exceeded = 29,
    placement_capacity_exhausted = 30,
    routing_id_conflict = 31,
    spot_generation_stale = 32,
    spot_moving = 33,
    relocation_data_lost = 34,
    spot_id_conflict = 35
};

namespace detail
{

/* Lifecycle/transport boundary conditions demoted from the public error enum
 * (shared implementation-gap §5.2). They surface at the awaited boundary as
 * the C++ standard convention — std::system_error with the mapped errc — while
 * the internal runtime keeps the precise state. */
enum class boundary_error_t
{
    none = 0,
    timed_out = 1,
    shutdown = 2,
    disconnected = 3,
    closed = 4,
    cancelled = 5,
    stale_generation = 6
};

inline std::error_code boundary_error_code (boundary_error_t state) noexcept
{
    switch (state) {
        case boundary_error_t::timed_out:
            return std::make_error_code (std::errc::timed_out);
        case boundary_error_t::shutdown:
        case boundary_error_t::cancelled:
            return std::make_error_code (std::errc::operation_canceled);
        case boundary_error_t::disconnected:
            return std::make_error_code (std::errc::not_connected);
        case boundary_error_t::closed:
            return std::make_error_code (std::errc::connection_aborted);
        case boundary_error_t::stale_generation:
            return std::make_error_code (std::errc::operation_not_permitted);
        case boundary_error_t::none:
            break;
    }
    return {};
}

} // namespace detail

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

    /* Boundary conditions demoted from the public kind enum surface through
     * the standard error-code facet: an empty code means a plain framework
     * error, otherwise the mapped std::errc identifies the boundary. */
    std::error_code code () const noexcept { return detail::boundary_error_code (_boundary); }

    const char *what () const noexcept override { return _message.c_str (); }

  private:
    friend framework_exception_t detail_make_boundary_exception (detail::boundary_error_t state,
                                                                 std::string message,
                                                                 bool retriable);
    friend detail::boundary_error_t
    detail_boundary_state (const framework_exception_t &error) noexcept;

    static bool is_retriable_by_default (framework_error_kind_t kind) noexcept
    {
        return kind == framework_error_kind_t::route_not_connected
               || kind == framework_error_kind_t::actor_location_stale;
    }

    framework_error_kind_t _kind;
    std::string _message;
    bool _retriable;
    detail::boundary_error_t _boundary = detail::boundary_error_t::none;
};

inline framework_exception_t detail_make_boundary_exception (detail::boundary_error_t state,
                                                             std::string message,
                                                             bool retriable)
{
    framework_exception_t error (framework_error_kind_t::request_failed, std::move (message),
                                 retriable);
    error._boundary = state;
    return error;
}

inline detail::boundary_error_t
detail_boundary_state (const framework_exception_t &error) noexcept
{
    return error._boundary;
}

namespace detail
{

inline framework_exception_t
make_boundary_exception (boundary_error_t state, std::string message, bool retriable = false)
{
    return detail_make_boundary_exception (state, std::move (message), retriable);
}

inline boundary_error_t boundary_state (const framework_exception_t &error) noexcept
{
    return detail_boundary_state (error);
}

} // namespace detail

} // namespace zlink::framework
