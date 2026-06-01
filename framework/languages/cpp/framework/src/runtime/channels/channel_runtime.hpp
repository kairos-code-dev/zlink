/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/framework/contracts/channels/channel.hpp>
#include <zlink/framework/contracts/codecs/serializer.hpp>
#include <zlink/framework/contracts/configuration/services.hpp>
#include <zlink/framework/contracts/handlers/handler_registry.hpp>

#include "runtime/registry/registry_runtime.hpp"
#include "runtime/streams/stream_runtime.hpp"

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>

namespace zlink::framework::detail
{

class spot_node_builder_state_t;
class stream_runtime_state_t;

class capability_builder_state_t
{
public:
  channel_capability_snapshot_t snapshot;
};

class channel_builder_state_t
{
public:
  explicit channel_builder_state_t (std::string name)
    : snapshot { std::move (name) }
  {
  }

  channel_snapshot_t snapshot;
};

class channel_runtime_state_t
{
public:
  std::map<std::string, channel_snapshot_t> channels;
  std::size_t max_pending = 1024;
  std::size_t pending = 0;
  std::map<std::uint64_t, std::string> pending_request_channels;
  std::map<std::uint64_t, channel_reliability_event_t> pending_operations;
  std::uint64_t next_request_seq = 1;
  bool shutdown = false;
  bool closed = false;
  retry_hook_t retry_hook;
  dead_letter_hook_t dead_letter_hook;
};

class zlink_builder_state_t
{
public:
  std::string node_name;
  std::shared_ptr<channel_runtime_state_t> runtime =
    std::make_shared<channel_runtime_state_t> ();
  std::map<std::string, std::shared_ptr<spot_node_builder_state_t>> spot_nodes;
  std::shared_ptr<registry_runtime_state_t> registry_runtime =
    std::make_shared<registry_runtime_state_t> ();
  std::shared_ptr<stream_runtime_state_t> stream_runtime =
    std::make_shared<stream_runtime_state_t> ();
};

class channel_runtime_t
{
public:
  explicit channel_runtime_t (std::shared_ptr<channel_runtime_state_t> state);

  result_t<zlink::message_t> dispatch_request (
    std::string channel_name,
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

  result_t<std::uint64_t> reserve_outbound_request (
    std::string channel_name);
  result_t<std::uint64_t> queue_pending_send (
    std::string channel_name,
    std::string idempotency_key = {});
  result_t<void> complete_outbound_reply (std::uint64_t request_seq);
  result_t<void> mark_send_ready (std::uint64_t operation_id);
  result_t<void> expire_pending (std::uint64_t operation_id);
  result_t<void> retry_pending (std::uint64_t operation_id);
  void close () noexcept;
  void shutdown () noexcept;
  std::size_t pending_count () const noexcept;
  std::size_t pending_limit () const noexcept;
  void drain () noexcept;

  static channel_runtime_t from (const message_bus_t &bus);

private:
  std::shared_ptr<channel_runtime_state_t> _state;
};

} // namespace zlink::framework::detail
