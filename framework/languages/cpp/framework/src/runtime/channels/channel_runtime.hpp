/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/framework/contracts/channels/channel.hpp>
#include <zlink/framework/contracts/codecs/serializer.hpp>
#include <zlink/framework/contracts/configuration/services.hpp>
#include <zlink/framework/contracts/handlers/handler_registry.hpp>

#include "runtime/channels/channel_pending_requests.hpp"
#include "runtime/channels/channel_runtime_bundle.hpp"
#include "runtime/channels/route_channel_registration.hpp"
#include "runtime/channels/route_channel_runtime.hpp"
#include "runtime/messaging/envelope_codec.hpp"
#include "runtime/registry/registry_runtime.hpp"
#include "runtime/streams/stream_runtime.hpp"

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <typeindex>
#include <vector>

namespace zlink::framework::detail
{

class spot_node_builder_state_t;
class channel_runtime_state_t;
class stream_runtime_state_t;
class channel_native_publisher_t;

result_t<void> validate_channel_native_reply (
  const runtime::messaging::message_parts_t &parts);

class capability_builder_state_t
{
  public:
    channel_capability_snapshot_t *target = nullptr;
    channel_capability_snapshot_t snapshot;
};

class channel_builder_state_t
{
  public:
    explicit channel_builder_state_t (std::string name) : snapshot{std::move (name)} {}

    channel_snapshot_t *target = nullptr;
    channel_snapshot_t snapshot;
};

class route_channel_builder_state_t
{
  public:
    explicit route_channel_builder_state_t (std::string router_channel_id) :
        registration (std::move (router_channel_id))
    {
    }

    route_channel_registration_t registration;
};

class route_client_state_t
{
  public:
    route_client_state_t (std::shared_ptr<channel_runtime_state_t> runtime,
                          serializer_registry_t &serializers) :
        runtime (std::move (runtime)), serializers (&serializers)
    {
    }

    std::shared_ptr<channel_runtime_state_t> runtime;
    serializer_registry_t *serializers;
};

class channel_runtime_state_t
{
  public:
    struct outbound_call_record_t
    {
        std::string kind;
        std::string channel_name;
        std::string topic;
        std::string packet_name;
        std::chrono::milliseconds timeout{0};
        std::map<std::string, std::string> metadata;
    };

    std::map<std::string, channel_snapshot_t> channels;
    mutable std::mutex mutex;
    std::size_t max_pending = 1024;
    std::chrono::milliseconds default_request_timeout{std::chrono::seconds (30)};
    std::size_t pending = 0;
    channel_pending_requests_t pending_requests;
    std::map<std::string, std::shared_ptr<channel_runtime_bundle_t>> server_bundles;
    std::map<std::string, std::shared_ptr<channel_runtime_bundle_t>> client_bundles;
    std::map<std::string, std::shared_ptr<channel_runtime_bundle_t>> publisher_bundles;
    std::map<std::string, std::shared_ptr<channel_runtime_bundle_t>> subscriber_bundles;
    std::map<std::string, std::shared_ptr<channel_native_publisher_t>> native_publishers;
    std::map<std::string, std::shared_ptr<route_channel_runtime_t>> route_channels;
    std::map<std::string, route_handler_registry_t> route_handlers;
    std::map<std::string, zlink::peer_weight_t> server_peer_weight_overrides;
    std::map<std::uint64_t, channel_reliability_event_t> pending_operations;
    std::vector<outbound_call_record_t> outbound_calls;
    dispatch_options_t dispatch;
    discovery_snapshot_t discovery;
    serializer_registry_t *serializers = nullptr;
    bool shutdown = false;
    bool closed = false;
    retry_hook_t retry_hook;
    dead_letter_hook_t dead_letter_hook;
};

class zlink_builder_state_t
{
  public:
    std::string node_name;
    std::shared_ptr<channel_runtime_state_t> runtime = std::make_shared<channel_runtime_state_t> ();
    std::map<std::string, std::shared_ptr<spot_node_builder_state_t>> spot_nodes;
    std::map<std::string, std::shared_ptr<route_channel_builder_state_t>> route_channels;
    std::shared_ptr<registry_runtime_state_t> registry_runtime =
      std::make_shared<registry_runtime_state_t> ();
    std::shared_ptr<stream_runtime_state_t> stream_runtime =
      std::make_shared<stream_runtime_state_t> ();
};

class channel_runtime_t
{
  public:
    explicit channel_runtime_t (std::shared_ptr<channel_runtime_state_t> state);

    result_t<zlink::message_t> dispatch_request (std::string channel_name,
                                                 std::string topic,
                                                 std::string packet_name,
                                                 service_provider_t &services,
                                                 serializer_registry_t &serializers,
                                                 const handler_registry_t &handlers,
                                                 const zlink::message_t &message) const;

    result_t<void> dispatch_send (std::string channel_name,
                                  std::string topic,
                                  std::string packet_name,
                                  service_provider_t &services,
                                  serializer_registry_t &serializers,
                                  const handler_registry_t &handlers,
                                  const zlink::message_t &message) const;

    result_t<std::uint64_t> reserve_outbound_request (std::string channel_name);
    result_t<std::uint64_t> queue_pending_send (std::string channel_name,
                                                std::string idempotency_key = {});
    result_t<void> complete_outbound_reply (std::uint64_t request_seq);
    result_t<void> cancel_outbound_request (std::uint64_t request_seq);
    result_t<void> mark_send_ready (std::uint64_t operation_id);
    result_t<void> expire_pending (std::uint64_t operation_id);
    result_t<void> retry_pending (std::uint64_t operation_id);
    void close () noexcept;
    void shutdown () noexcept;
    std::size_t pending_count () const noexcept;
    std::size_t pending_limit () const noexcept;
    std::vector<channel_runtime_state_t::outbound_call_record_t> outbound_calls () const;
    void bind_serializers (serializer_registry_t &serializers) noexcept;
    void bind_discovery (discovery_snapshot_t discovery) noexcept;
    dispatch_options_t dispatch_options () const;
    const dispatch_options_t &dispatch_options_ref () const noexcept { return _state->dispatch; }
    void drain () noexcept;
    void set_server_peer_weight (const std::string &channel_name, zlink::peer_weight_t value);
    std::optional<zlink::peer_weight_t>
    server_peer_weight_override (const std::string &channel_name) const;

    static channel_runtime_t from (const message_bus_t &bus);

  private:
    std::shared_ptr<channel_runtime_state_t> _state;
};

} // namespace zlink::framework::detail
