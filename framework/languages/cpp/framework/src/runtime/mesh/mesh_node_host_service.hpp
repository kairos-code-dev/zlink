/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include "runtime/dispatch/offload_executor.hpp"
#include "runtime/mesh/mesh_node_runtime.hpp"

#include <zlink/framework/contracts/configuration/module.hpp>
#include <zlink/framework/contracts/dispatch/execution.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace zlink::framework::detail
{
class route_handler_registry_t;
}

namespace zlink::framework::runtime
{

class mesh_node_host_service_t final : public hosted_service_t
{
  public:
    mesh_node_host_service_t (
      std::vector<std::shared_ptr<detail::mesh_node_builder_state_t>> registrations,
      serializer_registry_t &serializers,
      dispatch_options_t dispatch_options = {});
    ~mesh_node_host_service_t () override;

    void start (service_provider_t &services) override;
    void request_stop () noexcept override;
    void stop () noexcept override;
    std::vector<std::shared_ptr<detail::mesh_node_runtime_t>> nodes () const;
    zlink::submit_result_t submit_local_node_send (
      const std::shared_ptr<detail::mesh_node_runtime_t> &node,
      const std::vector<zlink::message_t> &parts);
    void seal_application_dispatch () noexcept;
    bool wait_for_accepted_callbacks_until (
      std::chrono::steady_clock::time_point deadline) noexcept;

  private:
    std::vector<std::shared_ptr<detail::mesh_node_builder_state_t>> _registrations;
    serializer_registry_t *_serializers;
    dispatch_options_t _dispatch_options;
    service_provider_t *_services = nullptr;
    std::atomic_bool _stop{false};
    std::atomic_bool _accept_application_dispatch{false};
    mutable std::mutex _dispatch_gate_mutex;
    std::condition_variable _dispatch_gate_changed;
    std::uint64_t _active_direct_dispatch = 0;
    std::unique_ptr<offload_executor_t> _application_dispatch;
    std::vector<std::shared_ptr<detail::mesh_node_runtime_t>> _nodes;
    std::vector<std::thread> _threads;
};

} // namespace zlink::framework::runtime
