/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "runtime/channels/route_handler_registry.hpp"

namespace zlink::framework::detail
{

class route_handler_invoker_t
{
public:
  result_t<void> invoke_send (
    const route_handler_registry_t &handlers,
    std::string_view router_channel_id,
    std::string_view packet_name,
    service_provider_t &services,
    serializer_registry_t &serializers,
    const zlink::message_t &message,
    const framework::route_handler_context_t &context) const;

  result_t<zlink::message_t> invoke_request (
    const route_handler_registry_t &handlers,
    std::string_view router_channel_id,
    std::string_view packet_name,
    service_provider_t &services,
    serializer_registry_t &serializers,
    const zlink::message_t &message,
    const framework::route_handler_context_t &context) const;
};

} // namespace zlink::framework::detail
