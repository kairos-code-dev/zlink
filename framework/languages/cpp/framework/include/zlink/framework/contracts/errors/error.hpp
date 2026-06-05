/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <exception>
#include <string>
#include <utility>

namespace zlink::framework
{

enum class framework_error_kind_t
{
    actor_route_not_found,
    actor_create_failed,
    actor_already_exists,
    actor_type_mismatch,
    spot_create_failed,
    spot_route_not_found,
    spot_type_mismatch,
    actor_session_not_bound,
    handler_not_found,
    route_handler_not_found,
    actor_dispatch_handler_not_found,
    payload_decode_failed,
    route_not_connected,
    request_target_not_found,
    request_rejected,
    request_protocol_error,
    request_failed,
    timeout,
    shutdown,
    disconnected,
    closed
};

class framework_exception_t : public std::exception
{
  public:
    framework_exception_t (framework_error_kind_t kind, std::string message, bool retriable = false) :
        _kind (kind), _message (std::move (message)), _retriable (retriable)
    {
    }

    framework_error_kind_t kind () const noexcept { return _kind; }

    bool is_retriable () const noexcept { return _retriable; }

    const char *what () const noexcept override { return _message.c_str (); }

  private:
    framework_error_kind_t _kind;
    std::string _message;
    bool _retriable;
};

} // namespace zlink::framework
