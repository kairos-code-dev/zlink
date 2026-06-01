/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/framework/contracts/channels/call.hpp>
#include <zlink/framework/contracts/channels/channel.hpp>
#include <zlink/framework/contracts/errors/result.hpp>
#include <zlink/framework/contracts/timers/timer.hpp>

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <typeindex>
#include <vector>

namespace zlink::framework
{

namespace detail
{
class spot_context_state_t;
class spot_node_builder_state_t;
class spot_node_runtime_state_t;
class spot_node_runtime_t;
} // namespace detail

class node_rid_t
{
public:
  node_rid_t () = default;
  explicit node_rid_t (std::string value);

  static node_rid_t from_string (std::string value);
  std::string_view value () const noexcept;
  bool empty () const noexcept;

private:
  std::string _value;
};

class spot_rid_t
{
public:
  spot_rid_t () = default;
  explicit spot_rid_t (std::string value);

  static spot_rid_t from_string (std::string value);
  std::string_view value () const noexcept;
  bool empty () const noexcept;

private:
  std::string _value;
};

struct spot_route_t
{
  node_rid_t node_rid;
  spot_rid_t spot_rid;
  std::string spot_name;
};

struct spot_packet_descriptor_t
{
  std::string packet_name;
  std::type_index payload_type;
};

struct spot_node_snapshot_t
{
  std::string name;
  std::string bind_endpoint;
  bool actor_gateway_enabled = false;
  std::optional<std::string> discovery_channel_name;
  std::vector<std::string> attached_channel_clients;
  std::vector<std::string> attached_publishers;
  std::vector<std::string> spot_names;
  std::optional<std::string> entry_spot_name;
  bool registry_spot_remote_addresses_enabled = false;
  std::optional<std::string> registry_spot_route_channel;
  std::vector<std::string> actor_types;
};

class spot_context_t
{
public:
  spot_context_t ();
  ~spot_context_t ();

  spot_context_t (spot_context_t &&) noexcept;
  spot_context_t &operator= (spot_context_t &&) noexcept;
  spot_context_t (const spot_context_t &) = default;
  spot_context_t &operator= (const spot_context_t &) = default;

  node_rid_t node_rid () const;
  spot_rid_t spot_rid () const;
  std::string spot_name () const;

  template<typename TEvent>
  send_call_t publish (std::string topic, TEvent event)
  {
    (void) event;
    return publish_erased (std::move (topic));
  }

  template<typename TReply, typename TRequest>
  request_call_t<TReply> request_to (node_rid_t node_rid,
                                     spot_rid_t spot_rid,
                                     TRequest request)
  {
    (void) request;
    return request_to_erased (std::move (node_rid), std::move (spot_rid))
      .template as<TReply> ();
  }

  template<typename TMessage>
  send_call_t send_to (node_rid_t node_rid,
                       spot_rid_t spot_rid,
                       TMessage message)
  {
    (void) message;
    return send_to_erased (std::move (node_rid), std::move (spot_rid));
  }

  template<typename TPayload>
  spot_context_t &register_packet (std::string packet_name)
  {
    return register_packet_erased (
      std::move (packet_name), std::type_index (typeid (TPayload)));
  }

  std::vector<spot_packet_descriptor_t> packet_registry () const;

  template<typename THandler>
  timer_t add_timer (std::string name,
                     std::chrono::milliseconds period,
                     timer_options_t options = {})
  {
    return add_timer_erased (
      std::move (name), period, std::move (options),
      std::type_index (typeid (THandler)));
  }

private:
  friend class spot_node_builder_t;
  friend class detail::spot_node_runtime_t;
  friend class detail::timer_runtime_t;

  class erased_request_call_t
  {
  public:
    explicit erased_request_call_t (framework_exception_t error);

    template<typename TReply>
    request_call_t<TReply> as () const
    {
      return request_call_t<TReply> (result_t<TReply>::failure (
        _error.kind (), _error.what (), _error.is_retriable ()));
    }

  private:
    framework_exception_t _error;
  };

  explicit spot_context_t (std::shared_ptr<detail::spot_context_state_t> state);

  send_call_t publish_erased (std::string topic);
  send_call_t send_to_erased (node_rid_t node_rid, spot_rid_t spot_rid);
  erased_request_call_t request_to_erased (node_rid_t node_rid,
                                           spot_rid_t spot_rid);
  spot_context_t &register_packet_erased (std::string packet_name,
                                          std::type_index payload_type);
  timer_t add_timer_erased (std::string name,
                            std::chrono::milliseconds period,
                            timer_options_t options,
                            std::type_index handler_type);

  std::shared_ptr<detail::spot_context_state_t> _state;
};

class spot_node_builder_t
{
public:
  spot_node_builder_t ();
  ~spot_node_builder_t ();

  spot_node_builder_t (spot_node_builder_t &&) noexcept;
  spot_node_builder_t &operator= (spot_node_builder_t &&) noexcept;
  spot_node_builder_t (const spot_node_builder_t &) = default;
  spot_node_builder_t &operator= (const spot_node_builder_t &) = default;

  spot_node_builder_t &bind (std::string endpoint);
  spot_node_builder_t &enable_actor_gateway ();
  spot_node_builder_t &use_discovery (std::string channel_name);
  spot_node_builder_t &use_registry_spot_remote_addresses ();
  spot_node_builder_t &use_registry_spot_remote_addresses (
    std::string route_channel_name);
  spot_node_builder_t &attach_channel_client (std::string channel_name);
  spot_node_builder_t &attach_publisher (std::string channel_name);

  template<typename TSpot>
  spot_node_builder_t &add_spot (std::string spot_name)
  {
    return add_spot_factory (
      std::move (spot_name), std::type_index (typeid (TSpot)), false);
  }

  template<typename TEntrySpot>
  spot_node_builder_t &add_entry_spot ()
  {
    return add_spot_factory (
      std::string ("entry"), std::type_index (typeid (TEntrySpot)), true);
  }

  template<typename TActorFactory>
  spot_node_builder_t &add_actor_factory (std::string actor_type)
  {
    return add_actor_factory_erased (
      std::move (actor_type), std::type_index (typeid (TActorFactory)));
  }

  spot_node_builder_t &add_spot_resolver (
    std::string name,
    std::function<std::optional<spot_route_t> (spot_rid_t)> resolver);

  spot_node_snapshot_t snapshot () const;
  spot_context_t create_spot (std::string spot_name);
  std::optional<std::string> spot_name_for (spot_rid_t spot_rid) const;
  std::optional<spot_route_t> resolve_spot (spot_rid_t spot_rid) const;

private:
  friend class zlink_builder_t;
  friend class detail::spot_node_runtime_t;
  explicit spot_node_builder_t (
    std::shared_ptr<detail::spot_node_builder_state_t> state);

  spot_node_builder_t &add_spot_factory (std::string spot_name,
                                         std::type_index spot_type,
                                         bool entry_spot);
  spot_node_builder_t &add_actor_factory_erased (std::string actor_type,
                                                 std::type_index factory_type);

  std::shared_ptr<detail::spot_node_builder_state_t> _state;
};

} // namespace zlink::framework
