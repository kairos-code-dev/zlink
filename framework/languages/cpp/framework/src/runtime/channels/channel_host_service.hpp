/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/framework/contracts/channels/channel.hpp>
#include <zlink/framework/contracts/configuration/module.hpp>
#include <zlink/framework/contracts/handlers/handler_registry.hpp>

#include <atomic>
#include <memory>
#include <thread>
#include <vector>

namespace zlink::framework::runtime
{

class channel_host_service_t final : public hosted_service_t
{
  public:
    channel_host_service_t (message_bus_t bus,
                            std::vector<channel_snapshot_t> channels,
                            discovery_snapshot_t discovery,
                            handler_registry_t &handlers,
                            serializer_registry_t &serializers);
    ~channel_host_service_t () override;

    void start (service_provider_t &services) override;
    void stop () noexcept override;

  private:
    class server_loop_t;

    message_bus_t _bus;
    std::vector<channel_snapshot_t> _channels;
    discovery_snapshot_t _discovery;
    handler_registry_t *_handlers;
    serializer_registry_t *_serializers;
    service_provider_t *_services = nullptr;
    std::atomic_bool _stop{false};
    std::vector<std::unique_ptr<server_loop_t>> _loops;
    std::vector<std::thread> _threads;
};

} // namespace zlink::framework::runtime
