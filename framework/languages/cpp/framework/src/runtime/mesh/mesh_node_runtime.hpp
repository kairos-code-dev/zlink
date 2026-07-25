/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include <zlink/framework/contracts/configuration/mesh_node.hpp>

#include "runtime/channels/route_handler_registry.hpp"
#include "runtime/spots/spot_runtime.hpp"
#include "runtime/stateful/public_host_runtime.hpp"

#include <zlink/Contracts/Sockets/stream_socket.hpp>

#include <atomic>
#include <map>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace zlink::framework
{
class zlink_builder_t;
}

namespace zlink::framework::detail
{
namespace host = zlink::framework::runtime::host;

struct mesh_channel_registration_t
{
    int weight = 100;
    std::string handler_group;
};

struct mesh_node_builder_state_t
{
    explicit mesh_node_builder_state_t (std::string name);

    std::mutex mutex;
    std::string mesh_name;
    std::string listen_endpoint;
    std::optional<zlink::routing_id_t> routing_id;
    int placement_weight = 100;
    std::int32_t actor_limit = 10000;
    std::int32_t spot_limit = 128;
    std::int32_t activation_concurrency_limit = 128;
    std::map<std::string, mesh_channel_registration_t> channels;
    std::function<void (const std::string &)> channel_name_observer;
    route_handler_registry_t handlers;
    std::vector<mesh_peer_connection_t> peer_connections;
    std::size_t allocation_slot_count = 0;
    std::string allocation_routing_id_prefix;
    std::string allocation_group;
    mesh_node_socket_config_t socket;
    entry_spot_options_t entry_spot;
    std::chrono::milliseconds default_request_timeout{std::chrono::seconds (30)};
    std::size_t max_pending = 1024;
    std::atomic<std::uint64_t> next_join_completion_operation{1};
    std::shared_ptr<spot_node_builder_state_t> spot_state;
    spot_node_builder_t spot_builder;
};

class mesh_node_runtime_t
{
  public:
    struct operation_completion_t
    {
        host::receive_record_t record;
        std::vector<zlink::message_t> parts;
    };
    explicit mesh_node_runtime_t (std::shared_ptr<mesh_node_builder_state_t> state);
    ~mesh_node_runtime_t ();

    mesh_node_runtime_t (const mesh_node_runtime_t &) = delete;
    mesh_node_runtime_t &operator= (const mesh_node_runtime_t &) = delete;

    void start ();
    void stop () noexcept;
    void bind_serializers (serializer_registry_t &serializers) noexcept;
    void bind_descriptor_publisher (
      std::function<void (const std::map<std::string, int> &,
                          int,
                          std::uint64_t)> publisher);
    void configure_user_spot_operations (
      std::shared_ptr<location_store_t> store,
      host::user_spot_materializer_t materializer);
    void connect_peer (const zlink::routing_id_t &expected_routing_id,
                       const std::string &endpoint);
    void disconnect_peer (const std::string &endpoint) noexcept;

    zlink::submit_result_t send_to_node (const zlink::routing_id_t &target,
                                         const std::vector<zlink::message_t> &parts,
                                         std::vector<std::uint8_t> metadata = {});
    zlink::submit_result_t
    send_to_node (const zlink::routing_id_t &target,
                  const std::vector<zlink::message_t> &parts,
                  const std::map<std::string, std::string> &metadata);
    zlink::submit_result_t request_to_node (
      const zlink::routing_id_t &target,
      const std::vector<zlink::message_t> &parts,
      host::operation_id_t &operation_id,
      std::chrono::milliseconds timeout,
      std::vector<std::uint8_t> metadata = {});
    zlink::submit_result_t request_to_node (
      const zlink::routing_id_t &target,
      const std::vector<zlink::message_t> &parts,
      host::operation_id_t &operation_id,
      std::chrono::milliseconds timeout,
      const std::map<std::string, std::string> &metadata);
    zlink::submit_result_t send_to_channel (const std::string &channel_name,
                                            const std::vector<zlink::message_t> &parts,
                                            std::vector<std::uint8_t> metadata = {});
    zlink::submit_result_t
    send_to_channel (const std::string &channel_name,
                     const std::vector<zlink::message_t> &parts,
                     const std::map<std::string, std::string> &metadata);
    zlink::submit_result_t request_to_channel (
      const std::string &channel_name,
      const std::vector<zlink::message_t> &parts,
      host::operation_id_t &operation_id,
      std::chrono::milliseconds timeout,
      std::vector<std::uint8_t> metadata = {});
    zlink::submit_result_t request_to_channel (
      const std::string &channel_name,
      const std::vector<zlink::message_t> &parts,
      host::operation_id_t &operation_id,
      std::chrono::milliseconds timeout,
      const std::map<std::string, std::string> &metadata);
    host::spot_handle_t get_or_create_spot (std::string spot_id);
    zlink::submit_result_t send_to_spot (
      const std::string &source_spot_id,
      const zlink::routing_id_t &target_node_rid,
      const std::string &target_spot_id,
      std::uint64_t target_spot_generation,
      const std::vector<zlink::message_t> &parts,
      std::vector<std::uint8_t> metadata = {});
    zlink::submit_result_t request_to_spot (
      const std::string &source_spot_id,
      const zlink::routing_id_t &target_node_rid,
      const std::string &target_spot_id,
      std::uint64_t target_spot_generation,
      const std::vector<zlink::message_t> &parts,
      host::operation_id_t &operation_id,
      std::chrono::milliseconds timeout,
      std::vector<std::uint8_t> metadata = {});
    host::actor_handle_t create_actor (
      std::string actor_type,
      std::string actor_id,
      const std::vector<zlink::message_t> &creation_parts = {},
      std::chrono::milliseconds timeout = {});
    zlink::submit_result_t send_to_actor (
      const actor_ref_t &target,
      const std::vector<zlink::message_t> &parts,
      std::vector<std::uint8_t> metadata = {});
    zlink::submit_result_t request_to_actor (
      const actor_ref_t &target,
      const std::vector<zlink::message_t> &parts,
      host::operation_id_t &operation_id,
      std::chrono::milliseconds timeout,
      std::vector<std::uint8_t> metadata = {});
    zlink::submit_result_t send_actor_bound_session (
      const actor_ref_t &actor,
      std::uint64_t expected_binding_generation,
      const std::vector<zlink::message_t> &parts);
    zlink::context_t &native_context ();
    host::public_host_runtime_t &native_node ();
    bool prepare_actor_transfer (
      const host::actor_transfer_prepare_t &prepare,
      std::chrono::milliseconds timeout,
      host::actor_transfer_token_t &token,
      host::actor_transfer_prepare_result_t &result);
    result_t<actor_ref_t> create_application_actor (
      std::string actor_type,
      std::string actor_id,
      const std::optional<zlink::message_t> &creation_payload,
      std::chrono::milliseconds timeout);
    result_t<actor_join_reply_t> join_application_actor_to_entry_spot (
      const actor_ref_t &actor,
      const node_rid_t &target_node,
      const zlink::message_t &request,
      std::chrono::milliseconds timeout);
    result_t<actor_join_reply_t> join_application_actor_to_spot (
      actor_ref_t actor,
      const node_rid_t &target_node,
      const spot_id_t &target_spot,
      std::uint64_t target_spot_generation,
      const zlink::message_t &request,
      std::chrono::milliseconds timeout,
      std::optional<zlink::routing_id_t> bound_session_node_rid = std::nullopt,
      std::optional<zlink::routing_id_t> bound_session_rid = std::nullopt);
    result_t<std::optional<zlink::message_t>> relay_application_actor (
      const actor_ref_t &actor,
      const stream_header_t &header,
      const zlink::message_t &payload,
      std::chrono::milliseconds timeout);
    result_t<void> bind_application_actor_session (
      const actor_ref_t &actor,
      const node_rid_t &session_node,
      std::chrono::milliseconds timeout);
    result_t<void> notify_application_actor_disconnected (
      const actor_ref_t &actor,
      const node_rid_t &target_node,
      std::chrono::milliseconds timeout);
    std::optional<actor_ref_t> forward_straggler_actor (const actor_ref_t &actor);
    result_t<operation_completion_t> wait_for_completion (
      const host::operation_id_t &operation,
      std::chrono::milliseconds timeout);
    std::size_t dispatch_ready (
      const std::function<void (const host::ready_record_t &,
                                const host::receive_record_t &,
                                std::vector<zlink::message_t>)> &dispatch);
    host::node_status_t status () const;
    std::string mesh_name () const;
    std::optional<zlink::routing_id_t> routing_id () const;
    std::string listen_endpoint () const;
    std::map<std::string, int> channel_weights () const;
    std::size_t max_pending () const noexcept;
    void set_channel_weight (const std::string &channel_name, int weight);
    int placement_weight () const;
    void set_placement_weight (int weight);
    std::int32_t actor_limit () const;
    std::int32_t spot_limit () const;
    std::int32_t activation_concurrency_limit () const;
    void application_work_enqueued () noexcept;
    void application_work_started () noexcept;
    void application_work_finished () noexcept;
    std::uint64_t pending_application_callbacks () const noexcept;
    std::uint64_t active_application_callbacks () const noexcept;

    static std::shared_ptr<mesh_node_runtime_t> from (zlink_builder_t &builder,
                                                      const std::string &mesh_name);
    static std::vector<std::shared_ptr<mesh_node_builder_state_t>>
    registrations (zlink_builder_t &builder);

  private:
    result_t<actor_join_reply_t> wait_for_join_completion (
      const host::operation_id_t &operation,
      const actor_ref_t &actor,
      std::chrono::milliseconds timeout);
    std::shared_ptr<mesh_node_builder_state_t> _state;
    serializer_registry_t *_serializers = nullptr;
    std::shared_ptr<location_store_t> _user_spot_store;
    host::user_spot_materializer_t _user_spot_materializer;
    std::function<void (const std::map<std::string, int> &,
                        int,
                        std::uint64_t)> _descriptor_publisher;
    std::shared_ptr<host::public_host_runtime_t> _node;
    std::map<std::string, host::spot_handle_t> _spots;
    std::map<std::string, host::actor_handle_t> _actors;
    std::mutex _peer_mutex;
    std::atomic_uint64_t _pending_application_callbacks{0};
    std::atomic_uint64_t _active_application_callbacks{0};
    std::map<std::string, std::uint64_t> _peer_connection_intents;
    std::mutex _completion_mutex;
    std::condition_variable _completion_ready;
    std::map<std::pair<std::uint64_t, std::uint64_t>, operation_completion_t>
      _completed_operations;
};

} // namespace zlink::framework::detail
