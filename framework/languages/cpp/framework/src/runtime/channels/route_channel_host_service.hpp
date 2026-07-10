/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/framework/contracts/channels/channel.hpp>
#include <zlink/framework/contracts/configuration/module.hpp>

#include "runtime/spots/spot_runtime.hpp"

#include <atomic>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace zlink::framework::detail
{
class route_internal_packet_dispatcher_t;
} // namespace zlink::framework::detail

namespace zlink::framework::runtime
{

class route_channel_host_service_t final : public hosted_service_t
{
  public:
    struct spot_node_runtime_t
    {
        spot_node_snapshot_t snapshot;
        detail::spot_node_runtime_t runtime;
    };

    route_channel_host_service_t (
      message_bus_t bus,
      serializer_registry_t &serializers,
      std::vector<spot_node_runtime_t> spot_nodes,
      std::map<std::string, std::shared_ptr<detail::route_internal_packet_dispatcher_t>>
        internal_dispatchers = {});
    ~route_channel_host_service_t () override;

    void start (service_provider_t &services) override;
    void request_stop () noexcept override;
    void stop () noexcept override;

  private:
    class route_loop_t;

    message_bus_t _bus;
    serializer_registry_t *_serializers;
    std::vector<spot_node_runtime_t> _spot_nodes;
    std::map<std::string, std::shared_ptr<detail::route_internal_packet_dispatcher_t>>
      _internal_dispatchers;
    service_provider_t *_services = nullptr;
    std::atomic_bool _stop{false};
    std::vector<std::unique_ptr<route_loop_t>> _loops;
    std::vector<std::thread> _threads;
};

} // namespace zlink::framework::runtime
