/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include <zlink/framework.hpp>

#include <string>
#include <system_error>

namespace zlink::framework::e2e::resilience_lifecycle
{

template <typename T> std::string public_error_type (const result_t<T> &result)
{
    if (const auto *error = result.error (); error != nullptr
        && error->code () == std::make_error_code (std::errc::timed_out)) {
        return "TimeoutException";
    }
    switch (result.error_kind ()) {
        case framework_error_kind_t::route_not_connected:
            return "RouteNotConnected";
        case framework_error_kind_t::request_target_not_found:
            return "RequestTargetNotFound";
        case framework_error_kind_t::request_rejected:
            return "RequestRejected";
        case framework_error_kind_t::request_failed:
            return "RequestFailed";
        case framework_error_kind_t::handler_not_found:
            return "HandlerNotFound";
        default:
            return "UnexpectedFrameworkError";
    }
}

} // namespace zlink::framework::e2e::resilience_lifecycle
