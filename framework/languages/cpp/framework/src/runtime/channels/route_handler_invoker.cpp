/* SPDX-License-Identifier: MPL-2.0 */

#include "runtime/channels/route_handler_invoker.hpp"

namespace zlink::framework::detail
{

task_t<void>
route_handler_invoker_t::invoke_send (const route_handler_registry_t &handlers,
                                      std::string_view router_channel_id,
                                      std::string_view packet_name,
                                      service_provider_t &services,
                                      serializer_registry_t &serializers,
                                      const zlink::message_t &message,
                                      const framework::route_handler_context_t &context) const
{
    try {
        auto invocation_scope = services.create_scope (service_scope_kind_t::handler_invocation);
        auto &invocation_services = invocation_scope.provider ();
        auto route_task = handlers.invoke_async (
          router_channel_id, runtime::messaging::message_kind_t::command, packet_name,
          invocation_services, serializers, message, context);
        auto result = route_task.result ();
        if (!result) {
            return task_t<void> (result_t<void>::failure (
              result.error_kind (),
              result.error () ? result.error ()->what () : "routed send handler failed"));
        }
        return task_t<void> (result_t<void>::success ());
    }
    catch (const framework_exception_t &error) {
        return task_t<void> (
          result_t<void>::failure (error.kind (), error.what (), error.is_retriable ()));
    }
    catch (...) {
        return task_t<void> (result_t<void>::failure (
          framework_error_kind_t::request_failed, "routed send handler threw an exception"));
    }
}

task_t<zlink::message_t>
route_handler_invoker_t::invoke_request (const route_handler_registry_t &handlers,
                                         std::string_view router_channel_id,
                                         std::string_view packet_name,
                                         service_provider_t &services,
                                         serializer_registry_t &serializers,
                                         const zlink::message_t &message,
                                         const framework::route_handler_context_t &context) const
{
    try {
        auto invocation_scope = services.create_scope (service_scope_kind_t::handler_invocation);
        auto &invocation_services = invocation_scope.provider ();
        auto handler_task = handlers.invoke_async (
          router_channel_id, runtime::messaging::message_kind_t::request, packet_name,
          invocation_services, serializers, message, context);
        return task_t<zlink::message_t> (handler_task.result ());
    }
    catch (const framework_exception_t &error) {
        return task_t<zlink::message_t> (result_t<zlink::message_t>::failure (
          error.kind (), error.what (), error.is_retriable ()));
    }
    catch (...) {
        return task_t<zlink::message_t> (result_t<zlink::message_t>::failure (
          framework_error_kind_t::request_failed, "routed request handler threw an exception"));
    }
}

} // namespace zlink::framework::detail
