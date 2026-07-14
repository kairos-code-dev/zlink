/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include <zlink/framework/contracts/configuration/endpoint_connections.hpp>
#include <zlink/framework/contracts/configuration/module.hpp>
#include <zlink/framework/contracts/locations/runtime_query.hpp>
#include <zlink/framework/contracts/spots/spot.hpp>

#include "runtime/locations/location_runtime.hpp"
#include "runtime/spots/spot_runtime.hpp"

#include <atomic>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

namespace zlink::framework::runtime
{

class live_location_reader_t;

class spot_node_host_service_t final : public hosted_service_t
{
  public:
    struct node_runtime_t
    {
        spot_node_snapshot_t snapshot;
        detail::spot_node_runtime_t runtime;
        /* Role handles for live manual endpoint mutation (endpoint
         * connections contract): attached once the native node exists;
         * attach replays the handle-configured set as the initial connects. */
        std::optional<endpoint_connections_t> router_connections;
        std::optional<endpoint_connections_t> pub_sub_connections;
    };

    explicit spot_node_host_service_t (std::vector<node_runtime_t> spot_nodes);
    ~spot_node_host_service_t () override;

    void start (service_provider_t &services) override;
    void request_stop () noexcept override;
    void stop () noexcept override;

    struct native_node_t;

  private:
    std::vector<node_runtime_t> _spot_nodes;
    location_runtime_t *_location_runtime = nullptr;
    live_location_reader_t *_location_store = nullptr;
    std::vector<std::unique_ptr<native_node_t>> _nodes;
    std::atomic_bool _running{false};
    std::thread _receive_thread;
};

} // namespace zlink::framework::runtime
