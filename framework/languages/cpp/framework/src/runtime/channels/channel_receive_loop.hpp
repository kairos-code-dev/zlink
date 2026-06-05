/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "runtime/channels/channel_message_pump.hpp"
#include "runtime/channels/channel_runtime_bundle.hpp"

#include <deque>

namespace zlink::framework::detail
{

struct channel_receive_result_t
{
    std::size_t dispatched = 0;
    std::vector<runtime::messaging::message_parts_t> replies;
};

class channel_receive_loop_t
{
  public:
    channel_receive_loop_t (channel_runtime_bundle_t &bundle, channel_message_pump_t pump);

    void enqueue_server_message (runtime::messaging::message_parts_t parts);

    result_t<channel_receive_result_t> drain_server_messages (const std::string &channel_name,
                                                              service_provider_t &services,
                                                              serializer_registry_t &serializers,
                                                              const handler_registry_t &handlers);

    std::size_t pending_message_count () const noexcept;

  private:
    channel_runtime_bundle_t &_bundle;
    channel_message_pump_t _pump;
    std::deque<runtime::messaging::message_parts_t> _server_messages;
};

} // namespace zlink::framework::detail
