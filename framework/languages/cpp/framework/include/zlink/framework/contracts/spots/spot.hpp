/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/framework/contracts/channels/call.hpp>
#include <zlink/framework/contracts/channels/channel.hpp>
#include <zlink/framework/contracts/codecs/serializer.hpp>
#include <zlink/framework/contracts/configuration/services.hpp>
#include <zlink/framework/contracts/detail/message_name.hpp>
#include <zlink/framework/contracts/dispatch/task.hpp>
#include <zlink/framework/contracts/errors/result.hpp>
#include <zlink/framework/contracts/timers/timer.hpp>

#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <typeindex>
#include <type_traits>
#include <vector>

namespace zlink::framework
{

enum class spot_actor_change_kind_t
{
  join_spot = 1,
  join_entry_spot = 2,
  leave_spot = 3
};

struct spot_actor_change_result_t
{
  explicit spot_actor_change_result_t (
    spot_actor_change_kind_t kind = spot_actor_change_kind_t::join_spot)
    : kind (kind)
  {
  }

  spot_actor_change_kind_t kind;
};

struct spot_actor_message_metadata_t
{
  std::map<std::string, std::string> values;
};

class spot_actor_reply_options_t
{
public:
  spot_actor_reply_options_t &metadata (std::string key, std::string value)
  {
    metadata_values.values[std::move (key)] = std::move (value);
    return *this;
  }

  spot_actor_reply_options_t &compress (bool enabled = true)
  {
    compress_payload = enabled;
    return *this;
  }

  spot_actor_message_metadata_t metadata_values;
  bool compress_payload = false;
};

struct spot_actor_send_context_t
{
  std::string packet_name;
  std::string content_type;
  spot_actor_message_metadata_t metadata;
};

struct spot_actor_request_context_t
{
  std::string packet_name;
  std::string content_type;
  spot_actor_message_metadata_t metadata;
  spot_actor_reply_options_t reply;
};

namespace detail
{
class spot_context_state_t;
class spot_node_builder_state_t;
class spot_node_runtime_state_t;
class spot_node_runtime_t;

inline result_t<zlink::message_t>
current_spot_exception_to_message_result ()
{
  try {
    throw;
  } catch (const framework_exception_t &error) {
    return result_t<zlink::message_t>::failure (error.kind (),
                                                error.what (),
                                                error.is_retriable ());
  } catch (...) {
    return result_t<zlink::message_t>::failure (
      framework_error_kind_t::request_failed,
      "spot handler threw an exception");
  }
}

template<typename T>
struct task_value_type_t
{
};

template<typename T>
struct task_value_type_t<task_t<T>>
{
  using type = T;
};

template<typename T>
inline constexpr bool is_task_v = requires { typename task_value_type_t<T>::type; };

template<typename TResult>
task_t<zlink::message_t>
serialize_handler_result (TResult &&result, serializer_registry_t &serializers)
{
  using result_type = std::remove_cvref_t<TResult>;
  if constexpr (is_task_v<result_type>) {
    using value_type = typename task_value_type_t<result_type>::type;
    if constexpr (std::is_void_v<value_type>) {
      co_await result;
      co_return result_t<zlink::message_t>::success (zlink::message_t {});
    } else {
      auto value = co_await result;
      co_return result_t<zlink::message_t>::success (
        serializers.get<value_type> ().serialize (value));
    }
  } else {
    co_return result_t<zlink::message_t>::success (
      serializers.get<result_type> ().serialize (result));
  }
}

template<typename THandler, typename TSpot, typename TMessage>
task_t<zlink::message_t>
invoke_spot_packet (THandler &handler,
                    void *spot,
                    const TMessage &message,
                    serializer_registry_t &serializers)
{
  try {
    auto &typed_spot = *static_cast<TSpot *> (spot);
    if constexpr (requires { handler.handle (typed_spot, message); }) {
      using result_type = decltype (handler.handle (typed_spot, message));
      if constexpr (std::is_void_v<result_type>) {
        handler.handle (typed_spot, message);
        return task_t<zlink::message_t> (
          result_t<zlink::message_t>::success (zlink::message_t {}));
      } else {
        return serialize_handler_result (
          handler.handle (typed_spot, message), serializers);
      }
    } else {
      return task_t<zlink::message_t> (
        result_t<zlink::message_t>::failure (
          framework_error_kind_t::request_protocol_error,
          "spot packet handler must expose handle(spot, message)"));
    }
  } catch (...) {
    return task_t<zlink::message_t> (
      current_spot_exception_to_message_result ());
  }
}

template<typename THandler, typename TSpot, typename TActor, typename TRequest, typename TReply>
task_t<zlink::message_t>
invoke_spot_actor_join (THandler &handler,
                        void *spot,
                        void *actor,
                        const TRequest &request,
                        serializer_registry_t &serializers)
{
  try {
    auto &typed_spot = *static_cast<TSpot *> (spot);
    auto &typed_actor = *static_cast<TActor *> (actor);
    if constexpr (requires { handler.handle (typed_spot, typed_actor, request); }) {
      return serialize_handler_result (
        handler.handle (typed_spot, typed_actor, request), serializers);
    } else {
      return task_t<zlink::message_t> (
        result_t<zlink::message_t>::failure (
          framework_error_kind_t::request_protocol_error,
          "spot actor join handler must expose handle(spot, actor, request)"));
    }
  } catch (...) {
    return task_t<zlink::message_t> (
      current_spot_exception_to_message_result ());
  }
}

template<typename THandler, typename TSpot, typename TActor, typename TMessage>
task_t<zlink::message_t>
invoke_spot_actor_packet (THandler &handler,
                          void *spot,
                          void *actor,
                          const spot_actor_send_context_t &send_context,
                          spot_actor_request_context_t &request_context,
                          const TMessage &message,
                          serializer_registry_t &serializers)
{
  try {
    auto &typed_spot = *static_cast<TSpot *> (spot);
    auto &typed_actor = *static_cast<TActor *> (actor);
    if constexpr (requires {
                    handler.handle (typed_spot, typed_actor, request_context,
                                    message);
                  }) {
      return serialize_handler_result (
        handler.handle (typed_spot, typed_actor, request_context, message),
        serializers);
    } else if constexpr (requires {
                           handler.handle (typed_spot, typed_actor,
                                           send_context, message);
                         }) {
      using result_type =
        decltype (handler.handle (typed_spot, typed_actor, send_context,
                                  message));
      if constexpr (std::is_void_v<result_type>) {
        handler.handle (typed_spot, typed_actor, send_context, message);
        return task_t<zlink::message_t> (
          result_t<zlink::message_t>::success (zlink::message_t {}));
      } else {
        return serialize_handler_result (
          handler.handle (typed_spot, typed_actor, send_context, message),
          serializers);
      }
    } else {
      return task_t<zlink::message_t> (
        result_t<zlink::message_t>::failure (
          framework_error_kind_t::request_protocol_error,
          "spot actor packet handler must expose handle(spot, actor, context, message)"));
    }
  } catch (...) {
    return task_t<zlink::message_t> (
      current_spot_exception_to_message_result ());
  }
}

template<typename THandler, typename TSpot, typename TActor>
task_t<zlink::message_t>
invoke_spot_actor_lifecycle (THandler &handler,
                             void *spot,
                             void *actor,
                             const spot_actor_change_result_t *result,
                             serializer_registry_t &serializers)
{
  try {
    auto &typed_spot = *static_cast<TSpot *> (spot);
    auto &typed_actor = *static_cast<TActor *> (actor);
    if (result == nullptr) {
      if constexpr (requires { handler.handle (typed_spot, typed_actor); }) {
        using result_type = decltype (handler.handle (typed_spot, typed_actor));
        if constexpr (std::is_void_v<result_type>) {
          handler.handle (typed_spot, typed_actor);
          return task_t<zlink::message_t> (
            result_t<zlink::message_t>::success (zlink::message_t {}));
        } else {
          return serialize_handler_result (
            handler.handle (typed_spot, typed_actor), serializers);
        }
      } else {
        return task_t<zlink::message_t> (
          result_t<zlink::message_t>::failure (
            framework_error_kind_t::request_protocol_error,
            "spot actor disconnected handler must expose handle(spot, actor)"));
      }
    }
    if constexpr (requires {
                    handler.handle (typed_spot, typed_actor, *result);
                  }) {
      using result_type =
        decltype (handler.handle (typed_spot, typed_actor, *result));
      if constexpr (std::is_void_v<result_type>) {
        handler.handle (typed_spot, typed_actor, *result);
        return task_t<zlink::message_t> (
          result_t<zlink::message_t>::success (zlink::message_t {}));
      } else {
        return serialize_handler_result (
          handler.handle (typed_spot, typed_actor, *result), serializers);
      }
    } else {
      return task_t<zlink::message_t> (
        result_t<zlink::message_t>::failure (
          framework_error_kind_t::request_protocol_error,
          "spot actor lifecycle handler must expose handle(spot, actor, result)"));
    }
  } catch (...) {
    return task_t<zlink::message_t> (
      current_spot_exception_to_message_result ());
  }
}
} // namespace detail

enum class spot_handler_kind_t
{
  packet,
  subscription,
  actor_join,
  actor_packet,
  post_actor_joined,
  actor_left,
  actor_disconnected
};

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

struct spot_handler_descriptor_t
{
  spot_handler_kind_t kind;
  std::string packet_name;
  std::string topic;
  std::type_index handler_type;
  std::type_index payload_type;
  std::type_index actor_type;
  std::type_index reply_type;
};

struct spot_node_snapshot_t
{
  std::string name;
  std::string bind_endpoint;
  std::optional<std::string> router_bind_endpoint;
  std::optional<std::string> pub_bind_endpoint;
  std::optional<zlink::routing_id_t> router_routing_id;
  std::optional<zlink::routing_id_t> pub_routing_id;
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

class spot_handler_registry_t;

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
  spot_handler_registry_t handlers ();

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

class spot_handler_registry_t
{
public:
  using invoker_t =
    std::function<task_t<zlink::message_t> (void *,
                                            void *,
                                            service_provider_t &,
                                            serializer_registry_t &,
                                            const zlink::message_t &,
                                            const spot_actor_change_result_t *)>;

  spot_handler_registry_t ();
  ~spot_handler_registry_t ();

  spot_handler_registry_t (spot_handler_registry_t &&) noexcept;
  spot_handler_registry_t &operator= (spot_handler_registry_t &&) noexcept;
  spot_handler_registry_t (const spot_handler_registry_t &) = default;
  spot_handler_registry_t &operator= (const spot_handler_registry_t &) =
    default;

  template<typename THandler, typename TSpot, typename TMessage>
  spot_handler_registry_t &add_handler (std::string packet_name =
                                          detail::message_name<TMessage> ())
  {
    return add_handler_erased (
      spot_handler_kind_t::packet,
      std::move (packet_name),
      {},
      std::type_index (typeid (THandler)),
      std::type_index (typeid (TMessage)),
      std::type_index (typeid (void)),
      std::type_index (typeid (void)),
      [](void *spot,
         void *,
         service_provider_t &services,
         serializer_registry_t &serializers,
         const zlink::message_t &message,
         const spot_actor_change_result_t *) {
        auto &handler = services.get_required<THandler> ();
        auto payload = serializers.get<TMessage> ().deserialize (message);
        return detail::invoke_spot_packet<THandler, TSpot, TMessage> (
          handler, spot, payload, serializers);
      });
  }

  template<typename THandler>
  spot_handler_registry_t &add_handler (
    std::string packet_name =
      detail::message_name<typename THandler::request_type> ())
  {
    return add_handler<THandler,
                       typename THandler::spot_type,
                       typename THandler::request_type> (
      std::move (packet_name));
  }

  template<typename THandler, typename TSpot, typename TEvent>
  spot_handler_registry_t &add_subscribe (std::string topic)
  {
    return add_handler_erased (
      spot_handler_kind_t::subscription,
      detail::message_name<TEvent> (),
      std::move (topic),
      std::type_index (typeid (THandler)),
      std::type_index (typeid (TEvent)),
      std::type_index (typeid (void)),
      std::type_index (typeid (void)),
      [](void *spot,
         void *,
         service_provider_t &services,
         serializer_registry_t &serializers,
         const zlink::message_t &message,
         const spot_actor_change_result_t *) {
        auto &handler = services.get_required<THandler> ();
        auto payload = serializers.get<TEvent> ().deserialize (message);
        return detail::invoke_spot_packet<THandler, TSpot, TEvent> (
          handler, spot, payload, serializers);
      });
  }

  template<typename THandler>
  spot_handler_registry_t &add_subscribe (std::string topic)
  {
    return add_subscribe<THandler,
                         typename THandler::spot_type,
                         typename THandler::event_type> (
      std::move (topic));
  }

  template<typename THandler, typename TSpot, typename TActor, typename TRequest, typename TReply>
  spot_handler_registry_t &add_actor_join (
    std::string packet_name = detail::message_name<TRequest> ())
  {
    return add_handler_erased (
      spot_handler_kind_t::actor_join,
      std::move (packet_name),
      {},
      std::type_index (typeid (THandler)),
      std::type_index (typeid (TRequest)),
      std::type_index (typeid (TActor)),
      std::type_index (typeid (TReply)),
      [](void *spot,
         void *actor,
         service_provider_t &services,
         serializer_registry_t &serializers,
         const zlink::message_t &message,
         const spot_actor_change_result_t *) {
        auto &handler = services.get_required<THandler> ();
        auto request = serializers.get<TRequest> ().deserialize (message);
        return detail::invoke_spot_actor_join<THandler,
                                              TSpot,
                                              TActor,
                                              TRequest,
                                              TReply> (
          handler, spot, actor, request, serializers);
      });
  }

  template<typename THandler>
  spot_handler_registry_t &add_actor_join (
    std::string packet_name =
      detail::message_name<typename THandler::request_type> ())
  {
    return add_actor_join<THandler,
                          typename THandler::spot_type,
                          typename THandler::actor_type,
                          typename THandler::request_type,
                          typename THandler::reply_type> (
      std::move (packet_name));
  }

  template<typename THandler, typename TSpot, typename TActor, typename TMessage>
  spot_handler_registry_t &add_actor_packet (
    std::string packet_name = detail::message_name<TMessage> ())
  {
    auto registered_packet_name = packet_name;
    return add_handler_erased (
      spot_handler_kind_t::actor_packet,
      std::move (packet_name),
      {},
      std::type_index (typeid (THandler)),
      std::type_index (typeid (TMessage)),
      std::type_index (typeid (TActor)),
      std::type_index (typeid (void)),
      [registered_packet_name = std::move (registered_packet_name)](
         void *spot,
         void *actor,
         service_provider_t &services,
         serializer_registry_t &serializers,
         const zlink::message_t &message,
         const spot_actor_change_result_t *) {
        auto &handler = services.get_required<THandler> ();
        auto payload = serializers.get<TMessage> ().deserialize (message);
        spot_actor_send_context_t send_context {
          registered_packet_name,
          "application/json",
          {} };
        spot_actor_request_context_t request_context {
          registered_packet_name,
          "application/json",
          {},
          {} };
        return detail::invoke_spot_actor_packet<THandler,
                                                TSpot,
                                                TActor,
                                                TMessage> (
          handler, spot, actor, send_context, request_context, payload,
          serializers);
      });
  }

  template<typename THandler>
  spot_handler_registry_t &add_actor_packet (
    std::string packet_name =
      detail::message_name<typename THandler::request_type> ())
  {
    return add_actor_packet<THandler,
                            typename THandler::spot_type,
                            typename THandler::actor_type,
                            typename THandler::request_type> (
      std::move (packet_name));
  }

  template<typename THandler, typename TSpot, typename TActor>
  spot_handler_registry_t &add_post_actor_joined ()
  {
    return add_actor_lifecycle_handler<THandler, TSpot, TActor> (
      spot_handler_kind_t::post_actor_joined);
  }

  template<typename THandler>
  spot_handler_registry_t &add_post_actor_joined ()
  {
    return add_post_actor_joined<THandler,
                                 typename THandler::spot_type,
                                 typename THandler::actor_type> ();
  }

  template<typename THandler, typename TSpot, typename TActor>
  spot_handler_registry_t &add_actor_left ()
  {
    return add_actor_lifecycle_handler<THandler, TSpot, TActor> (
      spot_handler_kind_t::actor_left);
  }

  template<typename THandler>
  spot_handler_registry_t &add_actor_left ()
  {
    return add_actor_left<THandler,
                          typename THandler::spot_type,
                          typename THandler::actor_type> ();
  }

  template<typename THandler, typename TSpot, typename TActor>
  spot_handler_registry_t &add_actor_disconnected ()
  {
    return add_actor_lifecycle_handler<THandler, TSpot, TActor> (
      spot_handler_kind_t::actor_disconnected);
  }

  template<typename THandler>
  spot_handler_registry_t &add_actor_disconnected ()
  {
    return add_actor_disconnected<THandler,
                                  typename THandler::spot_type,
                                  typename THandler::actor_type> ();
  }

  std::vector<spot_handler_descriptor_t> descriptors () const;

  template<typename TSpot>
  result_t<zlink::message_t> invoke_packet (
    std::string_view packet_name,
    TSpot &spot,
    service_provider_t &services,
    serializer_registry_t &serializers,
    const zlink::message_t &message) const
  {
    return invoke_erased (spot_handler_kind_t::packet,
                          packet_name,
                          {},
                          std::type_index (typeid (void)),
                          &spot,
                          nullptr,
                          services,
                          serializers,
                          message)
      .result ();
  }

  template<typename TSpot, typename TActor>
  result_t<zlink::message_t> invoke_actor_join (
    std::string_view packet_name,
    TSpot &spot,
    TActor &actor,
    service_provider_t &services,
    serializer_registry_t &serializers,
    const zlink::message_t &message) const
  {
    return invoke_erased (spot_handler_kind_t::actor_join,
                          packet_name,
                          {},
                          std::type_index (typeid (TActor)),
                          &spot,
                          &actor,
                          services,
                          serializers,
                          message)
      .result ();
  }

  template<typename TSpot, typename TActor>
  result_t<zlink::message_t> invoke_actor_packet (
    std::string_view packet_name,
    TSpot &spot,
    TActor &actor,
    service_provider_t &services,
    serializer_registry_t &serializers,
    const zlink::message_t &message) const
  {
    return invoke_erased (spot_handler_kind_t::actor_packet,
                          packet_name,
                          {},
                          std::type_index (typeid (TActor)),
                          &spot,
                          &actor,
                          services,
                          serializers,
                          message)
      .result ();
  }

  template<typename TSpot, typename TActor>
  result_t<zlink::message_t> invoke_post_actor_joined (
    TSpot &spot,
    TActor &actor,
    service_provider_t &services,
    serializer_registry_t &serializers,
    spot_actor_change_result_t result =
      spot_actor_change_result_t (spot_actor_change_kind_t::join_spot)) const
  {
    return invoke_actor_lifecycle<TSpot, TActor> (
      spot_handler_kind_t::post_actor_joined, spot, actor, services,
      serializers, result)
      .result ();
  }

  template<typename TSpot, typename TActor>
  result_t<zlink::message_t> invoke_actor_left (
    TSpot &spot,
    TActor &actor,
    service_provider_t &services,
    serializer_registry_t &serializers,
    spot_actor_change_result_t result =
      spot_actor_change_result_t (spot_actor_change_kind_t::leave_spot)) const
  {
    return invoke_actor_lifecycle<TSpot, TActor> (
      spot_handler_kind_t::actor_left, spot, actor, services, serializers,
      result)
      .result ();
  }

  template<typename TSpot, typename TActor>
  result_t<zlink::message_t> invoke_actor_disconnected (
    TSpot &spot,
    TActor &actor,
    service_provider_t &services,
    serializer_registry_t &serializers) const
  {
    return invoke_actor_lifecycle<TSpot, TActor> (
      spot_handler_kind_t::actor_disconnected, spot, actor, services,
      serializers)
      .result ();
  }

private:
  friend class spot_context_t;
  explicit spot_handler_registry_t (
    std::shared_ptr<detail::spot_context_state_t> state);

  template<typename THandler, typename TSpot, typename TActor>
  spot_handler_registry_t &add_actor_lifecycle_handler (
    spot_handler_kind_t kind)
  {
    return add_handler_erased (
      kind,
      {},
      {},
      std::type_index (typeid (THandler)),
      std::type_index (typeid (void)),
      std::type_index (typeid (TActor)),
      std::type_index (typeid (void)),
      [](void *spot,
         void *actor,
         service_provider_t &services,
         serializer_registry_t &serializers,
         const zlink::message_t &,
         const spot_actor_change_result_t *result) {
        auto &handler = services.get_required<THandler> ();
        return detail::invoke_spot_actor_lifecycle<THandler, TSpot, TActor> (
          handler, spot, actor, result, serializers);
      });
  }

  spot_handler_registry_t &add_handler_erased (
    spot_handler_kind_t kind,
    std::string packet_name,
    std::string topic,
    std::type_index handler_type,
    std::type_index payload_type,
    std::type_index actor_type,
    std::type_index reply_type,
    invoker_t invoker);

  task_t<zlink::message_t> invoke_erased (
    spot_handler_kind_t kind,
    std::string_view packet_name,
    std::string_view topic,
    std::type_index actor_type,
    void *spot,
    void *actor,
    service_provider_t &services,
    serializer_registry_t &serializers,
    const zlink::message_t &message,
    const spot_actor_change_result_t *change_result = nullptr) const;

  template<typename TSpot, typename TActor>
  task_t<zlink::message_t> invoke_actor_lifecycle (
    spot_handler_kind_t kind,
    TSpot &spot,
    TActor &actor,
    service_provider_t &services,
    serializer_registry_t &serializers,
    const spot_actor_change_result_t &change_result) const
  {
    return invoke_erased (kind,
                          {},
                          {},
                          std::type_index (typeid (TActor)),
                          &spot,
                          &actor,
                          services,
                          serializers,
                          zlink::message_t {},
                          &change_result);
  }

  template<typename TSpot, typename TActor>
  task_t<zlink::message_t> invoke_actor_lifecycle (
    spot_handler_kind_t kind,
    TSpot &spot,
    TActor &actor,
    service_provider_t &services,
    serializer_registry_t &serializers) const
  {
    return invoke_erased (kind,
                          {},
                          {},
                          std::type_index (typeid (TActor)),
                          &spot,
                          &actor,
                          services,
                          serializers,
                          zlink::message_t {},
                          nullptr);
  }

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
  spot_node_builder_t &enable_router (std::string endpoint);
  spot_node_builder_t &enable_router (std::string endpoint,
                                      zlink::routing_id_t routing_id);
  spot_node_builder_t &enable_pub_sub (std::string endpoint);
  spot_node_builder_t &enable_pub_sub (std::string endpoint,
                                       zlink::routing_id_t routing_id);
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
