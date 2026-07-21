/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include "runtime/channels/route_handler_registry.hpp"
#include "runtime/channels/route_internal_packet_dispatcher.hpp"

#include <zlink/Contracts/Service/dispatch.hpp>
#include <zlink/framework/contracts/dispatch/execution.hpp>

#include <vector>

namespace zlink::framework::detail
{

class mesh_record_dispatcher_t
{
  public:
    mesh_record_dispatcher_t (service_provider_t &services,
                              serializer_registry_t &serializers,
                              const route_handler_registry_t &handlers,
                              dispatch_options_t dispatch_options = {});

    result_t<void> dispatch (const zlink::service::receive_record_t &record,
                             std::vector<zlink::message_t> parts) const;

  private:
    service_provider_t *_services;
    serializer_registry_t *_serializers;
    const route_handler_registry_t *_handlers;
    dispatch_options_t _dispatch_options;
    no_route_internal_packet_dispatcher_t _no_internal_packets;
};

} // namespace zlink::framework::detail
