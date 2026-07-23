/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include "runtime/channels/channel_runtime.hpp"
#include "runtime/client_server/raw_client_server_owner.hpp"
#include "runtime/locations/location_runtime.hpp"

#include <zlink/framework/contracts/channels/channel.hpp>
#include <zlink/framework/contracts/configuration/services.hpp>
#include <zlink/framework/contracts/handlers/handler_registry.hpp>
#include <zlink/framework/contracts/locations/stores.hpp>

#include <atomic>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace zlink::framework::runtime::client_server
{

mesh::service_node_state_t client_server_service_state (
  framework_runtime_state_t state);
framework_runtime_state_t client_server_framework_state (
  mesh::service_node_state_t state);

class client_server_location_runtime_t
{
  public:
    client_server_location_runtime_t (
      message_bus_t bus,
      std::vector<channel_snapshot_t> channels,
      location_runtime_t &locations,
      client_server_location_store_t &store,
      owner_lease_store_t &leases,
      service_provider_t &services,
      serializer_registry_t &serializers,
      const handler_registry_t &handlers);
    ~client_server_location_runtime_t () noexcept;

    client_server_location_runtime_t (
      const client_server_location_runtime_t &) = delete;
    client_server_location_runtime_t &operator= (
      const client_server_location_runtime_t &) = delete;

    void start ();
    void stop () noexcept;
    bool empty () const noexcept;

  private:
    struct server_entry_t;
    struct client_connection_t;
    struct client_channel_t;

    void start_server (const channel_snapshot_t &channel,
                       const location_owner_token_t &owner);
    void start_client (const channel_snapshot_t &channel);
    void run ();
    void reconcile ();
    void reconcile_channel (client_channel_t &channel);
    void publish_servers ();
    void pump ();
    void dispatch_server (server_entry_t &server);
    void stop_servers () noexcept;
    void stop_clients () noexcept;

    result_t<void> send (const std::string &channel_name,
                         std::string packet_name,
                         std::string content_type,
                         zlink::message_t message,
                         std::chrono::milliseconds timeout);
    result_t<zlink::message_t>
    request (const std::string &channel_name,
             std::string packet_name,
             std::string content_type,
             zlink::message_t message,
             std::chrono::milliseconds timeout);
    std::shared_ptr<raw_client_server_client_t>
    select_ready (const std::string &channel_name,
                  std::chrono::steady_clock::time_point deadline);

    static std::uint64_t make_lifecycle_generation ();
    static std::uint32_t effective_max_message_bytes (
      const channel_capability_snapshot_t &capability);
    static std::vector<std::uint8_t> client_routing_id (
      const channel_snapshot_t &channel);
    static protocol::client_server_server_admission_t to_admission (
      const client_server_server_descriptor_t &descriptor,
      std::uint32_t effective_max_message_bytes);
    static client_server_server_descriptor_t to_descriptor (
      const protocol::client_server_server_admission_t &admission,
      const location_owner_token_t &owner);
    bool owner_is_live (
      const client_server_server_descriptor_t &descriptor) const;

    message_bus_t _bus;
    detail::channel_runtime_t _channel_runtime;
    std::vector<channel_snapshot_t> _channels;
    location_runtime_t *_locations;
    client_server_location_store_t *_store;
    owner_lease_store_t *_leases;
    service_provider_t *_services;
    serializer_registry_t *_serializers;
    const handler_registry_t *_handlers;
    mutable std::mutex _gate;
    std::condition_variable _ready;
    std::map<std::string, std::unique_ptr<server_entry_t>> _servers;
    std::map<std::string, std::unique_ptr<client_channel_t>> _clients;
    std::atomic_bool _stop{false};
    std::thread _thread;
};

} // namespace zlink::framework::runtime::client_server
