/* SPDX-License-Identifier: MPL-2.0 */

#include "runtime/channels/channel_message_pump.hpp"

#include <utility>

namespace zlink::framework::detail
{

channel_message_pump_t::channel_message_pump_t (channel_packet_dispatcher_t dispatcher) :
    _dispatcher (std::move (dispatcher))
{
}

result_t<runtime::messaging::message_parts_t>
channel_message_pump_t::dispatch_server_message (const std::string &channel_name,
                                                 const runtime::messaging::message_parts_t &parts,
                                                 service_provider_t &services,
                                                 serializer_registry_t &serializers,
                                                 const handler_registry_t &handlers) const
{
    return _dispatcher.dispatch_server_message (channel_name, parts, services, serializers, handlers);
}

} // namespace zlink::framework::detail
