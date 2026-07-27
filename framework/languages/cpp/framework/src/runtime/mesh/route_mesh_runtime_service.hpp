/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include "runtime/mesh/mesh_node_runtime.hpp"

#include <zlink/framework/contracts/configuration/module.hpp>
#include <zlink/framework/contracts/locations/runtime_query.hpp>
#include <zlink/framework/contracts/locations/stores.hpp>
#include <zlink/framework/contracts/monitoring/route_mesh_runtime.hpp>

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace zlink::framework::runtime
{

class route_mesh_runtime_service_t final : public route_mesh_runtime_t
{
  public:
    struct state_t;

    using drain_callback_t =
      std::function<task_t<drain_result_t> (std::chrono::milliseconds)>;
    using await_drained_callback_t = std::function<task_t<drain_result_t> ()>;

    route_mesh_runtime_service_t (
      std::vector<std::shared_ptr<detail::mesh_node_runtime_t>> nodes,
      location_runtime_query_t *location_runtime,
      drain_callback_t drain,
      await_drained_callback_t await_drained,
      location_store_t *location_store = nullptr);
    ~route_mesh_runtime_service_t ();

    mesh_node_snapshot_t snapshot (std::string mesh_name) const override;
    std::unique_ptr<mesh_runtime_observation_t>
    observe (std::string mesh_name,
             std::size_t capacity,
             std::function<void (const mesh_runtime_event_t &)> observer) override;
    bool is_ready (std::string mesh_name) const override;
    task_t<drain_result_t>
    drain (std::string mesh_name, std::chrono::milliseconds deadline) override;
    task_t<drain_result_t> await_drained (std::string mesh_name) override;

    void start ();
    void stop () noexcept;

  private:
    std::shared_ptr<state_t> _state;
};

class route_mesh_runtime_host_service_t final : public hosted_service_t
{
  public:
    explicit route_mesh_runtime_host_service_t (
      std::shared_ptr<route_mesh_runtime_service_t> runtime);

    void start (service_provider_t &) override;
    void request_stop () noexcept override;
    void stop () noexcept override;

  private:
    std::shared_ptr<route_mesh_runtime_service_t> _runtime;
};

} // namespace zlink::framework::runtime
