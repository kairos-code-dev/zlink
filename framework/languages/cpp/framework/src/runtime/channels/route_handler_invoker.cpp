/* SPDX-License-Identifier: MPL-2.0 */

#include "runtime/channels/route_handler_invoker.hpp"

namespace zlink::framework::detail
{

result_t<void>
route_handler_invoker_t::invoke_send (
  const route_handler_registry_t &handlers,
  std::string_view router_channel_id,
  std::string_view packet_name,
  service_provider_t &services,
  serializer_registry_t &serializers,
  const zlink::message_t &message,
  const framework::route_handler_context_t &context) const
{
  auto result = handlers.invoke (
    router_channel_id,
    runtime::messaging::message_kind_t::command,
    packet_name,
    services,
    serializers,
    message,
    context);
  if (!result) {
    return result_t<void>::failure (
      result.error_kind (),
      result.error () ? result.error ()->what ()
                      : "routed send handler failed");
  }
  return result_t<void>::success ();
}

result_t<zlink::message_t>
route_handler_invoker_t::invoke_request (
  const route_handler_registry_t &handlers,
  std::string_view router_channel_id,
  std::string_view packet_name,
  service_provider_t &services,
  serializer_registry_t &serializers,
  const zlink::message_t &message,
  const framework::route_handler_context_t &context) const
{
  return handlers.invoke (
    router_channel_id,
    runtime::messaging::message_kind_t::request,
    packet_name,
    services,
    serializers,
    message,
    context);
}

} // namespace zlink::framework::detail
