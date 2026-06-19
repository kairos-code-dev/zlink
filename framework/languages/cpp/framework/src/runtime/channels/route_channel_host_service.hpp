/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/framework/contracts/channels/channel.hpp>
#include <zlink/framework/contracts/configuration/module.hpp>
#include <zlink/framework/contracts/registry/registry.hpp>

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
    route_channel_host_service_t (
      message_bus_t bus,
      serializer_registry_t &serializers,
      registry_query_t registry,
      discovery_snapshot_t discovery,
      std::map<std::string, std::shared_ptr<detail::route_internal_packet_dispatcher_t>>
        internal_dispatchers = {});
    ~route_channel_host_service_t () override;

    void start (service_provider_t &services) override;
    void stop () noexcept override;

  private:
    class route_loop_t;

    message_bus_t _bus;
    serializer_registry_t *_serializers;
    registry_query_t _registry;
    discovery_snapshot_t _discovery;
    std::map<std::string, std::shared_ptr<detail::route_internal_packet_dispatcher_t>>
      _internal_dispatchers;
    service_provider_t *_services = nullptr;
    std::atomic_bool _stop{false};
    std::vector<std::unique_ptr<route_loop_t>> _loops;
    std::vector<std::thread> _threads;
};

} // namespace zlink::framework::runtime
