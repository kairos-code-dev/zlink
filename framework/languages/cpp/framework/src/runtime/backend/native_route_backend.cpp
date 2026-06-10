/* SPDX-License-Identifier: MPL-2.0 */

#include "runtime/backend/native_route_backend.hpp"

#include <zlink/Contracts/Errors/errors.hpp>
#include <zlink/Contracts/Service/operation_contracts.hpp>
#include <zlink/Contracts/Sockets/routed_socket_contracts.hpp>

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

} // namespace

native_route_backend_t::native_route_backend_t (zlink::router_socket_t &router) : _router (&router)
{
}

result_t<void>
native_route_backend_t::submit_send (const zlink::routing_id_t &target_node_rid,
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
        auto submit = std::move (_router->send (target_node_rid)).message (copied[0]);
        submit = append_remaining_parts (std::move (submit), copied);
        if (!std::move (submit).submit ()) {
            return submit_failure ("native route send was not accepted");
        }
        return result_t<void>::success ();
    }
    catch (const std::exception &ex) {
        return result_t<void>::failure (framework_error_kind_t::request_failed, ex.what ());
    }
}

result_t<runtime::messaging::message_parts_t>
native_route_backend_t::submit_request (const zlink::routing_id_t &target_node_rid,
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
        auto submit = std::move (_router->request (target_node_rid)).message (copied[0]);
        submit = append_remaining_parts (std::move (submit), copied);
        auto reply = std::move (submit).timeout (timeout).async().get ();
        return result_t<runtime::messaging::message_parts_t>::success (
          runtime::messaging::message_parts_t (std::move (reply)));
    }
    catch (const std::exception &ex) {
        return result_t<runtime::messaging::message_parts_t>::failure (
          framework_error_kind_t::request_failed, ex.what ());
    }
}

} // namespace zlink::framework::detail::backend
