/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/Contracts/Core/routing_id.hpp>
#include <zlink/framework/contracts/channels/call.hpp>
#include <zlink/framework/contracts/codecs/serializer.hpp>
#include <zlink/framework/contracts/configuration/services.hpp>
#include <zlink/framework/contracts/detail/message_name.hpp>
#include <zlink/framework/contracts/dispatch/task.hpp>
#include <zlink/framework/contracts/errors/error.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <typeindex>
#include <utility>
#include <vector>

namespace zlink::framework
{

namespace detail
{
class capability_builder_state_t;
class channel_builder_state_t;
class channel_runtime_state_t;
class channel_runtime_t;
class channel_runtime_manager_t;
class route_client_state_t;
class route_channel_builder_state_t;
} // namespace detail

enum class channel_capability_t
{
  server,
  client,
  publisher,
  subscriber
};

struct channel_capability_snapshot_t
{
  bool enabled = false;
  bool discovery = false;
  std::vector<std::string> bind_endpoints;
  std::vector<std::string> connect_endpoints;
};

struct channel_snapshot_t
{
  std::string name;
  channel_capability_snapshot_t server;
  channel_capability_snapshot_t client;
  channel_capability_snapshot_t publisher;
  channel_capability_snapshot_t subscriber;
};

struct channel_reliability_event_t
{
  std::string channel_name;
  std::string idempotency_key;
  framework_error_kind_t error_kind;
  std::string message;
};

struct route_handler_context_t
{
  std::string router_channel_id;
  zlink::routing_id_t source_node_rid;
  std::string packet_name;
  std::string content_type;
};

enum class route_handler_kind_t
{
  send,
  request
};

struct route_handler_registration_t
{
  route_handler_kind_t kind;
  std::string packet_name;
  std::type_index owner_type;
  std::type_index message_type;
  std::type_index reply_type;
  std::function<result_t<zlink::message_t> (
    service_provider_t &,
    serializer_registry_t &,
    const zlink::message_t &,
    const route_handler_context_t &)> invoker;
};

class capability_builder_t
{
public:
  capability_builder_t ();
  ~capability_builder_t ();

  capability_builder_t (capability_builder_t &&) noexcept;
  capability_builder_t &operator= (capability_builder_t &&) noexcept;
  capability_builder_t (const capability_builder_t &) = default;
  capability_builder_t &operator= (const capability_builder_t &) = default;

  capability_builder_t &bind (std::string endpoint);
  capability_builder_t &connect (std::string endpoint);
  capability_builder_t &use_discovery ();

  channel_capability_snapshot_t snapshot () const;

private:
  friend class channel_builder_t;
  explicit capability_builder_t (
    std::shared_ptr<detail::capability_builder_state_t> state);

  std::shared_ptr<detail::capability_builder_state_t> _state;
};

class channel_builder_t
{
public:
  channel_builder_t ();
  ~channel_builder_t ();

  channel_builder_t (channel_builder_t &&) noexcept;
  channel_builder_t &operator= (channel_builder_t &&) noexcept;
  channel_builder_t (const channel_builder_t &) = default;
  channel_builder_t &operator= (const channel_builder_t &) = default;

  channel_builder_t &enable_server (
    std::function<void (capability_builder_t &)> configure = {});
  channel_builder_t &enable_client (
    std::function<void (capability_builder_t &)> configure = {});
  channel_builder_t &enable_publisher (
    std::function<void (capability_builder_t &)> configure = {});
  channel_builder_t &enable_subscriber (
    std::function<void (capability_builder_t &)> configure = {});

  channel_snapshot_t snapshot () const;

private:
  friend class zlink_builder_t;
  explicit channel_builder_t (
    std::shared_ptr<detail::channel_builder_state_t> state);
  void enable_capability (
    channel_capability_snapshot_t &target,
    const std::function<void (capability_builder_t &)> &configure);

  std::shared_ptr<detail::channel_builder_state_t> _state;
};

class route_channel_builder_t
{
public:
  route_channel_builder_t ();
  ~route_channel_builder_t ();

  route_channel_builder_t (route_channel_builder_t &&) noexcept;
  route_channel_builder_t &operator= (route_channel_builder_t &&) noexcept;
  route_channel_builder_t (const route_channel_builder_t &) = default;
  route_channel_builder_t &operator= (const route_channel_builder_t &) =
    default;

  route_channel_builder_t &bind (std::string endpoint);
  route_channel_builder_t &connect (std::string endpoint);
  route_channel_builder_t &add_handler_group (std::string group_name);

  template<typename TOwner, typename TMessage>
  route_channel_builder_t &add_send_handler (
    std::string packet_name,
    void (TOwner::*method) (const TMessage &, const route_handler_context_t &))
  {
    const auto packet = packet_name.empty ()
                          ? detail::message_name<TMessage> ()
                          : std::move (packet_name);
    return add_handler (route_handler_registration_t {
      route_handler_kind_t::send,
      packet,
      std::type_index (typeid (TOwner)),
      std::type_index (typeid (TMessage)),
      std::type_index (typeid (void)),
      [method](service_provider_t &services,
               serializer_registry_t &serializers,
               const zlink::message_t &message,
               const route_handler_context_t &context)
        -> result_t<zlink::message_t> {
        try {
          auto &owner = services.get_required<TOwner> ();
          auto payload = serializers.get<TMessage> ().deserialize (message);
          (owner.*method) (payload, context);
          return result_t<zlink::message_t>::success (zlink::message_t {});
        } catch (const framework_exception_t &error) {
          return result_t<zlink::message_t>::failure (
            error.kind (), error.what (), error.is_retriable ());
        } catch (...) {
          return result_t<zlink::message_t>::failure (
            framework_error_kind_t::request_failed,
            "routed send handler threw an exception");
        }
      } });
  }

  template<typename TOwner, typename TRequest, typename TReply>
  route_channel_builder_t &add_request_handler (
    std::string packet_name,
    TReply (TOwner::*method) (const TRequest &, const route_handler_context_t &))
  {
    const auto packet = packet_name.empty ()
                          ? detail::message_name<TRequest> ()
                          : std::move (packet_name);
    return add_handler (route_handler_registration_t {
      route_handler_kind_t::request,
      packet,
      std::type_index (typeid (TOwner)),
      std::type_index (typeid (TRequest)),
      std::type_index (typeid (TReply)),
      [method](service_provider_t &services,
               serializer_registry_t &serializers,
               const zlink::message_t &message,
               const route_handler_context_t &context)
        -> result_t<zlink::message_t> {
        try {
          auto &owner = services.get_required<TOwner> ();
          auto request = serializers.get<TRequest> ().deserialize (message);
          auto reply = (owner.*method) (request, context);
          return result_t<zlink::message_t>::success (
            serializers.get<TReply> ().serialize (reply));
        } catch (const framework_exception_t &error) {
          return result_t<zlink::message_t>::failure (
            error.kind (), error.what (), error.is_retriable ());
        } catch (...) {
          return result_t<zlink::message_t>::failure (
            framework_error_kind_t::request_failed,
            "routed request handler threw an exception");
        }
      } });
  }

private:
  friend class zlink_builder_t;
  explicit route_channel_builder_t (
    std::shared_ptr<detail::route_channel_builder_state_t> state);

  route_channel_builder_t &add_handler (
    route_handler_registration_t registration);

  std::shared_ptr<detail::route_channel_builder_state_t> _state;
};

class message_bus_t
{
public:
  message_bus_t ();
  ~message_bus_t ();

  message_bus_t (message_bus_t &&) noexcept;
  message_bus_t &operator= (message_bus_t &&) noexcept;
  message_bus_t (const message_bus_t &) = default;
  message_bus_t &operator= (const message_bus_t &) = default;

  template<typename TRequest, typename TReply>
  request_call_t<TReply> request (std::string channel_name,
                                  TRequest request)
  {
    (void) request;
    return request_call_t<TReply> (
      submit_request (std::move (channel_name)).template as<TReply> ());
  }

  template<typename TMessage>
  send_call_t send (std::string channel_name, TMessage message)
  {
    (void) message;
    return send_call_t (submit_send (std::move (channel_name)));
  }

  template<typename TEvent>
  send_call_t publish (std::string channel_name,
                       std::string topic,
                       TEvent event)
  {
    (void) event;
    return send_call_t (
      submit_publish (std::move (channel_name), std::move (topic)));
  }

  std::size_t pending_count () const noexcept;
  std::size_t pending_limit () const noexcept;

private:
  friend class zlink_builder_t;
  friend class request_client_t;
  friend class publisher_t;
  friend class detail::channel_runtime_t;
  friend class detail::channel_runtime_manager_t;

  class erased_request_result_t
  {
  public:
    explicit erased_request_result_t (framework_exception_t error);

    template<typename TReply>
    result_t<TReply> as () const
    {
      return result_t<TReply>::failure (
        _error.kind (), _error.what (), _error.is_retriable ());
    }

  private:
    framework_exception_t _error;
  };

  explicit message_bus_t (
    std::shared_ptr<detail::channel_runtime_state_t> state);

  erased_request_result_t submit_request (std::string channel_name);
  result_t<void> submit_send (std::string channel_name);
  result_t<void> submit_publish (std::string channel_name, std::string topic);

  std::shared_ptr<detail::channel_runtime_state_t> _state;
};

class route_send_call_t
{
public:
  using submit_fn_t = std::function<result_t<void> (const std::string &)>;

  route_send_call_t (std::string packet_name, submit_fn_t submit);

  route_send_call_t &packet_name (std::string packet_name);
  task_t<void> submit ();
  pending_operation_t submit (std::function<void (result_t<void>)> callback);

private:
  std::string _packet_name;
  submit_fn_t _submit;
};

class route_request_call_t
{
public:
  using submit_fn_t =
    std::function<result_t<std::uint64_t> (const std::string &,
                                           std::chrono::milliseconds)>;

  route_request_call_t (std::string packet_name, submit_fn_t submit);

  route_request_call_t &packet_name (std::string packet_name);
  route_request_call_t &timeout (std::chrono::milliseconds timeout);
  task_t<std::uint64_t> submit ();
  pending_operation_t submit (
    std::function<void (result_t<std::uint64_t>)> callback);

private:
  std::string _packet_name;
  std::chrono::milliseconds _timeout { 0 };
  submit_fn_t _submit;
};

template<typename TReply>
class typed_route_request_call_t
{
public:
  using submit_fn_t =
    std::function<result_t<TReply> (const std::string &,
                                    std::chrono::milliseconds)>;

  typed_route_request_call_t (std::string packet_name, submit_fn_t submit)
    : _packet_name (std::move (packet_name)), _submit (std::move (submit))
  {
  }

  typed_route_request_call_t &packet_name (std::string packet_name)
  {
    _packet_name = std::move (packet_name);
    return *this;
  }

  typed_route_request_call_t &timeout (std::chrono::milliseconds timeout)
  {
    _timeout = timeout;
    return *this;
  }

  task_t<TReply> submit ()
  {
    if (!_submit) {
      return task_t<TReply> (result_t<TReply>::failure (
        framework_error_kind_t::request_protocol_error,
        "typed route request call is not bound to a route client"));
    }
    return task_t<TReply> (_submit (_packet_name, _timeout));
  }

  pending_operation_t submit (std::function<void (result_t<TReply>)> callback)
  {
    auto result = submit ().result ();
    callback (result);
    return pending_operation_t::make_completed ();
  }

private:
  std::string _packet_name;
  std::chrono::milliseconds _timeout { 0 };
  submit_fn_t _submit;
};

class route_client_t
{
public:
  route_client_t ();
  ~route_client_t ();

  route_client_t (route_client_t &&) noexcept;
  route_client_t &operator= (route_client_t &&) noexcept;
  route_client_t (const route_client_t &) = default;
  route_client_t &operator= (const route_client_t &) = default;

  template<typename TMessage>
  route_send_call_t send (std::string router_channel_id,
                          zlink::routing_id_t target_node_rid,
                          TMessage message)
  {
    auto state = _state;
    return route_send_call_t (
      detail::message_name<TMessage> (),
      [state,
       router_channel_id = std::move (router_channel_id),
       target_node_rid = std::move (target_node_rid),
       message = std::move (message)](
        const std::string &packet_name) -> result_t<void> {
        return submit_send_erased (state,
                                   router_channel_id,
                                   target_node_rid,
                                   packet_name,
                                   std::type_index (typeid (TMessage)),
                                   &message);
      });
  }

  template<typename TRequest>
  route_request_call_t request (std::string router_channel_id,
                                zlink::routing_id_t target_node_rid,
                                TRequest request)
  {
    auto state = _state;
    return route_request_call_t (
      detail::message_name<TRequest> (),
      [state,
       router_channel_id = std::move (router_channel_id),
       target_node_rid = std::move (target_node_rid),
       request = std::move (request)](
        const std::string &packet_name,
        std::chrono::milliseconds timeout) -> result_t<std::uint64_t> {
        return submit_request_erased (state,
                                      router_channel_id,
                                      target_node_rid,
                                      packet_name,
                                      std::type_index (typeid (TRequest)),
                                      &request,
                                      timeout);
      });
  }

  template<typename TRequest, typename TReply>
  typed_route_request_call_t<TReply> request (
    std::string router_channel_id,
    zlink::routing_id_t target_node_rid,
    TRequest request)
  {
    auto state = _state;
    auto *serializers = _serializers;
    return typed_route_request_call_t<TReply> (
      detail::message_name<TRequest> (),
      [state,
       serializers,
       router_channel_id = std::move (router_channel_id),
       target_node_rid = std::move (target_node_rid),
       request = std::move (request)](
        const std::string &packet_name,
        std::chrono::milliseconds timeout) -> result_t<TReply> {
        auto reply = submit_request_reply_erased (
          state,
          router_channel_id,
          target_node_rid,
          packet_name,
          std::type_index (typeid (TRequest)),
          &request,
          timeout);
        if (!reply) {
          return result_t<TReply>::failure (
            reply.error_kind (),
            reply.error () ? reply.error ()->what ()
                           : "route request failed");
        }
        if (serializers == nullptr) {
          return result_t<TReply>::failure (
            framework_error_kind_t::request_protocol_error,
            "route client has no serializer registry");
        }
        try {
          return result_t<TReply>::success (
            serializers->get<TReply> ().deserialize (reply.value ()));
        } catch (const framework_exception_t &error) {
          return result_t<TReply>::failure (
            error.kind (), error.what (), error.is_retriable ());
        }
      });
  }

private:
  friend class zlink_builder_t;
  explicit route_client_t (std::shared_ptr<detail::route_client_state_t> state,
                           serializer_registry_t &serializers);

  static result_t<void> submit_send_erased (
    const std::shared_ptr<detail::route_client_state_t> &state,
    const std::string &router_channel_id,
    const zlink::routing_id_t &target_node_rid,
    const std::string &packet_name,
    std::type_index message_type,
    const void *message);

  static result_t<std::uint64_t> submit_request_erased (
    const std::shared_ptr<detail::route_client_state_t> &state,
    const std::string &router_channel_id,
    const zlink::routing_id_t &target_node_rid,
    const std::string &packet_name,
    std::type_index request_type,
    const void *request,
    std::chrono::milliseconds timeout);

  static result_t<zlink::message_t> submit_request_reply_erased (
    const std::shared_ptr<detail::route_client_state_t> &state,
    const std::string &router_channel_id,
    const zlink::routing_id_t &target_node_rid,
    const std::string &packet_name,
    std::type_index request_type,
    const void *request,
    std::chrono::milliseconds timeout);

  std::shared_ptr<detail::route_client_state_t> _state;
  serializer_registry_t *_serializers = nullptr;
};

class request_client_t
{
public:
  request_client_t (message_bus_t bus, std::string channel_name);

  template<typename TRequest, typename TReply>
  request_call_t<TReply> request (TRequest request)
  {
    return _bus.request<TRequest, TReply> (_channel_name, std::move (request));
  }

private:
  message_bus_t _bus;
  std::string _channel_name;
};

class publisher_t
{
public:
  explicit publisher_t (message_bus_t bus);

  template<typename TEvent>
  send_call_t publish (std::string channel_name,
                       std::string topic,
                       TEvent event)
  {
    return _bus.publish (
      std::move (channel_name), std::move (topic), std::move (event));
  }

private:
  message_bus_t _bus;
};

using retry_hook_t = std::function<void (const channel_reliability_event_t &)>;
using dead_letter_hook_t =
  std::function<void (const channel_reliability_event_t &)>;

} // namespace zlink::framework
