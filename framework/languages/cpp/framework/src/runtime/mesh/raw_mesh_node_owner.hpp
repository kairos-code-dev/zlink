/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include "runtime/backend/raw_route_port.hpp"
#include "runtime/foundation/operation_registry.hpp"
#include "runtime/mesh/service_liveness_registry.hpp"
#include "runtime/mesh/service_mailbox.hpp"
#include "runtime/mesh/service_topology_registry.hpp"
#include "runtime/protocol/service_wire_codec.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <map>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace zlink
{
class context_t;
class router_socket_t;
class socket_monitor_t;
}

namespace zlink::framework::runtime::mesh
{

enum class raw_mesh_pump_result_t
{
    no_data,
    infrastructure,
    application,
    backpressured,
    protocol_error
};

struct raw_mesh_node_options_t
{
    service_node_descriptor_t descriptor;
    std::size_t application_message_budget = 4096;
    std::size_t application_byte_budget = 16u * 1024u * 1024u;
    std::size_t infrastructure_message_budget = 1024;
    std::size_t infrastructure_byte_budget = 4u * 1024u * 1024u;
};

struct raw_mesh_byte_vector_less_t
{
    bool operator() (const std::vector<std::uint8_t> &left,
                     const std::vector<std::uint8_t> &right) const noexcept;
};

class raw_mesh_node_owner_t
{
  public:
    explicit raw_mesh_node_owner_t (raw_mesh_node_options_t options);
    ~raw_mesh_node_owner_t () noexcept;

    raw_mesh_node_owner_t (const raw_mesh_node_owner_t &) = delete;
    raw_mesh_node_owner_t &operator= (const raw_mesh_node_owner_t &) = delete;

    void start ();
    void close () noexcept;
    bool started () const noexcept;

    std::string endpoint () const;
    zlink::context_t &context ();
    service_topology_registry_t &topology () noexcept;
    service_liveness_registry_t &liveness () noexcept;
    service_mailbox_t &mailbox () noexcept;

    bool connect_peer (const std::string &endpoint);
    bool connect_peer (const std::string &endpoint,
                       service_node_descriptor_t expected_descriptor);
    peer_admission_result_t admit_peer (
      service_node_descriptor_t descriptor,
      std::vector<std::uint8_t> connection_id,
      service_liveness_registry_t::clock_t::time_point now);
    bool send_to_node (const std::vector<std::uint8_t> &target_routing_id,
                       const protocol::application_payload_t &application_payload);
    bool request_to_node (
      const std::vector<std::uint8_t> &target_routing_id,
      const protocol::application_payload_t &application_payload,
      std::chrono::milliseconds timeout,
      foundation::operation_registry_t::callback_t callback);
    bool request_to_channel (
      const std::string &channel_name,
      const protocol::application_payload_t &application_payload,
      std::chrono::milliseconds timeout,
      foundation::operation_registry_t::callback_t callback);
    bool reply (const service_mailbox_record_t &request,
                const protocol::application_payload_t &application_payload);
    bool reply_failure (const service_mailbox_record_t &request,
                        std::uint32_t terminal_result,
                        std::uint32_t failure_code);
    std::size_t expire_requests (
      foundation::operation_registry_t::clock_t::time_point now);
    bool send_to_channel (const std::string &channel_name,
                          const protocol::application_payload_t &application_payload);
    bool send_to_spot (
      const std::vector<std::uint8_t> &target_routing_id,
      const std::vector<std::uint8_t> &source_spot_routing_id,
      const protocol::spot_route_fence_t &target,
      const protocol::application_payload_t &application_payload);
    bool request_to_spot (
      const std::vector<std::uint8_t> &target_routing_id,
      const std::vector<std::uint8_t> &source_spot_routing_id,
      const protocol::spot_route_fence_t &target,
      const protocol::application_payload_t &application_payload,
      std::chrono::milliseconds timeout,
      foundation::operation_registry_t::callback_t callback);
    bool send_to_actor (
      const std::vector<std::uint8_t> &target_routing_id,
      const std::optional<std::pair<std::string, std::uint64_t>> &source_actor,
      const protocol::actor_route_fence_t &target,
      const protocol::application_payload_t &application_payload);
    bool request_to_actor (
      const std::vector<std::uint8_t> &target_routing_id,
      const std::optional<std::pair<std::string, std::uint64_t>> &source_actor,
      const protocol::actor_route_fence_t &target,
      const protocol::application_payload_t &application_payload,
      std::chrono::milliseconds timeout,
      foundation::operation_registry_t::callback_t callback);
    bool request_user_spot_create (
      const std::vector<std::uint8_t> &target_routing_id,
      protocol::user_spot_create_header_t request,
      std::chrono::milliseconds timeout,
      foundation::operation_registry_t::callback_t callback);
    bool request_user_spot_close (
      const std::vector<std::uint8_t> &target_routing_id,
      protocol::user_spot_close_header_t request,
      std::chrono::milliseconds timeout,
      foundation::operation_registry_t::callback_t callback);
    bool reply_user_spot_create (
      const service_mailbox_record_t &request,
      const protocol::user_spot_create_reply_t &reply,
      std::optional<protocol::application_payload_t>
        application_reply = std::nullopt);
    bool reply_user_spot_close (
      const service_mailbox_record_t &request,
      const protocol::user_spot_close_reply_t &reply);
    raw_mesh_pump_result_t
    pump_one (service_liveness_registry_t::clock_t::time_point now);
    std::size_t drain_monitor_events (
      service_liveness_registry_t::clock_t::time_point now);
    service_liveness_tick_t
    tick_liveness (service_liveness_registry_t::clock_t::time_point now);

  private:
    static std::string owner_key (const std::vector<std::uint8_t> &routing_id);
    static foundation::operation_id_t operation_id (
      std::uint64_t lifecycle_generation,
      std::uint64_t correlation);
    bool request_to_target (
      const std::vector<std::uint8_t> &target_routing_id,
      const protocol::application_payload_t &application_payload,
      std::chrono::milliseconds timeout,
      foundation::operation_registry_t::callback_t callback,
      const std::optional<std::string> &channel_name);
    bool send_with_header (
      const std::vector<std::uint8_t> &target_routing_id,
      std::vector<std::uint8_t> header,
      const protocol::application_payload_t &application_payload);
    bool request_with_header (
      const std::vector<std::uint8_t> &target_routing_id,
      const std::function<std::vector<std::uint8_t> (std::uint64_t)> &header,
      const protocol::application_payload_t &application_payload,
      std::chrono::milliseconds timeout,
      foundation::operation_registry_t::callback_t callback);
    bool request_infrastructure (
      const std::vector<std::uint8_t> &target_routing_id,
      const std::function<std::vector<std::uint8_t> (std::uint64_t)> &header,
      const std::function<std::vector<std::uint8_t> (
        const detail::backend::raw_message_t &)> &decode_reply,
      std::chrono::milliseconds timeout,
      foundation::operation_registry_t::callback_t callback);
    bool reply_infrastructure (
      const service_mailbox_record_t &request,
      std::vector<std::uint8_t> header);

    raw_mesh_node_options_t _options;
    mutable std::mutex _lifecycle_mutex;
    std::mutex _socket_mutex;
    std::unique_ptr<zlink::context_t> _context;
    std::unique_ptr<zlink::router_socket_t> _router;
    std::unique_ptr<zlink::socket_monitor_t> _monitor;
    std::shared_ptr<detail::backend::raw_route_port_t> _port;
    service_topology_registry_t _topology;
    service_liveness_registry_t _liveness;
    service_mailbox_t _mailbox;
    std::shared_ptr<foundation::operation_registry_t> _operations;
    std::map<std::vector<std::uint8_t>, service_node_descriptor_t,
             raw_mesh_byte_vector_less_t>
      _expected_peers;
    std::map<std::vector<std::uint8_t>, std::vector<std::uint8_t>,
             raw_mesh_byte_vector_less_t>
      _connections;
    std::uint64_t _next_correlation = 1;
    bool _closed = false;
};

} // namespace zlink::framework::runtime::mesh
