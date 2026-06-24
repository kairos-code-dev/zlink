/* SPDX-License-Identifier: MPL-2.0 */

#include "runtime/backend/native_route_backend.hpp"

#include <zlink/Contracts/Errors/errors.hpp>
#include <zlink/Contracts/Service/operation_contracts.hpp>
#include <zlink/Contracts/Sockets/routed_socket_contracts.hpp>

#include <cerrno>
#include <utility>
#include <vector>

namespace zlink::framework::detail::backend
{

namespace
{

std::vector<zlink::message_t> copy_parts (const runtime::messaging::message_parts_t &parts)
{
    std::vector<zlink::message_t> copied;
    copied.reserve (parts.size ());
    for (const auto &part : parts.items ()) {
        copied.push_back (part);
    }
    return copied;
}

template <typename TOperation>
auto append_remaining_parts (TOperation operation, std::vector<zlink::message_t> &parts)
{
    for (std::size_t index = 1; index < parts.size (); ++index) {
        operation = std::move (operation).message (parts[index]);
    }
    return operation;
}

result_t<void> submit_failure (const char *message)
{
    return result_t<void>::failure (framework_error_kind_t::request_failed, message);
}

bool is_route_unreachable_errno (int value)
{
    return value == EHOSTUNREACH || value == ENETUNREACH || value == ECONNREFUSED
           || value == ENOTCONN;
}

framework_exception_t map_native_route_exception (const std::exception &error)
{
    if (const auto *request_error = dynamic_cast<const zlink::request_error_t *> (&error);
        request_error != nullptr) {
        if (request_error->result () == zlink::request_result_t::timed_out) {
            return framework_exception_t (framework_error_kind_t::timeout,
                                          "route request timed out");
        }
        if (request_error->result () == zlink::request_result_t::not_connected
            || is_route_unreachable_errno (request_error->internal_errno ())) {
            return framework_exception_t (framework_error_kind_t::route_not_connected,
                                          request_error->what ());
        }
        return framework_exception_t (framework_error_kind_t::request_failed,
                                      request_error->what ());
    }
    if (const auto *submit_error = dynamic_cast<const zlink::submit_error_t *> (&error);
        submit_error != nullptr) {
        if (submit_error->result () == zlink::submit_result_t::not_connected
            || is_route_unreachable_errno (submit_error->internal_errno ())) {
            return framework_exception_t (framework_error_kind_t::route_not_connected,
                                          submit_error->what ());
        }
    }
    return framework_exception_t (framework_error_kind_t::request_failed, error.what ());
}

} // namespace

native_route_backend_t::native_route_backend_t (zlink::router_socket_t &router) : _router (&router)
{
}

void native_route_backend_t::attach_spot_route_bridge (
  std::unique_ptr<zlink::service::spot_route_bridge_t> bridge,
  std::string channel_name)
{
    _spot_route_bridge = std::move (bridge);
    _spot_route_channel_name = std::move (channel_name);
}

result_t<void>
native_route_backend_t::submit_send (const zlink::routing_id_t &target_node_rid,
                                     const std::optional<zlink::routing_id_t> &target_spot_rid,
                                     const runtime::messaging::message_parts_t &parts)
{
    if (_router == nullptr) {
        return result_t<void>::failure (framework_error_kind_t::request_protocol_error,
                                        "native route backend has no router socket");
    }
    auto copied = copy_parts (parts);
    if (copied.empty ()) {
        return result_t<void>::failure (framework_error_kind_t::request_protocol_error,
                                        "native route send requires at least one message part");
    }
    try {
        if (target_spot_rid && _spot_route_bridge) {
            if (!_spot_route_bridge->send (_spot_route_channel_name, target_node_rid,
                                           *target_spot_rid, copied, zlink::send_flags_t::none)) {
                return submit_failure ("native route bridge send was not accepted");
            }
            return result_t<void>::success ();
        }
        auto submit = std::move (_router->send (target_node_rid)).message (copied[0]);
        submit = append_remaining_parts (std::move (submit), copied);
        if (!std::move (submit).submit ()) {
            return submit_failure ("native route send was not accepted");
        }
        return result_t<void>::success ();
    }
    catch (const std::exception &ex) {
        const auto error = map_native_route_exception (ex);
        return result_t<void>::failure (error.kind (), error.what (), error.is_retriable ());
    }
}

result_t<runtime::messaging::message_parts_t>
native_route_backend_t::submit_request (const zlink::routing_id_t &target_node_rid,
                                        const std::optional<zlink::routing_id_t> &target_spot_rid,
                                        const runtime::messaging::message_parts_t &parts,
                                        std::chrono::milliseconds timeout)
{
    if (_router == nullptr) {
        return result_t<runtime::messaging::message_parts_t>::failure (
          framework_error_kind_t::request_protocol_error,
          "native route backend has no router socket");
    }
    auto copied = copy_parts (parts);
    if (copied.empty ()) {
        return result_t<runtime::messaging::message_parts_t>::failure (
          framework_error_kind_t::request_protocol_error,
          "native route request requires at least one message part");
    }
    try {
        if (target_spot_rid && _spot_route_bridge) {
            auto reply =
              _spot_route_bridge->request (_spot_route_channel_name, target_node_rid,
                                           *target_spot_rid, copied, timeout)
                .get ();
            return result_t<runtime::messaging::message_parts_t>::success (
              runtime::messaging::message_parts_t (std::move (reply)));
        }
        auto submit = std::move (_router->request (target_node_rid)).message (copied[0]);
        submit = append_remaining_parts (std::move (submit), copied);
        auto reply = std::move (submit).timeout (timeout).async().get ();
        return result_t<runtime::messaging::message_parts_t>::success (
          runtime::messaging::message_parts_t (std::move (reply)));
    }
    catch (const std::exception &ex) {
        const auto error = map_native_route_exception (ex);
        return result_t<runtime::messaging::message_parts_t>::failure (
          error.kind (), error.what (), error.is_retriable ());
    }
}

bool native_route_backend_t::handle_router_received (
  const zlink::routing_id_t &source_node_rid,
  std::vector<zlink::message_t> &parts,
  std::optional<std::uint64_t> request_seq)
{
    if (!_spot_route_bridge) {
        return false;
    }
    return _spot_route_bridge->handle_router_received (_spot_route_channel_name,
                                                       source_node_rid, parts, request_seq);
}

} // namespace zlink::framework::detail::backend
