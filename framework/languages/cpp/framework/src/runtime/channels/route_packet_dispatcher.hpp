/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "runtime/channels/route_handler_invoker.hpp"
#include "runtime/channels/route_internal_packet_dispatcher.hpp"
#include "runtime/channels/route_packet.hpp"

#include <optional>
#include <string>

namespace zlink::framework::detail
{

class route_packet_dispatcher_t
{
public:
  explicit route_packet_dispatcher_t (std::string router_channel_id);
  route_packet_dispatcher_t (
    std::string router_channel_id,
    service_provider_t &services,
    serializer_registry_t &serializers,
    const route_handler_registry_t &handlers,
    const route_internal_packet_dispatcher_t &internal_packets);

  result_t<std::optional<route_dispatch_reply_t>> dispatch (
    const route_received_packet_t &received) const;

private:
  result_t<std::optional<route_dispatch_reply_t>> dispatch_send (
    const route_received_packet_t &received,
    const runtime::messaging::envelope_header_t &header) const;

  result_t<std::optional<route_dispatch_reply_t>> dispatch_request (
    const route_received_packet_t &received,
    const runtime::messaging::envelope_header_t &header) const;

  result_t<std::optional<route_dispatch_reply_t>> reply_error (
    const route_received_packet_t &received,
    const runtime::messaging::envelope_header_t &header,
    const framework_exception_t &error) const;

  std::string _router_channel_id;
  service_provider_t *_services = nullptr;
  serializer_registry_t *_serializers = nullptr;
  const route_handler_registry_t *_handlers = nullptr;
  const route_internal_packet_dispatcher_t *_internal_packets = nullptr;
  route_handler_invoker_t _invoker;
};

} // namespace zlink::framework::detail
