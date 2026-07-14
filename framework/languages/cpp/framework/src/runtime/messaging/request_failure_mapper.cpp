/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#include "runtime/messaging/request_failure_mapper.hpp"

namespace zlink::framework::runtime::messaging
{

framework_exception_t
request_failure_mapper_t::completion_exception (request_result_t result,
                                                const std::string &operation_name) const
{
    switch (result) {
        case request_result_t::timed_out:
            return detail::make_boundary_exception (detail::boundary_error_t::timed_out,
                                          operation_name + " timed out.");
        case request_result_t::not_connected:
            return framework_exception_t (
              framework_error_kind_t::route_not_connected,
              operation_name + " failed because the target route is not connected.", true);
        case request_result_t::not_found:
            return framework_exception_t (framework_error_kind_t::request_target_not_found,
                                          operation_name
                                            + " failed because the target was not found.");
        case request_result_t::rejected:
            return framework_exception_t (framework_error_kind_t::request_rejected,
                                          operation_name + " was rejected.");
        case request_result_t::conflict:
        case request_result_t::busy:
            return framework_exception_t (framework_error_kind_t::request_rejected,
                                          operation_name + " was rejected.", true);
        case request_result_t::protocol_error:
            return framework_exception_t (framework_error_kind_t::request_protocol_error,
                                          operation_name + " failed with a protocol error.");
        case request_result_t::invalid_argument:
        case request_result_t::invalid_state:
        case request_result_t::not_supported:
        case request_result_t::terminated:
        case request_result_t::internal_error:
            return framework_exception_t (framework_error_kind_t::request_failed,
                                          operation_name + " failed.");
    }
    return framework_exception_t (framework_error_kind_t::request_failed,
                                  operation_name + " failed.");
}

framework_exception_t
request_failure_mapper_t::error_header_exception (const std::string &error_code,
                                                  const std::string &error_message,
                                                  const std::string &operation_name) const
{
    if (error_code == "timeout") {
        return detail::make_boundary_exception (detail::boundary_error_t::timed_out,
                                      error_message.empty () ? operation_name + " timed out."
                                                             : error_message);
    }
    if (error_code == "route_not_connected") {
        return framework_exception_t (framework_error_kind_t::route_not_connected,
                                      error_message.empty ()
                                        ? operation_name
                                            + " failed because the target route is not connected."
                                        : error_message,
                                      true);
    }
    if (error_code == "request_target_not_found") {
        return framework_exception_t (
          framework_error_kind_t::request_target_not_found,
          error_message.empty () ? operation_name + " failed because the target was not found."
                                 : error_message);
    }
    if (error_code == "request_rejected") {
        return framework_exception_t (framework_error_kind_t::request_rejected,
                                      error_message.empty () ? operation_name + " was rejected."
                                                             : error_message);
    }
    if (error_code == "request_protocol_error") {
        return framework_exception_t (framework_error_kind_t::request_protocol_error,
                                      error_message.empty ()
                                        ? operation_name + " failed with a protocol error."
                                        : error_message);
    }
    if (error_code == "handler_not_found") {
        return framework_exception_t (
          framework_error_kind_t::handler_not_found,
          error_message.empty () ? operation_name + " failed because the handler was not found."
                                 : error_message);
    }
    if (error_code == "payload_decode_failed") {
        return framework_exception_t (
          framework_error_kind_t::payload_decode_failed,
          error_message.empty () ? operation_name + " failed because the payload could not be decoded."
                                 : error_message);
    }
    return framework_exception_t (framework_error_kind_t::request_failed,
                                  error_message.empty () ? operation_name + " failed."
                                                         : error_message);
}

} // namespace zlink::framework::runtime::messaging
