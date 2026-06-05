/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "runtime/channels/channel_packet_dispatcher.hpp"

#include <zlink/framework/contracts/errors/result.hpp>

namespace zlink::framework::detail
{

class channel_message_pump_t
{
  public:
    explicit channel_message_pump_t (channel_packet_dispatcher_t dispatcher);

    result_t<runtime::messaging::message_parts_t>
    dispatch_server_message (const std::string &channel_name,
                             const runtime::messaging::message_parts_t &parts,
                             service_provider_t &services,
                             serializer_registry_t &serializers,
                             const handler_registry_t &handlers) const;

  private:
    channel_packet_dispatcher_t _dispatcher;
};

} // namespace zlink::framework::detail
