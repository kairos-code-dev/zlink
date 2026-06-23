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
#include <map>
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
class channel_outbound_exchange_t;
class channel_runtime_manager_t;
class route_client_state_t;
class route_channel_builder_state_t;
} // namespace detail

class spot_context_t;

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
    std::optional<zlink::routing_id_t> routing_id;
    std::vector<std::string> bind_endpoints;
    std::vector<std::string> connect_endpoints;
};

struct channel_snapshot_t
{
    std::string name;
    std::optional<std::chrono::milliseconds> default_request_timeout;
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
    std::function<task_t<zlink::message_t> (service_provider_t &,
                                            serializer_registry_t &,
                                            const zlink::message_t &,
                                            const route_handler_context_t &)>
      invoker;
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
    capability_builder_t &set_routing_id (zlink::routing_id_t routing_id);

    channel_capability_snapshot_t snapshot () const;

  private:
    friend class channel_builder_t;
    explicit capability_builder_t (std::shared_ptr<detail::capability_builder_state_t> state);

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

    capability_builder_t enable_server ();
    capability_builder_t enable_client ();
    capability_builder_t enable_publisher ();
    capability_builder_t enable_subscriber ();
    channel_builder_t &default_request_timeout (std::chrono::milliseconds timeout);

    channel_snapshot_t snapshot () const;

  private:
    friend class zlink_builder_t;
    explicit channel_builder_t (std::shared_ptr<detail::channel_builder_state_t> state);
    capability_builder_t enable_capability (channel_capability_snapshot_t &target);

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
    route_channel_builder_t &operator= (const route_channel_builder_t &) = default;

    route_channel_builder_t &bind (std::string endpoint);
    route_channel_builder_t &set_routing_id (zlink::routing_id_t routing_id);
    route_channel_builder_t &connect (std::string endpoint);
    route_channel_builder_t &default_request_timeout (std::chrono::milliseconds timeout);
    route_channel_builder_t &add_handler_group (std::string group_name);
    route_channel_builder_t &enable_spot_route_egress (std::string target_spot_node_channel_name);

    template <typename TOwner, typename TMessage>
    route_channel_builder_t &
    add_send_handler (std::string packet_name,
                      void (TOwner::*method) (const TMessage &, const route_handler_context_t &))
    {
        const auto packet =
          packet_name.empty () ? detail::message_name<TMessage> () : std::move (packet_name);
        return add_handler (route_handler_registration_t{
          route_handler_kind_t::send, packet, std::type_index (typeid (TOwner)),
          std::type_index (typeid (TMessage)), std::type_index (typeid (void)),
          [method] (service_provider_t &services, serializer_registry_t &serializers,
                    const zlink::message_t &message,
                    const route_handler_context_t &context) -> task_t<zlink::message_t> {
              try {
                  auto &owner = services.get_required<TOwner> ();
                  auto payload = serializers.get<TMessage> ().deserialize (message);
                  (owner.*method) (payload, context);
                  return task_t<zlink::message_t> (
                    result_t<zlink::message_t>::success (zlink::message_t{}));
              }
              catch (const framework_exception_t &error) {
                  return task_t<zlink::message_t> (result_t<zlink::message_t>::failure (
                    error.kind (), error.what (), error.is_retriable ()));
              }
              catch (...) {
                  return task_t<zlink::message_t> (
                    result_t<zlink::message_t>::failure (framework_error_kind_t::request_failed,
                                                         "routed send handler threw an exception"));
              }
          }});
    }

    template <typename TOwner, typename TMessage>
    route_channel_builder_t &add_send_handler (std::string packet_name,
                                               void (TOwner::*method) (const TMessage &))
    {
        const auto packet =
          packet_name.empty () ? detail::message_name<TMessage> () : std::move (packet_name);
        return add_handler (route_handler_registration_t{
          route_handler_kind_t::send, packet, std::type_index (typeid (TOwner)),
          std::type_index (typeid (TMessage)), std::type_index (typeid (void)),
          [method] (service_provider_t &services, serializer_registry_t &serializers,
                    const zlink::message_t &message,
                    const route_handler_context_t &) -> task_t<zlink::message_t> {
              try {
                  auto &owner = services.get_required<TOwner> ();
                  auto payload = serializers.get<TMessage> ().deserialize (message);
                  (owner.*method) (payload);
                  return task_t<zlink::message_t> (
                    result_t<zlink::message_t>::success (zlink::message_t{}));
              }
              catch (const framework_exception_t &error) {
                  return task_t<zlink::message_t> (result_t<zlink::message_t>::failure (
                    error.kind (), error.what (), error.is_retriable ()));
              }
              catch (...) {
                  return task_t<zlink::message_t> (result_t<zlink::message_t>::failure (
                    framework_error_kind_t::request_failed,
                    "routed send handler threw an exception"));
              }
          }});
    }

    template <typename TOwner, typename TMessage>
    route_channel_builder_t &add_send_handler (std::string packet_name,
                                               task_t<void> (TOwner::*method) (const TMessage &))
    {
        const auto packet =
          packet_name.empty () ? detail::message_name<TMessage> () : std::move (packet_name);
        return add_handler (route_handler_registration_t{
          route_handler_kind_t::send, packet, std::type_index (typeid (TOwner)),
          std::type_index (typeid (TMessage)), std::type_index (typeid (void)),
          [method] (service_provider_t &services, serializer_registry_t &serializers,
                    const zlink::message_t &message,
                    const route_handler_context_t &) -> task_t<zlink::message_t> {
              try {
                  auto &owner = services.get_required<TOwner> ();
                  auto payload = serializers.get<TMessage> ().deserialize (message);
                  co_await (owner.*method) (payload);
                  co_return result_t<zlink::message_t>::success (zlink::message_t{});
              }
              catch (const framework_exception_t &error) {
                  co_return result_t<zlink::message_t>::failure (
                    error.kind (), error.what (), error.is_retriable ());
              }
              catch (...) {
                  co_return result_t<zlink::message_t>::failure (
                    framework_error_kind_t::request_failed,
                    "routed send handler threw an exception");
              }
          }});
    }

    template <typename TOwner, typename TRequest, typename TReply>
    route_channel_builder_t &add_request_handler (
      std::string packet_name,
      TReply (TOwner::*method) (const TRequest &, const route_handler_context_t &))
    {
        const auto packet =
          packet_name.empty () ? detail::message_name<TRequest> () : std::move (packet_name);
        return add_handler (route_handler_registration_t{
          route_handler_kind_t::request, packet, std::type_index (typeid (TOwner)),
          std::type_index (typeid (TRequest)), std::type_index (typeid (TReply)),
          [method] (service_provider_t &services, serializer_registry_t &serializers,
                    const zlink::message_t &message,
                    const route_handler_context_t &context) -> task_t<zlink::message_t> {
              try {
                  auto &owner = services.get_required<TOwner> ();
                  auto request = serializers.get<TRequest> ().deserialize (message);
                  auto reply = (owner.*method) (request, context);
                  return task_t<zlink::message_t> (result_t<zlink::message_t>::success (
                    serializers.get<TReply> ().serialize (reply)));
              }
              catch (const framework_exception_t &error) {
                  return task_t<zlink::message_t> (result_t<zlink::message_t>::failure (
                    error.kind (), error.what (), error.is_retriable ()));
              }
              catch (...) {
                  return task_t<zlink::message_t> (result_t<zlink::message_t>::failure (
                    framework_error_kind_t::request_failed,
                    "routed request handler threw an exception"));
              }
          }});
    }

    template <typename TOwner, typename TRequest, typename TReply>
    route_channel_builder_t &add_request_handler (std::string packet_name,
                                                  TReply (TOwner::*method) (const TRequest &))
    {
        const auto packet =
          packet_name.empty () ? detail::message_name<TRequest> () : std::move (packet_name);
        return add_handler (route_handler_registration_t{
          route_handler_kind_t::request, packet, std::type_index (typeid (TOwner)),
          std::type_index (typeid (TRequest)), std::type_index (typeid (TReply)),
          [method] (service_provider_t &services, serializer_registry_t &serializers,
                    const zlink::message_t &message,
                    const route_handler_context_t &) -> task_t<zlink::message_t> {
              try {
                  auto &owner = services.get_required<TOwner> ();
                  auto request = serializers.get<TRequest> ().deserialize (message);
                  auto reply = (owner.*method) (request);
                  return task_t<zlink::message_t> (result_t<zlink::message_t>::success (
                    serializers.get<TReply> ().serialize (reply)));
              }
              catch (const framework_exception_t &error) {
                  return task_t<zlink::message_t> (result_t<zlink::message_t>::failure (
                    error.kind (), error.what (), error.is_retriable ()));
              }
              catch (...) {
                  return task_t<zlink::message_t> (result_t<zlink::message_t>::failure (
                    framework_error_kind_t::request_failed,
                    "routed request handler threw an exception"));
              }
          }});
    }

    template <typename TOwner, typename TRequest, typename TReply>
    route_channel_builder_t &
    add_request_handler (std::string packet_name,
                         task_t<TReply> (TOwner::*method) (const TRequest &))
    {
        const auto packet =
          packet_name.empty () ? detail::message_name<TRequest> () : std::move (packet_name);
        return add_handler (route_handler_registration_t{
          route_handler_kind_t::request, packet, std::type_index (typeid (TOwner)),
          std::type_index (typeid (TRequest)), std::type_index (typeid (TReply)),
          [method] (service_provider_t &services, serializer_registry_t &serializers,
                    const zlink::message_t &message,
                    const route_handler_context_t &) -> task_t<zlink::message_t> {
              try {
                  auto &owner = services.get_required<TOwner> ();
                  auto request = serializers.get<TRequest> ().deserialize (message);
                  auto reply = co_await (owner.*method) (request);
                  co_return result_t<zlink::message_t>::success (
                    serializers.get<TReply> ().serialize (reply));
              }
              catch (const framework_exception_t &error) {
                  co_return result_t<zlink::message_t>::failure (
                    error.kind (), error.what (), error.is_retriable ());
              }
              catch (...) {
                  co_return result_t<zlink::message_t>::failure (
                    framework_error_kind_t::request_failed,
                    "routed request handler threw an exception");
              }
          }});
    }

  private:
    friend class zlink_builder_t;
    explicit route_channel_builder_t (std::shared_ptr<detail::route_channel_builder_state_t> state);

    route_channel_builder_t &add_handler (route_handler_registration_t registration);

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

    template <typename TRequest>
    channel_request_call_t request (std::string channel_name, TRequest request)
    {
        auto state = _state;
        return channel_request_call_t (
          detail::message_name<TRequest> (),
          serializers (),
          [state, channel_name = std::move (channel_name), request = std::move (request)] (
            const std::string &packet_name, std::chrono::milliseconds timeout,
            const channel_request_call_t::metadata_map_t &metadata) {
              auto bus = message_bus_t (state);
              const auto effective_timeout = timeout > std::chrono::milliseconds::zero ()
                                               ? timeout
                                               : bus.default_request_timeout (channel_name);
              return task_t<zlink::message_t> (
                bus.submit_request (channel_name, packet_name, std::type_index (typeid (TRequest)),
                                    &request, effective_timeout, metadata)
                  .message ());
          });
    }

    template <typename TMessage> send_call_t send (std::string channel_name, TMessage message)
    {
        auto state = _state;
        return send_call_t (
          detail::message_name<TMessage> (),
            [state, channel_name = std::move (channel_name), message = std::move (message)] (
            const std::string &packet_name, std::chrono::milliseconds timeout,
            const send_call_t::metadata_map_t &metadata) {
              return task_t<void> (
                message_bus_t (state).submit_send (channel_name, packet_name,
                                                   std::type_index (typeid (TMessage)),
                                                   &message, timeout, metadata));
          });
    }

    template <typename TEvent>
    send_call_t publish (std::string channel_name, std::string topic, TEvent event)
    {
        return send_call_t (detail::message_name<TEvent> (),
                            [state = _state, channel_name = std::move (channel_name),
                             topic = std::move (topic), event = std::move (event)] (
                              const std::string &packet_name, std::chrono::milliseconds timeout,
                              const send_call_t::metadata_map_t &metadata) {
                                return task_t<void> (message_bus_t (state).submit_publish (
                                  channel_name, topic, packet_name,
                                  std::type_index (typeid (TEvent)), &event, timeout, metadata));
                            });
    }

    std::size_t pending_count () const noexcept;
    std::size_t pending_limit () const noexcept;
    std::chrono::milliseconds default_request_timeout (const std::string &channel_name) const;

  private:
    friend class zlink_builder_t;
    friend class request_client_t;
    friend class publisher_t;
    friend class spot_context_t;
    friend class detail::channel_outbound_exchange_t;
    friend class detail::channel_runtime_t;
    friend class detail::channel_runtime_manager_t;

    class erased_request_result_t
    {
      public:
        explicit erased_request_result_t (framework_exception_t error);
        erased_request_result_t (zlink::message_t reply, serializer_registry_t &serializers);

        result_t<zlink::message_t> message () const
        {
            if (_reply) {
                return result_t<zlink::message_t>::success (*_reply);
            }
            return result_t<zlink::message_t>::failure (_error.kind (), _error.what (),
                                                        _error.is_retriable ());
        }

        template <typename TReply> result_t<TReply> as () const
        {
            if (_reply) {
                if (_serializers == nullptr) {
                    return result_t<TReply>::failure (
                      framework_error_kind_t::request_protocol_error,
                      "channel request has no serializer registry");
                }
                try {
                    return result_t<TReply>::success (
                      _serializers->get<TReply> ().deserialize (*_reply));
                }
                catch (const framework_exception_t &error) {
                    return result_t<TReply>::failure (error.kind (), error.what (),
                                                      error.is_retriable ());
                }
            }
            return result_t<TReply>::failure (_error.kind (), _error.what (),
                                              _error.is_retriable ());
        }

      private:
        framework_exception_t _error;
        std::optional<zlink::message_t> _reply;
        serializer_registry_t *_serializers = nullptr;
    };

    explicit message_bus_t (std::shared_ptr<detail::channel_runtime_state_t> state);

    erased_request_result_t
    submit_request (std::string channel_name,
                    std::string packet_name,
                    std::type_index request_type,
                    const void *request,
                    std::chrono::milliseconds timeout,
                    const channel_request_call_t::metadata_map_t &metadata);
    serializer_registry_t *serializers () const noexcept;
    result_t<void> submit_send (std::string channel_name,
                                std::string packet_name,
                                std::type_index message_type,
                                const void *message,
                                std::chrono::milliseconds timeout,
                                const send_call_t::metadata_map_t &metadata);
    result_t<void> submit_publish (std::string channel_name,
                                   std::string topic,
                                   std::string packet_name,
                                   std::type_index event_type,
                                   const void *event,
                                   std::chrono::milliseconds timeout,
                                   const send_call_t::metadata_map_t &metadata);

    std::shared_ptr<detail::channel_runtime_state_t> _state;
};

class route_send_call_t
{
  public:
    using metadata_map_t = std::map<std::string, std::string>;
    using submit_fn_t = std::function<task_t<void> (const std::string &, const metadata_map_t &)>;

    route_send_call_t (std::string packet_name, submit_fn_t submit);

    route_send_call_t &packet_name (std::string packet_name);
    route_send_call_t &metadata (std::string key, std::string value);
    task_t<void> async ();

  private:
    std::string _packet_name;
    metadata_map_t _metadata;
    submit_fn_t _submit;
};

class route_request_call_t
{
  public:
    using metadata_map_t = std::map<std::string, std::string>;
    using submit_fn_t = std::function<task_t<std::uint64_t> (
      const std::string &, std::chrono::milliseconds, const metadata_map_t &)>;
    using typed_submit_fn_t = std::function<task_t<zlink::message_t> (
      const std::string &, std::chrono::milliseconds, const metadata_map_t &)>;

    route_request_call_t (std::string packet_name, submit_fn_t submit);
    route_request_call_t (std::string packet_name,
                          submit_fn_t submit,
                          serializer_registry_t *serializers,
                          typed_submit_fn_t typed_submit) :
        _packet_name (std::move (packet_name)),
        _submit (std::move (submit)),
        _serializers (serializers),
        _typed_submit (std::move (typed_submit))
    {
    }

    route_request_call_t &packet_name (std::string packet_name);
    route_request_call_t &timeout (std::chrono::milliseconds timeout);
    route_request_call_t &metadata (std::string key, std::string value);
    task_t<std::uint64_t> async ();
    template <typename TReply> task_t<TReply> async ()
    {
        if (!_typed_submit) {
            co_return result_t<TReply>::failure (
              framework_error_kind_t::request_protocol_error,
              "typed route request call is not bound to a route client");
        }
        auto reply = co_await _typed_submit (_packet_name, _timeout, _metadata);
        if (_serializers == nullptr) {
            co_return result_t<TReply>::failure (
              framework_error_kind_t::request_protocol_error,
              "route client has no serializer registry");
        }
        try {
            co_return _serializers->get<TReply> ().deserialize (reply);
        }
        catch (const framework_exception_t &error) {
            co_return result_t<TReply>::failure (
              error.kind (), error.what (), error.is_retriable ());
        }
    }

  private:
    std::string _packet_name;
    std::chrono::milliseconds _timeout{0};
    metadata_map_t _metadata;
    submit_fn_t _submit;
    serializer_registry_t *_serializers = nullptr;
    typed_submit_fn_t _typed_submit;
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

    template <typename TMessage>
    route_send_call_t
    send (std::string router_channel_id, zlink::routing_id_t target_node_rid, TMessage message)
    {
        auto state = _state;
        return route_send_call_t (
          detail::message_name<TMessage> (),
          [state, router_channel_id = std::move (router_channel_id),
           target_node_rid = std::move (target_node_rid), message = std::move (message)] (
            const std::string &packet_name,
            const route_send_call_t::metadata_map_t &metadata) -> task_t<void> {
              return submit_send_erased (state, router_channel_id, target_node_rid, packet_name,
                                         std::type_index (typeid (TMessage)), &message, metadata);
          });
    }

    template <typename TRequest>
    route_request_call_t
    request (std::string router_channel_id, zlink::routing_id_t target_node_rid, TRequest request)
    {
        auto state = _state;
        auto request_value = std::make_shared<TRequest> (std::move (request));
        return route_request_call_t (
          detail::message_name<TRequest> (),
          [state, router_channel_id = std::move (router_channel_id),
           target_node_rid = std::move (target_node_rid), request_value] (
            const std::string &packet_name, std::chrono::milliseconds timeout,
            const route_request_call_t::metadata_map_t &metadata) -> task_t<std::uint64_t> {
              return submit_request_erased (state, router_channel_id, target_node_rid, packet_name,
                                            std::type_index (typeid (TRequest)),
                                            request_value.get (), timeout, metadata);
          },
          _serializers,
          [state, router_channel_id, target_node_rid, request_value] (
            const std::string &packet_name, std::chrono::milliseconds timeout,
            const route_request_call_t::metadata_map_t &metadata) mutable -> task_t<zlink::message_t> {
              return submit_request_reply_message_erased (
                state, router_channel_id, target_node_rid, packet_name,
                std::type_index (typeid (TRequest)), request_value.get (), timeout, metadata);
          });
    }

  private:
    friend class zlink_builder_t;
    explicit route_client_t (std::shared_ptr<detail::route_client_state_t> state,
                             serializer_registry_t &serializers);

    static task_t<void>
    submit_send_erased (const std::shared_ptr<detail::route_client_state_t> &state,
                        const std::string &router_channel_id,
                        const zlink::routing_id_t &target_node_rid,
                        const std::string &packet_name,
                        std::type_index message_type,
                        const void *message,
                        const route_send_call_t::metadata_map_t &metadata);

    static task_t<std::uint64_t>
    submit_request_erased (const std::shared_ptr<detail::route_client_state_t> &state,
                           const std::string &router_channel_id,
                           const zlink::routing_id_t &target_node_rid,
                           const std::string &packet_name,
                           std::type_index request_type,
                           const void *request,
                           std::chrono::milliseconds timeout,
                           const route_request_call_t::metadata_map_t &metadata);

    template <typename TReply>
    static task_t<TReply>
    submit_request_reply_erased (const std::shared_ptr<detail::route_client_state_t> &state,
                                 serializer_registry_t *serializers,
                                 std::string router_channel_id,
                                 zlink::routing_id_t target_node_rid,
                                 std::string packet_name,
                                 std::type_index request_type,
                                 const void *request,
                                 std::chrono::milliseconds timeout,
                                 const std::map<std::string, std::string> &metadata);

    static task_t<zlink::message_t>
    submit_request_reply_message_erased (const std::shared_ptr<detail::route_client_state_t> &state,
                                         std::string router_channel_id,
                                         zlink::routing_id_t target_node_rid,
                                         std::string packet_name,
                                         std::type_index request_type,
                                         const void *request,
                                         std::chrono::milliseconds timeout,
                                         std::map<std::string, std::string> metadata);

    std::shared_ptr<detail::route_client_state_t> _state;
    serializer_registry_t *_serializers = nullptr;
};

template <typename TReply>
task_t<TReply> route_client_t::submit_request_reply_erased (
  const std::shared_ptr<detail::route_client_state_t> &state,
  serializer_registry_t *serializers,
  std::string router_channel_id,
  zlink::routing_id_t target_node_rid,
  std::string packet_name,
  std::type_index request_type,
  const void *request,
  std::chrono::milliseconds timeout,
  const std::map<std::string, std::string> &metadata)
{
    if (serializers == nullptr) {
        co_return result_t<TReply>::failure (framework_error_kind_t::request_protocol_error,
                                             "route client has no serializer registry");
    }
    auto reply = co_await submit_request_reply_message_erased (
      state, std::move (router_channel_id), std::move (target_node_rid), std::move (packet_name),
      request_type, request, timeout, metadata);
    try {
        co_return result_t<TReply>::success (serializers->get<TReply> ().deserialize (reply));
    }
    catch (const framework_exception_t &error) {
        co_return result_t<TReply>::failure (error.kind (), error.what (), error.is_retriable ());
    }
}

class request_client_t
{
  public:
    request_client_t (message_bus_t bus, std::string channel_name);

    template <typename TRequest> channel_request_call_t request (TRequest request)
    {
        return _bus.request (_channel_name, std::move (request));
    }

  private:
    message_bus_t _bus;
    std::string _channel_name;
};

class channel_client_t
{
  public:
    explicit channel_client_t (message_bus_t bus) : _bus (std::move (bus)) {}

    template <typename TRequest>
    channel_request_call_t request_to_channel (std::string channel_name, TRequest request)
    {
        return _bus.request (std::move (channel_name), std::move (request));
    }

    template <typename TRequest>
    channel_request_call_t request (std::string channel_name, TRequest request)
    {
        return request_to_channel (std::move (channel_name), std::move (request));
    }

    template <typename TMessage>
    send_call_t send_to_channel (std::string channel_name, TMessage message)
    {
        return _bus.send (std::move (channel_name), std::move (message));
    }

    template <typename TMessage>
    send_call_t send (std::string channel_name, TMessage message)
    {
        return send_to_channel (std::move (channel_name), std::move (message));
    }

  private:
    message_bus_t _bus;
};

class publisher_t
{
  public:
    explicit publisher_t (message_bus_t bus);

    template <typename TEvent>
    send_call_t publish (std::string channel_name, std::string topic, TEvent event)
    {
        return _bus.publish (std::move (channel_name), std::move (topic), std::move (event));
    }

  private:
    message_bus_t _bus;
};

using retry_hook_t = std::function<void (const channel_reliability_event_t &)>;
using dead_letter_hook_t = std::function<void (const channel_reliability_event_t &)>;

} // namespace zlink::framework
