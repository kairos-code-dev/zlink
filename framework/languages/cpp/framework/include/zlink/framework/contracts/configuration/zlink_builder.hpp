/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/framework/contracts/channels/channel.hpp>
#include <zlink/framework/contracts/registry/registry.hpp>
#include <zlink/framework/contracts/spots/spot.hpp>
#include <zlink/framework/contracts/streams/stream.hpp>

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace zlink::framework
{

namespace detail
{
class channel_runtime_manager_t;
class zlink_builder_state_t;
} // namespace detail

class zlink_builder_t
{
public:
  zlink_builder_t ();
  ~zlink_builder_t ();

  zlink_builder_t (zlink_builder_t &&) noexcept;
  zlink_builder_t &operator= (zlink_builder_t &&) noexcept;
  zlink_builder_t (const zlink_builder_t &) = delete;
  zlink_builder_t &operator= (const zlink_builder_t &) = delete;

  zlink_builder_t &node (std::string node_name);
  zlink_builder_t &max_pending (std::size_t count);
  zlink_builder_t &on_retry (retry_hook_t hook);
  zlink_builder_t &on_dead_letter (dead_letter_hook_t hook);
  zlink_builder_t &registry (
    std::function<void (registry_builder_t &)> configure);
  zlink_builder_t &discovery (
    std::function<void (discovery_builder_t &)> configure);
  zlink_builder_t &route_channel (std::string route_channel_name);
  zlink_builder_t &route_channel (
    std::string route_channel_name,
    std::function<void (route_channel_builder_t &)> configure);
  zlink_builder_t &channel (
    std::string channel_name,
    std::function<void (channel_builder_t &)> configure);
  zlink_builder_t &spot_node (
    std::string spot_node_name,
    std::function<void (spot_node_builder_t &)> configure);
  zlink_builder_t &stream (
    std::string stream_name,
    std::function<void (stream_builder_t &)> configure);

  std::vector<channel_snapshot_t> channels () const;
  registry_options_snapshot_t registry_options () const;
  discovery_snapshot_t discovery_options () const;
  std::vector<std::string> route_channels () const;
  std::vector<spot_node_snapshot_t> spot_nodes () const;
  std::vector<stream_snapshot_t> streams () const;
  result_t<void> validate_registry () const;
  registry_query_t registry_query () const;
  message_bus_t message_bus () const;
  request_client_t request_client (std::string channel_name) const;
  publisher_t publisher () const;
  route_client_t route_client (serializer_registry_t &serializers) const;

private:
  friend class detail::channel_runtime_manager_t;
  friend class detail::registry_runtime_t;
  friend class detail::stream_runtime_t;
  std::shared_ptr<detail::zlink_builder_state_t> _state;
};

} // namespace zlink::framework
