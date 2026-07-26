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
    actor_manager_t actor_manager ();
    zlink::submit_result_t submit_local_node_send (
      const std::shared_ptr<detail::mesh_node_runtime_t> &node,
      const std::vector<zlink::message_t> &parts);
    void seal_application_dispatch () noexcept;
    bool wait_for_accepted_callbacks_until (
      std::chrono::steady_clock::time_point deadline) noexcept;
    bool publish_descriptor_state (framework_runtime_state_t state) noexcept;

  private:
    task_t<spot_create_result_t> create_user_spot (
      const std::shared_ptr<detail::mesh_node_runtime_t> &source,
      bool exclusive,
      std::optional<spot_id_t> spot_id,
      std::string stable_type,
      std::optional<std::string> mesh_name,
      std::optional<message_t> request,
      std::chrono::milliseconds timeout);
    task_t<std::optional<spot_ref_t>> find_user_spot (
      spot_id_t spot_id);
    task_t<bool> close_user_spot (
      const std::shared_ptr<detail::mesh_node_runtime_t> &source,
      spot_ref_t spot);
    task_t<actor_create_result_t> create_actor (
      bool exclusive,
      actor_id_t actor_id,
      std::string stable_type,
      std::optional<std::string> mesh_name,
      std::optional<message_t> request,
      std::chrono::milliseconds timeout,
      creation_operation_identity_t operation);
    task_t<std::optional<actor_ref_t>> find_actor (
      actor_id_t actor_id);
    task_t<std::optional<spot_ref_t>> find_actor_spot (
      actor_id_t actor_id);
    task_t<bool> destroy_actor (actor_ref_t actor);

    std::vector<std::shared_ptr<detail::mesh_node_builder_state_t>> _registrations;
    serializer_registry_t *_serializers;
    dispatch_options_t _dispatch_options;
    service_provider_t *_services = nullptr;
    std::shared_ptr<location_store_t> _location_store;
    std::optional<location_owner_token_t> _location_owner;
    std::vector<mesh_node_descriptor_key_t> _published_mesh_nodes;
    std::vector<mesh_node_descriptor_t> _published_mesh_descriptors;
    std::mutex _descriptor_publish_mutex;
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
