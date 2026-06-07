/* SPDX-License-Identifier: MPL-2.0 */

#include "runtime/channels/route_handler_invoker.hpp"

#include "runtime/dispatch/coroutine_executor.hpp"

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
    detail::task_completion_source_t<void> completion;
    auto task = completion.task ();
    auto owned_router_channel_id = std::string (router_channel_id);
    auto owned_packet_name = std::string (packet_name);
    auto owned_message = message;
    auto owned_context = context;
    auto handler_task = runtime::handler_coroutine_executor ().submit<zlink::message_t> (
      [&handlers, &services, &serializers,
       owned_router_channel_id = std::move (owned_router_channel_id),
       owned_packet_name = std::move (owned_packet_name), owned_message = std::move (owned_message),
       owned_context =
         std::move (owned_context)] () -> boost::asio::awaitable<result_t<zlink::message_t>> {
          try {
              auto route_task = handlers.invoke_async (
                owned_router_channel_id, runtime::messaging::message_kind_t::command,
                owned_packet_name, services, serializers, owned_message, owned_context);
              co_return result_t<zlink::message_t>::success (
                (co_await runtime::await_task_result (std::move (route_task))).value ());
          }
          catch (const framework_exception_t &error) {
              co_return result_t<zlink::message_t>::failure (error.kind (), error.what (),
                                                             error.is_retriable ());
          }
          catch (...) {
              co_return result_t<zlink::message_t>::failure (
                framework_error_kind_t::request_failed, "routed send handler threw an exception");
          }
      });
    detail::observe_task_completion (
      handler_task,
      [completion = std::move (completion)] (const result_t<zlink::message_t> &result) mutable {
          if (!result) {
              completion.complete (result_t<void>::failure (
                result.error_kind (),
                result.error () ? result.error ()->what () : "routed send handler failed"));
              return;
          }
          completion.complete (result_t<void>::success ());
      });
    return task;
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
    auto owned_router_channel_id = std::string (router_channel_id);
    auto owned_packet_name = std::string (packet_name);
    auto owned_message = message;
    auto owned_context = context;
    return runtime::handler_coroutine_executor ().submit<zlink::message_t> (
      [&handlers, &services, &serializers,
       owned_router_channel_id = std::move (owned_router_channel_id),
       owned_packet_name = std::move (owned_packet_name), owned_message = std::move (owned_message),
       owned_context =
         std::move (owned_context)] () -> boost::asio::awaitable<result_t<zlink::message_t>> {
          try {
              auto handler_task = handlers.invoke_async (
                owned_router_channel_id, runtime::messaging::message_kind_t::request,
                owned_packet_name, services, serializers, owned_message, owned_context);
              co_return result_t<zlink::message_t>::success (
                (co_await runtime::await_task_result (std::move (handler_task))).value ());
          }
          catch (const framework_exception_t &error) {
              co_return result_t<zlink::message_t>::failure (error.kind (), error.what (),
                                                             error.is_retriable ());
          }
          catch (...) {
              co_return result_t<zlink::message_t>::failure (
                framework_error_kind_t::request_failed,
                "routed request handler threw an exception");
          }
      });
}

} // namespace zlink::framework::detail
