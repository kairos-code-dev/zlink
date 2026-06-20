/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/framework/contracts/configuration/module.hpp>
#include <zlink/framework/contracts/registry/registry.hpp>
#include <zlink/framework/contracts/spots/spot.hpp>

#include "runtime/spots/spot_runtime.hpp"

#include <atomic>
#include <memory>
#include <thread>
#include <vector>

namespace zlink::framework::runtime
{

class spot_node_host_service_t final : public hosted_service_t
{
  public:
    struct node_runtime_t
    {
        spot_node_snapshot_t snapshot;
        detail::spot_node_runtime_t runtime;
    };

    spot_node_host_service_t (std::vector<node_runtime_t> spot_nodes,
                              discovery_snapshot_t discovery);
    ~spot_node_host_service_t () override;

    void start (service_provider_t &services) override;
    void stop () noexcept override;

  private:
    struct native_node_t;

    std::vector<node_runtime_t> _spot_nodes;
    discovery_snapshot_t _discovery;
    std::vector<std::unique_ptr<native_node_t>> _nodes;
    std::atomic_bool _running{false};
    std::thread _receive_thread;
};

} // namespace zlink::framework::runtime
