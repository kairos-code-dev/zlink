/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/framework/contracts/channels/call.hpp>
#include <zlink/framework/contracts/channels/channel.hpp>
#include <zlink/framework/contracts/codecs/serializer.hpp>
#include <zlink/framework/contracts/configuration/services.hpp>
#include <zlink/framework/contracts/detail/handler_invocation.hpp>
#include <zlink/framework/contracts/detail/message_name.hpp>
#include <zlink/framework/contracts/dispatch/task.hpp>
#include <zlink/framework/contracts/errors/result.hpp>
#include <zlink/framework/contracts/timers/timer.hpp>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <typeindex>
#include <type_traits>
#include <utility>
#include <vector>

namespace zlink::framework
{

enum class spot_actor_change_kind_t
{
    join_spot = 1,
    join_entry_spot = 2,
    leave_spot = 3
};

class spot_t
{
  public:
    virtual ~spot_t () = default;
};

class entry_spot_t : public spot_t
{
  public:
    ~entry_spot_t () override = default;
};

struct spot_actor_change_result_t
{
    explicit spot_actor_change_result_t (spot_actor_change_kind_t kind = spot_actor_change_kind_t::join_spot) :
        kind (kind)
    {
        switch (kind) {
            case spot_actor_change_kind_t::join_spot:
            case spot_actor_change_kind_t::join_entry_spot:
            case spot_actor_change_kind_t::leave_spot:
                break;
            default:
                throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                             "spot actor change kind must be a membership change");
        }
    }

    spot_actor_change_kind_t kind;
};

struct spot_actor_message_metadata_t
{
    std::optional<std::string_view> find (std::string_view key) const
    {
        const auto iterator = values.find (std::string (key));
        if (iterator == values.end ()) {
            return std::nullopt;
        }
        return iterator->second;
    }

    bool contains (std::string_view key) const { return values.find (std::string (key)) != values.end (); }

    bool empty () const noexcept { return values.empty (); }

    std::map<std::string, std::string> values;
};

class message_metadata_policy_t
{
  public:
    message_metadata_policy_t &add_forwarded_metadata_key (std::string key)
    {
        if (key.empty () || is_blank (key)) {
            throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                         "metadata key must not be empty");
        }
        _forwarded_keys.insert (std::move (key));
        return *this;
    }

    bool can_forward (std::string_view key) const
    {
        return _forwarded_keys.find (std::string (key)) != _forwarded_keys.end ();
    }

    spot_actor_message_metadata_t project (const std::map<std::string, std::string> &metadata) const
    {
        spot_actor_message_metadata_t projected;
        for (const auto &[key, value] : metadata) {
            if (can_forward (key)) {
                projected.values.emplace (key, value);
            }
        }
        return projected;
    }

  private:
    static bool is_blank (const std::string &value)
    {
        return std::all_of (value.begin (), value.end (), [] (unsigned char ch) { return std::isspace (ch) != 0; });
    }

    std::set<std::string> _forwarded_keys;
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

template <typename T> struct spot_member_function_traits_t;

template <typename TResult, typename TSpot, typename... TArgs>
struct spot_member_function_traits_t<TResult (TSpot::*) (TArgs...)>
{
    using spot_type = TSpot;
    using result_type = TResult;
    using args_tuple = std::tuple<TArgs...>;
    static constexpr std::size_t arg_count = sizeof...(TArgs);

    template <std::size_t Index> using arg_t = std::tuple_element_t<Index, args_tuple>;
};

template <typename TResult, typename TSpot, typename... TArgs>
struct spot_member_function_traits_t<TResult (TSpot::*) (TArgs...) const>
    : spot_member_function_traits_t<TResult (TSpot::*) (TArgs...)>
{
};

template <auto Method> using spot_member_traits_t = spot_member_function_traits_t<decltype (Method)>;

template <typename T> using unqualified_spot_arg_t = std::remove_cvref_t<T>;

template <typename T> struct spot_member_reply_payload_t
{
    using type = std::remove_cvref_t<T>;
};

template <typename T> struct spot_member_reply_payload_t<task_t<T>>
{
    using type = T;
};

template <typename TResult>
task_t<zlink::message_t> complete_spot_member_call (TResult &&result, serializer_registry_t &serializers)
{
    using result_type = std::remove_cvref_t<TResult>;
    if constexpr (is_task_v<result_type>) {
        using value_type = typename task_value_type_t<result_type>::type;
        if constexpr (std::is_void_v<value_type>) {
            co_await result;
            co_return result_t<zlink::message_t>::success (zlink::message_t{});
        } else {
            auto value = co_await result;
            co_return result_t<zlink::message_t>::success (serializers.get<value_type> ().serialize (value));
        }
    } else if constexpr (std::is_void_v<result_type>) {
        co_return result_t<zlink::message_t>::success (zlink::message_t{});
    } else {
        co_return (co_await serialize_handler_result (std::forward<TResult> (result), serializers));
    }
}

template <typename TCall> task_t<zlink::message_t> invoke_spot_member (TCall &&call, serializer_registry_t &serializers)
{
    using result_type = std::invoke_result_t<TCall>;
    try {
        if constexpr (std::is_void_v<result_type>) {
            call ();
            co_return result_t<zlink::message_t>::success (zlink::message_t{});
        } else {
            co_return (co_await complete_spot_member_call (call (), serializers));
        }
    }
    catch (...) {
        co_return current_exception_to_message_result ("spot handler threw an exception");
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

struct accepted_spot_route_channel_t
{
    std::string channel_name;
    std::vector<std::string> manual_connections;
};

struct attached_channel_client_t
{
    std::string channel_name;
    std::vector<std::string> manual_connections;
};

struct attached_publisher_t
{
    std::string channel_name;
    std::vector<std::string> manual_connections;
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
    std::vector<std::string> router_manual_connections;
    std::vector<std::string> pub_sub_manual_connections;
    bool actor_gateway_enabled = false;
    std::optional<std::string> discovery_channel_name;
    std::vector<std::string> attached_channel_clients;
    std::vector<std::string> attached_publishers;
    std::vector<attached_channel_client_t> attached_channel_client_details;
    std::vector<attached_publisher_t> attached_publisher_details;
    std::vector<std::string> spot_names;
    std::optional<std::string> entry_spot_name;
    bool registry_spot_remote_addresses_enabled = false;
    std::optional<std::string> registry_spot_route_channel;
    std::vector<accepted_spot_route_channel_t> accepted_route_channels;
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

    template <typename TEvent> send_call_t publish (std::string topic, TEvent event)
    {
        (void) event;
        return publish_erased (std::move (topic));
    }

    template <typename TReply, typename TRequest>
    request_call_t<TReply> request_to (node_rid_t node_rid, spot_rid_t spot_rid, TRequest request)
    {
        (void) request;
        return request_to_erased (std::move (node_rid), std::move (spot_rid)).template as<TReply> ();
    }

    template <typename TMessage> send_call_t send_to (node_rid_t node_rid, spot_rid_t spot_rid, TMessage message)
    {
        (void) message;
        return send_to_erased (std::move (node_rid), std::move (spot_rid));
    }

    template <typename TPayload> spot_context_t &register_packet (std::string packet_name)
    {
        return register_packet_erased (std::move (packet_name), std::type_index (typeid (TPayload)));
    }

    std::vector<spot_packet_descriptor_t> packet_registry () const;

    template <typename THandler>
    timer_t add_timer (std::string name, std::chrono::milliseconds period, timer_options_t options = {})
    {
        return add_timer_erased (std::move (name), period, std::move (options), std::type_index (typeid (THandler)));
    }

  private:
    friend class spot_node_builder_t;
    friend class detail::spot_node_runtime_t;
    friend class detail::timer_runtime_t;

    class erased_request_call_t
    {
      public:
        explicit erased_request_call_t (framework_exception_t error);

        template <typename TReply> request_call_t<TReply> as () const
        {
            return request_call_t<TReply> (
              result_t<TReply>::failure (_error.kind (), _error.what (), _error.is_retriable ()));
        }

      private:
        framework_exception_t _error;
    };

    explicit spot_context_t (std::shared_ptr<detail::spot_context_state_t> state);

    send_call_t publish_erased (std::string topic);
    send_call_t send_to_erased (node_rid_t node_rid, spot_rid_t spot_rid);
    erased_request_call_t request_to_erased (node_rid_t node_rid, spot_rid_t spot_rid);
    spot_context_t &register_packet_erased (std::string packet_name, std::type_index payload_type);
    timer_t add_timer_erased (std::string name,
                              std::chrono::milliseconds period,
                              timer_options_t options,
                              std::type_index handler_type);

    std::shared_ptr<detail::spot_context_state_t> _state;
};

class spot_handler_registry_t
{
  public:
    using invoker_t = std::function<task_t<zlink::message_t> (void *,
                                                              void *,
                                                              service_provider_t &,
                                                              serializer_registry_t &,
                                                              const zlink::message_t &,
                                                              const spot_actor_message_metadata_t &,
                                                              const spot_actor_change_result_t *)>;

    spot_handler_registry_t ();
    ~spot_handler_registry_t ();

    spot_handler_registry_t (spot_handler_registry_t &&) noexcept;
    spot_handler_registry_t &operator= (spot_handler_registry_t &&) noexcept;
    spot_handler_registry_t (const spot_handler_registry_t &) = default;
    spot_handler_registry_t &operator= (const spot_handler_registry_t &) = default;

    template <auto Method>
    spot_handler_registry_t &
    add_handler (std::string packet_name = detail::message_name<
                   detail::unqualified_spot_arg_t<typename detail::spot_member_traits_t<Method>::template arg_t<0>>> ())
    {
        using traits = detail::spot_member_traits_t<Method>;
        static_assert (traits::arg_count == 1, "SPOT packet member must accept exactly one payload argument");
        using spot_type = typename traits::spot_type;
        using message_type = detail::unqualified_spot_arg_t<typename traits::template arg_t<0>>;
        return add_handler_erased (
          spot_handler_kind_t::packet, std::move (packet_name), {}, std::type_index (typeid (spot_type)),
          std::type_index (typeid (message_type)), std::type_index (typeid (void)), std::type_index (typeid (void)),
          [] (void *spot, void *, service_provider_t &, serializer_registry_t &serializers,
              const zlink::message_t &message, const spot_actor_message_metadata_t &,
              const spot_actor_change_result_t *) {
              auto &typed_spot = *static_cast<spot_type *> (spot);
              auto payload = serializers.get<message_type> ().deserialize (message);
              return detail::invoke_spot_member ([&] { return (typed_spot.*Method) (payload); }, serializers);
          });
    }

    template <auto Method> spot_handler_registry_t &add_subscribe (std::string topic)
    {
        using traits = detail::spot_member_traits_t<Method>;
        static_assert (traits::arg_count == 1, "SPOT subscription member must accept exactly one event argument");
        using spot_type = typename traits::spot_type;
        using event_type = detail::unqualified_spot_arg_t<typename traits::template arg_t<0>>;
        return add_handler_erased (
          spot_handler_kind_t::subscription, detail::message_name<event_type> (), std::move (topic),
          std::type_index (typeid (spot_type)), std::type_index (typeid (event_type)), std::type_index (typeid (void)),
          std::type_index (typeid (void)),
          [] (void *spot, void *, service_provider_t &, serializer_registry_t &serializers,
              const zlink::message_t &message, const spot_actor_message_metadata_t &,
              const spot_actor_change_result_t *) {
              auto &typed_spot = *static_cast<spot_type *> (spot);
              auto payload = serializers.get<event_type> ().deserialize (message);
              return detail::invoke_spot_member ([&] { return (typed_spot.*Method) (payload); }, serializers);
          });
    }

    template <auto Method>
    spot_handler_registry_t &add_actor_join (
      std::string packet_name = detail::message_name<
        detail::unqualified_spot_arg_t<typename detail::spot_member_traits_t<Method>::template arg_t<1>>> ())
    {
        using traits = detail::spot_member_traits_t<Method>;
        static_assert (traits::arg_count == 2, "SPOT actor join member must accept actor and request arguments");
        using spot_type = typename traits::spot_type;
        using actor_type = detail::unqualified_spot_arg_t<typename traits::template arg_t<0>>;
        using request_type = detail::unqualified_spot_arg_t<typename traits::template arg_t<1>>;
        using reply_type = typename detail::spot_member_reply_payload_t<typename traits::result_type>::type;
        return add_handler_erased (spot_handler_kind_t::actor_join, std::move (packet_name), {},
                                   std::type_index (typeid (spot_type)), std::type_index (typeid (request_type)),
                                   std::type_index (typeid (actor_type)), std::type_index (typeid (reply_type)),
                                   [] (void *spot, void *actor, service_provider_t &,
                                       serializer_registry_t &serializers, const zlink::message_t &message,
                                       const spot_actor_message_metadata_t &, const spot_actor_change_result_t *) {
                                       auto &typed_spot = *static_cast<spot_type *> (spot);
                                       auto &typed_actor = *static_cast<actor_type *> (actor);
                                       auto request = serializers.get<request_type> ().deserialize (message);
                                       return detail::invoke_spot_member (
                                         [&] { return (typed_spot.*Method) (typed_actor, request); }, serializers);
                                   });
    }

    template <auto Method>
    spot_handler_registry_t &add_actor_packet (
      std::string packet_name = detail::message_name<
        detail::unqualified_spot_arg_t<typename detail::spot_member_traits_t<Method>::template arg_t<2>>> ())
    {
        using traits = detail::spot_member_traits_t<Method>;
        static_assert (traits::arg_count == 3,
                       "SPOT actor packet member must accept actor, context, and payload arguments");
        using spot_type = typename traits::spot_type;
        using actor_type = detail::unqualified_spot_arg_t<typename traits::template arg_t<0>>;
        using context_type = detail::unqualified_spot_arg_t<typename traits::template arg_t<1>>;
        using message_type = detail::unqualified_spot_arg_t<typename traits::template arg_t<2>>;
        static_assert (std::is_same_v<context_type, spot_actor_send_context_t>
                         || std::is_same_v<context_type, spot_actor_request_context_t>,
                       "SPOT actor packet context must be spot_actor_send_context_t or "
                       "spot_actor_request_context_t");
        auto registered_packet_name = packet_name;
        return add_handler_erased (
          spot_handler_kind_t::actor_packet, std::move (packet_name), {}, std::type_index (typeid (spot_type)),
          std::type_index (typeid (message_type)), std::type_index (typeid (actor_type)),
          std::type_index (typeid (void)),
          [registered_packet_name = std::move (registered_packet_name)] (
            void *spot, void *actor, service_provider_t &, serializer_registry_t &serializers,
            const zlink::message_t &message, const spot_actor_message_metadata_t &metadata,
            const spot_actor_change_result_t *) {
              auto &typed_spot = *static_cast<spot_type *> (spot);
              auto &typed_actor = *static_cast<actor_type *> (actor);
              auto payload = serializers.get<message_type> ().deserialize (message);
              spot_actor_send_context_t send_context{registered_packet_name, "application/json", metadata};
              spot_actor_request_context_t request_context{registered_packet_name, "application/json", metadata, {}};
              if constexpr (std::is_same_v<context_type, spot_actor_request_context_t>) {
                  return detail::invoke_spot_member (
                    [&] { return (typed_spot.*Method) (typed_actor, request_context, payload); }, serializers);
              } else {
                  return detail::invoke_spot_member (
                    [&] { return (typed_spot.*Method) (typed_actor, send_context, payload); }, serializers);
              }
          });
    }

    template <auto Method> spot_handler_registry_t &add_post_actor_joined ()
    {
        return add_actor_lifecycle_handler<Method> (spot_handler_kind_t::post_actor_joined);
    }

    template <auto Method> spot_handler_registry_t &add_actor_left ()
    {
        return add_actor_lifecycle_handler<Method> (spot_handler_kind_t::actor_left);
    }

    template <auto Method> spot_handler_registry_t &add_actor_disconnected ()
    {
        return add_actor_lifecycle_handler<Method> (spot_handler_kind_t::actor_disconnected);
    }

    std::vector<spot_handler_descriptor_t> descriptors () const;

    template <typename TSpot>
    result_t<zlink::message_t> invoke_packet (std::string_view packet_name,
                                              TSpot &spot,
                                              service_provider_t &services,
                                              serializer_registry_t &serializers,
                                              const zlink::message_t &message) const
    {
        return invoke_erased (spot_handler_kind_t::packet, packet_name, {}, std::type_index (typeid (void)), &spot,
                              nullptr, services, serializers, message)
          .result ();
    }

    template <typename TSpot, typename TActor>
    result_t<zlink::message_t> invoke_actor_join (std::string_view packet_name,
                                                  TSpot &spot,
                                                  TActor &actor,
                                                  service_provider_t &services,
                                                  serializer_registry_t &serializers,
                                                  const zlink::message_t &message) const
    {
        return invoke_erased (spot_handler_kind_t::actor_join, packet_name, {}, std::type_index (typeid (TActor)),
                              &spot, &actor, services, serializers, message)
          .result ();
    }

    template <typename TSpot, typename TActor>
    result_t<zlink::message_t> invoke_actor_packet (std::string_view packet_name,
                                                    TSpot &spot,
                                                    TActor &actor,
                                                    service_provider_t &services,
                                                    serializer_registry_t &serializers,
                                                    const zlink::message_t &message) const
    {
        return invoke_erased (spot_handler_kind_t::actor_packet, packet_name, {}, std::type_index (typeid (TActor)),
                              &spot, &actor, services, serializers, message)
          .result ();
    }

    template <typename TSpot, typename TActor>
    result_t<zlink::message_t> invoke_actor_packet (std::string_view packet_name,
                                                    TSpot &spot,
                                                    TActor &actor,
                                                    service_provider_t &services,
                                                    serializer_registry_t &serializers,
                                                    const zlink::message_t &message,
                                                    spot_actor_message_metadata_t metadata) const
    {
        return invoke_erased (spot_handler_kind_t::actor_packet, packet_name, {}, std::type_index (typeid (TActor)),
                              &spot, &actor, services, serializers, message, nullptr, std::move (metadata))
          .result ();
    }

    template <typename TSpot, typename TActor>
    result_t<zlink::message_t> invoke_post_actor_joined (
      TSpot &spot,
      TActor &actor,
      service_provider_t &services,
      serializer_registry_t &serializers,
      spot_actor_change_result_t result = spot_actor_change_result_t (spot_actor_change_kind_t::join_spot)) const
    {
        return invoke_actor_lifecycle<TSpot, TActor> (spot_handler_kind_t::post_actor_joined, spot, actor, services,
                                                      serializers, result)
          .result ();
    }

    template <typename TSpot, typename TActor>
    result_t<zlink::message_t> invoke_actor_left (
      TSpot &spot,
      TActor &actor,
      service_provider_t &services,
      serializer_registry_t &serializers,
      spot_actor_change_result_t result = spot_actor_change_result_t (spot_actor_change_kind_t::leave_spot)) const
    {
        return invoke_actor_lifecycle<TSpot, TActor> (spot_handler_kind_t::actor_left, spot, actor, services,
                                                      serializers, result)
          .result ();
    }

    template <typename TSpot, typename TActor>
    result_t<zlink::message_t> invoke_actor_disconnected (TSpot &spot,
                                                          TActor &actor,
                                                          service_provider_t &services,
                                                          serializer_registry_t &serializers) const
    {
        return invoke_actor_lifecycle<TSpot, TActor> (spot_handler_kind_t::actor_disconnected, spot, actor, services,
                                                      serializers)
          .result ();
    }

  private:
    friend class spot_context_t;
    explicit spot_handler_registry_t (std::shared_ptr<detail::spot_context_state_t> state);

    template <auto Method> spot_handler_registry_t &add_actor_lifecycle_handler (spot_handler_kind_t kind)
    {
        using traits = detail::spot_member_traits_t<Method>;
        static_assert (traits::arg_count == 1 || traits::arg_count == 2,
                       "SPOT actor lifecycle member must accept actor or actor and change result");
        using spot_type = typename traits::spot_type;
        using actor_type = detail::unqualified_spot_arg_t<typename traits::template arg_t<0>>;
        if constexpr (traits::arg_count == 2) {
            using result_type = detail::unqualified_spot_arg_t<typename traits::template arg_t<1>>;
            static_assert (std::is_same_v<result_type, spot_actor_change_result_t>,
                           "SPOT actor lifecycle change argument must be spot_actor_change_result_t");
        }
        return add_handler_erased (
          kind, {}, {}, std::type_index (typeid (spot_type)), std::type_index (typeid (void)),
          std::type_index (typeid (actor_type)), std::type_index (typeid (void)),
          [] (void *spot, void *actor, service_provider_t &, serializer_registry_t &serializers,
              const zlink::message_t &, const spot_actor_message_metadata_t &,
              const spot_actor_change_result_t *result) {
              auto &typed_spot = *static_cast<spot_type *> (spot);
              auto &typed_actor = *static_cast<actor_type *> (actor);
              if constexpr (traits::arg_count == 1) {
                  return detail::invoke_spot_member ([&] { return (typed_spot.*Method) (typed_actor); }, serializers);
              } else {
                  if (result == nullptr) {
                      return task_t<zlink::message_t> (
                        result_t<zlink::message_t>::failure (framework_error_kind_t::request_protocol_error,
                                                             "spot actor lifecycle change result is required"));
                  }
                  return detail::invoke_spot_member ([&] { return (typed_spot.*Method) (typed_actor, *result); },
                                                     serializers);
              }
          });
    }

    spot_handler_registry_t &add_handler_erased (spot_handler_kind_t kind,
                                                 std::string packet_name,
                                                 std::string topic,
                                                 std::type_index handler_type,
                                                 std::type_index payload_type,
                                                 std::type_index actor_type,
                                                 std::type_index reply_type,
                                                 invoker_t invoker);

    task_t<zlink::message_t> invoke_erased (spot_handler_kind_t kind,
                                            std::string_view packet_name,
                                            std::string_view topic,
                                            std::type_index actor_type,
                                            void *spot,
                                            void *actor,
                                            service_provider_t &services,
                                            serializer_registry_t &serializers,
                                            const zlink::message_t &message,
                                            const spot_actor_change_result_t *change_result = nullptr,
                                            spot_actor_message_metadata_t metadata = {}) const;

    template <typename TSpot, typename TActor>
    task_t<zlink::message_t> invoke_actor_lifecycle (spot_handler_kind_t kind,
                                                     TSpot &spot,
                                                     TActor &actor,
                                                     service_provider_t &services,
                                                     serializer_registry_t &serializers,
                                                     const spot_actor_change_result_t &change_result) const
    {
        return invoke_erased (kind, {}, {}, std::type_index (typeid (TActor)), &spot, &actor, services, serializers,
                              zlink::message_t{}, &change_result);
    }

    template <typename TSpot, typename TActor>
    task_t<zlink::message_t> invoke_actor_lifecycle (spot_handler_kind_t kind,
                                                     TSpot &spot,
                                                     TActor &actor,
                                                     service_provider_t &services,
                                                     serializer_registry_t &serializers) const
    {
        return invoke_erased (kind, {}, {}, std::type_index (typeid (TActor)), &spot, &actor, services, serializers,
                              zlink::message_t{}, nullptr);
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
    spot_node_builder_t &enable_router (std::string endpoint, zlink::routing_id_t routing_id);
    spot_node_builder_t &connect_router (std::string endpoint);
    spot_node_builder_t &enable_pub_sub (std::string endpoint);
    spot_node_builder_t &enable_pub_sub (std::string endpoint, zlink::routing_id_t routing_id);
    spot_node_builder_t &connect_pub_sub (std::string endpoint);
    spot_node_builder_t &enable_actor_gateway ();
    spot_node_builder_t &use_discovery (std::string channel_name);
    spot_node_builder_t &use_registry_spot_remote_addresses ();
    spot_node_builder_t &use_registry_spot_remote_addresses (std::string route_channel_name);
    spot_node_builder_t &accept_routes_from_channel (std::string route_channel_name,
                                                     std::vector<std::string> manual_connections = {});
    spot_node_builder_t &attach_channel_client (std::string channel_name,
                                                std::vector<std::string> manual_connections = {});
    spot_node_builder_t &attach_publisher (std::string channel_name, std::vector<std::string> manual_connections = {});

    template <typename TSpot> spot_node_builder_t &add_spot (std::string spot_name)
    {
        static_assert (std::is_base_of_v<spot_t, TSpot>, "SPOT type must derive from zlink::framework::spot_t");
        static_assert (!std::is_base_of_v<entry_spot_t, TSpot>,
                       "Entry SPOT type must be registered with add_entry_spot<TEntrySpot>()");
        return add_spot_factory (std::move (spot_name), std::type_index (typeid (TSpot)), false);
    }

    template <typename TEntrySpot> spot_node_builder_t &add_entry_spot ()
    {
        static_assert (std::is_base_of_v<entry_spot_t, TEntrySpot>,
                       "Entry SPOT type must derive from zlink::framework::entry_spot_t");
        return add_spot_factory (std::string ("entry"), std::type_index (typeid (TEntrySpot)), true);
    }

    template <typename TActorFactory> spot_node_builder_t &add_actor_factory (std::string actor_type)
    {
        return add_actor_factory_erased (std::move (actor_type), std::type_index (typeid (TActorFactory)));
    }

    spot_node_builder_t &add_spot_resolver (std::string name,
                                            std::function<std::optional<spot_route_t> (spot_rid_t)> resolver);

    spot_node_snapshot_t snapshot () const;
    spot_context_t create_spot (std::string spot_name);
    std::optional<std::string> spot_name_for (spot_rid_t spot_rid) const;
    std::optional<spot_route_t> resolve_spot (spot_rid_t spot_rid) const;

  private:
    friend class zlink_builder_t;
    friend class detail::spot_node_runtime_t;
    explicit spot_node_builder_t (std::shared_ptr<detail::spot_node_builder_state_t> state);

    spot_node_builder_t &add_spot_factory (std::string spot_name, std::type_index spot_type, bool entry_spot);
    spot_node_builder_t &add_actor_factory_erased (std::string actor_type, std::type_index factory_type);

    std::shared_ptr<detail::spot_node_builder_state_t> _state;
};

} // namespace zlink::framework
