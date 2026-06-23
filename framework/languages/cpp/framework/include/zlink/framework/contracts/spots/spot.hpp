/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/Contracts/Messaging/message.hpp>
#include <zlink/framework/contracts/channels/call.hpp>
#include <zlink/framework/contracts/channels/channel.hpp>
#include <zlink/framework/contracts/codecs/serializer.hpp>
#include <zlink/framework/contracts/configuration/services.hpp>
#include <zlink/framework/contracts/detail/handler_invocation.hpp>
#include <zlink/framework/contracts/detail/message_name.hpp>
#include <zlink/framework/contracts/messaging/message.hpp>
#include <zlink/framework/contracts/dispatch/task.hpp>
#include <zlink/framework/contracts/errors/result.hpp>
#include <zlink/framework/contracts/timers/timer.hpp>
#include <zlink/framework/contracts/workers/worker.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <typeindex>
#include <type_traits>
#include <utility>
#include <vector>

namespace zlink::framework
{

class actor_context_t;
class actor_ref_t;
class spot_node_manager_t;

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

struct spot_actor_join_response_t
{
    bool accepted = false;
    std::optional<message_t> reply;

    static spot_actor_join_response_t accept (std::optional<message_t> reply = std::nullopt)
    {
        return spot_actor_join_response_t{true, std::move (reply)};
    }

    static spot_actor_join_response_t accept_raw (zlink::message_t reply)
    {
        return accept (message_t::from_encoded (std::move (reply)));
    }

    template <typename TReply>
      requires (!std::is_same_v<std::remove_cvref_t<TReply>, message_t>
                && !std::is_same_v<std::remove_cvref_t<TReply>, zlink::message_t>)
    static spot_actor_join_response_t accept (TReply reply)
    {
        return accept (message_t::from (std::move (reply)));
    }

    static spot_actor_join_response_t reject (std::optional<message_t> reply = std::nullopt)
    {
        return spot_actor_join_response_t{false, std::move (reply)};
    }

    static spot_actor_join_response_t reject_raw (zlink::message_t reply)
    {
        return reject (message_t::from_encoded (std::move (reply)));
    }

    template <typename TReply>
      requires (!std::is_same_v<std::remove_cvref_t<TReply>, message_t>
                && !std::is_same_v<std::remove_cvref_t<TReply>, zlink::message_t>)
    static spot_actor_join_response_t reject (TReply reply)
    {
        return reject (message_t::from (std::move (reply)));
    }
};

enum class spot_create_state_t
{
    existing,
    created,
    rejected
};

struct spot_create_response_t
{
    bool accepted = true;
    std::optional<message_t> reply;

    static spot_create_response_t accept (std::optional<message_t> reply = std::nullopt)
    {
        return spot_create_response_t{true, std::move (reply)};
    }

    static spot_create_response_t accept_raw (zlink::message_t reply)
    {
        return accept (message_t::from_encoded (std::move (reply)));
    }

    template <typename TReply>
      requires (!std::is_same_v<std::remove_cvref_t<TReply>, message_t>
                && !std::is_same_v<std::remove_cvref_t<TReply>, zlink::message_t>)
    static spot_create_response_t accept (TReply reply)
    {
        return accept (message_t::from (std::move (reply)));
    }

    static spot_create_response_t reject (std::optional<message_t> reply = std::nullopt)
    {
        return spot_create_response_t{false, std::move (reply)};
    }

    static spot_create_response_t reject_raw (zlink::message_t reply)
    {
        return reject (message_t::from_encoded (std::move (reply)));
    }

    template <typename TReply>
      requires (!std::is_same_v<std::remove_cvref_t<TReply>, message_t>
                && !std::is_same_v<std::remove_cvref_t<TReply>, zlink::message_t>)
    static spot_create_response_t reject (TReply reply)
    {
        return reject (message_t::from (std::move (reply)));
    }
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

    bool contains (std::string_view key) const
    {
        return values.find (std::string (key)) != values.end ();
    }

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
        return std::all_of (value.begin (), value.end (),
                            [] (unsigned char ch) { return std::isspace (ch) != 0; });
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

struct spot_actor_admission_callbacks_t
{
    std::function<spot_actor_join_response_t (void *, void *, const zlink::message_t &,
                                              serializer_registry_t &)>
      join;
    std::function<void (void *, void *)> on_actor_joined;
    std::function<void (void *, void *)> onCreateActor;
    std::function<void (void *, void *)> onLeaveActor;
    std::function<void (void *, void *)> onDisconnectActor;
    bool entry_spot = false;
};

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

template <auto Method>
using spot_member_traits_t = spot_member_function_traits_t<decltype (Method)>;

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
task_t<zlink::message_t> complete_spot_member_call (TResult &&result,
                                                    serializer_registry_t &serializers)
{
    using result_type = std::remove_cvref_t<TResult>;
    if constexpr (is_task_v<result_type>) {
        using value_type = typename task_value_type_t<result_type>::type;
        if constexpr (std::is_void_v<value_type>) {
            co_await result;
            co_return result_t<zlink::message_t>::success (zlink::message_t{});
        } else {
            auto value = co_await result;
            co_return result_t<zlink::message_t>::success (
              serializers.get<value_type> ().serialize (value));
        }
    } else if constexpr (std::is_void_v<result_type>) {
        co_return result_t<zlink::message_t>::success (zlink::message_t{});
    } else {
        co_return (co_await serialize_handler_result (std::forward<TResult> (result), serializers));
    }
}

template <typename TCall>
task_t<zlink::message_t> invoke_spot_member (TCall &&call, serializer_registry_t &serializers)
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
    actor_packet
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

struct spot_info_t
{
    spot_rid_t spot_rid;
    std::string spot_name;
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
    std::vector<std::string> attached_publishers;
    std::vector<attached_publisher_t> attached_publisher_details;
    std::vector<std::string> spot_names;
    std::optional<std::string> entry_spot_name;
    bool registry_spot_remote_addresses_enabled = false;
    std::optional<std::string> registry_spot_route_channel;
    std::vector<accepted_spot_route_channel_t> accepted_route_channels;
    std::vector<std::string> actor_types;
};

class spot_handler_registry_t;
struct spot_create_result_t;

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
    spot_node_manager_t manager () const;
    channel_client_t outbound () const;
    task_t<bool> close ();

    template <typename TEvent> send_call_t publish (std::string topic, TEvent event)
    {
        auto *serializers = serializer_registry ();
        if (serializers == nullptr) {
            return send_call_t (
              result_t<void>::failure (framework_error_kind_t::request_protocol_error,
                                       "spot publish requires a serializer registry"));
        }
        try {
            auto payload = serializers->get<TEvent> ().serialize (event);
            return publish_erased (std::move (topic), detail::message_name<TEvent> (),
                                   std::move (payload));
        }
        catch (const framework_exception_t &error) {
            return send_call_t (
              result_t<void>::failure (error.kind (), error.what (), error.is_retriable ()));
        }
    }

    template <typename TReply, typename TRequest>
    request_call_t<TReply> request_to (node_rid_t node_rid, spot_rid_t spot_rid, TRequest request)
    {
        auto *serializers = serializer_registry ();
        if (serializers == nullptr) {
            return request_call_t<TReply> (
              result_t<TReply>::failure (framework_error_kind_t::request_protocol_error,
                                         "spot request_to requires a serializer registry"));
        }
        try {
            auto payload = serializers->get<TRequest> ().serialize (request);
            return request_to_erased (std::move (node_rid), std::move (spot_rid),
                                      detail::message_name<TRequest> (), std::move (payload))
              .template as<TReply> ();
        }
        catch (const framework_exception_t &error) {
            return request_call_t<TReply> (
              result_t<TReply>::failure (error.kind (), error.what (), error.is_retriable ()));
        }
    }

    template <typename TMessage>
    send_call_t send_to (node_rid_t node_rid, spot_rid_t spot_rid, TMessage message)
    {
        auto *serializers = serializer_registry ();
        if (serializers == nullptr) {
            return send_call_t (
              result_t<void>::failure (framework_error_kind_t::request_protocol_error,
                                       "spot send_to requires a serializer registry"));
        }
        try {
            auto payload = serializers->get<TMessage> ().serialize (message);
            return send_to_erased (std::move (node_rid), std::move (spot_rid),
                                   detail::message_name<TMessage> (), std::move (payload));
        }
        catch (const framework_exception_t &error) {
            return send_call_t (
              result_t<void>::failure (error.kind (), error.what (), error.is_retriable ()));
        }
    }

    template <typename TPayload> spot_context_t &register_packet (std::string packet_name)
    {
        return register_packet_erased (std::move (packet_name),
                                       std::type_index (typeid (TPayload)));
    }

    template <typename TWork> auto run_worker (TWork work)
    {
        using result_type = std::invoke_result_t<TWork>;
        auto scheduler = _worker_scheduler;
        return worker_call_t<result_type> (
          [scheduler, work = std::move (work)] (
            std::optional<std::chrono::milliseconds> timeout) mutable -> task_t<result_type> {
              (void) timeout;
              if (!scheduler) {
                  return task_t<result_type> (result_t<result_type>::failure (
                    framework_error_kind_t::request_failed, "worker runtime is not configured"));
              }

              detail::task_completion_source_t<result_type> completion;
              auto task = completion.task ();
              auto shared_work = std::make_shared<TWork> (std::move (work));
              auto completed = std::make_shared<std::atomic_bool> (false);
              if (timeout && *timeout > std::chrono::milliseconds::zero ()) {
                  auto timeout_scheduler = scheduler;
                  auto timeout_completion = completion;
                  auto timeout_completed = completed;
                  std::thread ([timeout_scheduler, timeout_completion, timeout_completed,
                                timeout = *timeout] () mutable {
                      std::this_thread::sleep_for (timeout);
                      if (!timeout_completed->exchange (true)) {
                          timeout_scheduler->post_owner ([timeout_completion] () mutable {
                              timeout_completion.complete (result_t<result_type>::failure (
                                framework_error_kind_t::worker_timeout, "worker task timed out"));
                          });
                      }
                  }).detach ();
              }
              const auto scheduled = scheduler->try_schedule ([scheduler, shared_work, completion,
                                                               completed] () mutable {
                  auto result = detail::run_worker_body<result_type> (*shared_work);
                  if (!completed->exchange (true)) {
                      scheduler->post_owner ([completion, result = std::move (result)] () mutable {
                          completion.complete (std::move (result));
                      });
                  }
              });
              if (!scheduled) {
                  completed->store (true);
                  scheduler->post_owner ([completion] () mutable {
                      completion.complete (result_t<result_type>::failure (
                        framework_error_kind_t::worker_queue_full, "worker queue is full"));
                  });
              }
              return task;
          });
    }

    std::vector<spot_packet_descriptor_t> packet_registry () const;

    template <typename TActor>
    task_t<actor_ref_t> leaveActor (const actor_ref_t &actor_ref, TActor &actor)
    {
        return leaveActor_erased (
          actor_ref, std::type_index (typeid (TActor)), &actor,
          [] (void *actor_instance, const actor_ref_t &committed) {
              auto &typed_actor = *static_cast<TActor *> (actor_instance);
              if constexpr (requires { typed_actor.set_actor_ref (committed); }) {
                  typed_actor.set_actor_ref (committed);
              }
          });
    }

    template <typename THandler>
    timer_t
    add_timer (std::string name, std::chrono::milliseconds period, timer_options_t options = {})
    {
        return add_timer_erased (std::move (name), period, std::move (options),
                                 std::type_index (typeid (THandler)));
    }

  protected:
    friend class spot_node_builder_t;
    friend class entry_spot_context_t;
    friend class detail::spot_node_runtime_t;
    friend class detail::timer_runtime_t;

    class erased_request_call_t
    {
      public:
        explicit erased_request_call_t (framework_exception_t error);
        erased_request_call_t (std::string packet_name,
                               serializer_registry_t *serializers,
                               std::function<task_t<zlink::message_t> (
                                 const std::string &,
                                 std::chrono::milliseconds,
                                 const request_call_t<zlink::message_t>::metadata_map_t &)> submit);

        template <typename TReply> request_call_t<TReply> as () const
        {
            if (_error) {
                return request_call_t<TReply> (result_t<TReply>::failure (
                  _error->kind (), _error->what (), _error->is_retriable ()));
            }
            auto serializers = _serializers;
            auto submit = _submit;
            return request_call_t<TReply> (
              _packet_name,
              [serializers,
               submit] (const std::string &packet_name, std::chrono::milliseconds timeout,
                        const request_call_t<TReply>::metadata_map_t &metadata) -> task_t<TReply> {
                  if (!submit) {
                      co_return result_t<TReply>::failure (
                        framework_error_kind_t::request_protocol_error,
                        "spot request is not bound to a route channel");
                  }
                  if (serializers == nullptr) {
                      co_return result_t<TReply>::failure (
                        framework_error_kind_t::request_protocol_error,
                        "spot request has no serializer registry");
                  }
                  try {
                      auto reply = co_await submit (packet_name, timeout, metadata);
                      co_return serializers->get<TReply> ().deserialize (reply);
                  }
                  catch (const framework_exception_t &error) {
                      co_return result_t<TReply>::failure (error.kind (), error.what (),
                                                           error.is_retriable ());
                  }
              });
        }

      private:
        std::optional<framework_exception_t> _error;
        std::string _packet_name;
        serializer_registry_t *_serializers = nullptr;
        std::function<task_t<zlink::message_t> (
          const std::string &,
          std::chrono::milliseconds,
          const request_call_t<zlink::message_t>::metadata_map_t &)>
          _submit;
    };

    explicit spot_context_t (std::shared_ptr<detail::spot_context_state_t> state);

    send_call_t
    publish_erased (std::string topic, std::string packet_name, zlink::message_t payload);
    serializer_registry_t *serializer_registry () const noexcept;
    send_call_t send_to_erased (node_rid_t node_rid,
                                spot_rid_t spot_rid,
                                std::string packet_name,
                                zlink::message_t payload);
    erased_request_call_t request_to_erased (node_rid_t node_rid,
                                             spot_rid_t spot_rid,
                                             std::string packet_name,
                                             zlink::message_t payload);
    spot_context_t &register_packet_erased (std::string packet_name, std::type_index payload_type);
    task_t<actor_ref_t>
    leaveActor_erased (const actor_ref_t &actor_ref,
                       std::type_index actor_type,
                       void *actor,
                       std::function<void (void *, const actor_ref_t &)> update_actor_ref);
    timer_t add_timer_erased (std::string name,
                              std::chrono::milliseconds period,
                              timer_options_t options,
                              std::type_index handler_type);
    task_t<bool> close_erased ();

    std::shared_ptr<detail::spot_context_state_t> _state;
    std::shared_ptr<detail::worker_scheduler_t> _worker_scheduler;
};

class entry_spot_context_t : public spot_context_t
{
  public:
    entry_spot_context_t ();
    ~entry_spot_context_t ();

    entry_spot_context_t (entry_spot_context_t &&) noexcept;
    entry_spot_context_t &operator= (entry_spot_context_t &&) noexcept;
    entry_spot_context_t (const entry_spot_context_t &) = default;
    entry_spot_context_t &operator= (const entry_spot_context_t &) = default;

    explicit entry_spot_context_t (const spot_context_t &context);

    template <typename TActor>
    task_t<void> destroyActor (const actor_ref_t &actor_ref, TActor &actor)
    {
        (void) actor;
        return destroyActor_erased (actor_ref);
    }

  private:
    friend class detail::spot_node_runtime_t;
    explicit entry_spot_context_t (std::shared_ptr<detail::spot_context_state_t> state);

    task_t<void> destroyActor_erased (const actor_ref_t &actor_ref);
};

struct spot_create_result_t
{
    spot_rid_t spot_rid;
    spot_create_state_t state = spot_create_state_t::created;
    std::optional<zlink::message_t> reply;
    spot_context_t context;
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
                                              const spot_actor_message_metadata_t &)>;

    spot_handler_registry_t ();
    ~spot_handler_registry_t ();

    spot_handler_registry_t (spot_handler_registry_t &&) noexcept;
    spot_handler_registry_t &operator= (spot_handler_registry_t &&) noexcept;
    spot_handler_registry_t (const spot_handler_registry_t &) = default;
    spot_handler_registry_t &operator= (const spot_handler_registry_t &) = default;

    template <auto Method>
    spot_handler_registry_t &
    add_handler (std::string packet_name = detail::message_name<detail::unqualified_spot_arg_t<
                   typename detail::spot_member_traits_t<Method>::template arg_t<0>>> ())
    {
        using traits = detail::spot_member_traits_t<Method>;
        static_assert (traits::arg_count == 1,
                       "SPOT packet member must accept exactly one payload argument");
        using spot_type = typename traits::spot_type;
        using message_type = detail::unqualified_spot_arg_t<typename traits::template arg_t<0>>;
        return add_handler_erased (
          spot_handler_kind_t::packet, std::move (packet_name), {},
          std::type_index (typeid (spot_type)), std::type_index (typeid (message_type)),
          std::type_index (typeid (void)), std::type_index (typeid (void)),
          [] (void *spot, void *, service_provider_t &, serializer_registry_t &serializers,
              const zlink::message_t &message, const spot_actor_message_metadata_t &) {
              auto &typed_spot = *static_cast<spot_type *> (spot);
              auto payload = serializers.get<message_type> ().deserialize (message);
              return detail::invoke_spot_member ([&] { return (typed_spot.*Method) (payload); },
                                                 serializers);
          });
    }

    template <auto Method> spot_handler_registry_t &add_subscribe (std::string topic)
    {
        using traits = detail::spot_member_traits_t<Method>;
        static_assert (traits::arg_count == 1,
                       "SPOT subscription member must accept exactly one event argument");
        using spot_type = typename traits::spot_type;
        using event_type = detail::unqualified_spot_arg_t<typename traits::template arg_t<0>>;
        return add_handler_erased (
          spot_handler_kind_t::subscription, detail::message_name<event_type> (), std::move (topic),
          std::type_index (typeid (spot_type)), std::type_index (typeid (event_type)),
          std::type_index (typeid (void)), std::type_index (typeid (void)),
          [] (void *spot, void *, service_provider_t &, serializer_registry_t &serializers,
              const zlink::message_t &message, const spot_actor_message_metadata_t &) {
              auto &typed_spot = *static_cast<spot_type *> (spot);
              auto payload = serializers.get<event_type> ().deserialize (message);
              return detail::invoke_spot_member ([&] { return (typed_spot.*Method) (payload); },
                                                 serializers);
          });
    }

    template <auto Method>
    spot_handler_registry_t &
    add_actor_packet (std::string packet_name = detail::message_name<detail::unqualified_spot_arg_t<
                        typename detail::spot_member_traits_t<Method>::template arg_t<2>>> ())
    {
        using traits = detail::spot_member_traits_t<Method>;
        static_assert (
          traits::arg_count == 3,
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
        auto &registry = add_handler_erased (
          spot_handler_kind_t::actor_packet, std::move (packet_name), {},
          std::type_index (typeid (spot_type)), std::type_index (typeid (message_type)),
          std::type_index (typeid (actor_type)), std::type_index (typeid (void)),
          [registered_packet_name = std::move (registered_packet_name)] (
            void *spot, void *actor, service_provider_t &, serializer_registry_t &serializers,
            const zlink::message_t &message, const spot_actor_message_metadata_t &metadata) {
              auto &typed_spot = *static_cast<spot_type *> (spot);
              auto &typed_actor = *static_cast<actor_type *> (actor);
              auto payload = serializers.get<message_type> ().deserialize (message);
              spot_actor_send_context_t send_context{registered_packet_name, "application/json",
                                                     metadata};
              spot_actor_request_context_t request_context{
                registered_packet_name, "application/json", metadata, {}};
              if constexpr (std::is_same_v<context_type, spot_actor_request_context_t>) {
                  return detail::invoke_spot_member (
                    [&] { return (typed_spot.*Method) (typed_actor, request_context, payload); },
                    serializers);
              } else {
                  return detail::invoke_spot_member (
                    [&] { return (typed_spot.*Method) (typed_actor, send_context, payload); },
                    serializers);
              }
          });
        registry.template register_actor_admission<spot_type, actor_type> ();
        return registry;
    }

    std::vector<spot_handler_descriptor_t> descriptors () const;

    template <typename TSpot>
    result_t<zlink::message_t> invoke_packet (std::string_view packet_name,
                                              TSpot &spot,
                                              service_provider_t &services,
                                              serializer_registry_t &serializers,
                                              const zlink::message_t &message) const
    {
        return invoke_erased (spot_handler_kind_t::packet, packet_name, {},
                              std::type_index (typeid (void)), &spot, nullptr, services,
                              serializers, message)
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
        return invoke_erased (spot_handler_kind_t::actor_packet, packet_name, {},
                              std::type_index (typeid (TActor)), &spot, &actor, services,
                              serializers, message)
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
        return invoke_erased (spot_handler_kind_t::actor_packet, packet_name, {},
                              std::type_index (typeid (TActor)), &spot, &actor, services,
                              serializers, message, std::move (metadata))
          .result ();
    }

  private:
    friend class spot_context_t;
    friend class detail::spot_node_runtime_t;
    explicit spot_handler_registry_t (std::shared_ptr<detail::spot_context_state_t> state);

    template <typename TSpot, typename TActor> void register_actor_admission ()
    {
        detail::spot_actor_admission_callbacks_t callbacks;
        callbacks.entry_spot = std::is_base_of_v<entry_spot_t, TSpot>;
        callbacks.join = [] (void *spot,
                              void *actor,
                              const zlink::message_t &request,
                              serializer_registry_t &serializers) {
            auto &typed_spot = *static_cast<TSpot *> (spot);
            auto &typed_actor = *static_cast<TActor *> (actor);
            if constexpr (requires { typed_spot.on_actor_join (typed_actor, message_t{}); }) {
                return typed_spot.on_actor_join (typed_actor,
                                                 message_t::from_encoded (request, &serializers));
            } else if constexpr (requires { typed_spot.on_actor_join (typed_actor, request); }) {
                return typed_spot.on_actor_join (typed_actor, request);
            } else if constexpr (std::is_base_of_v<entry_spot_t, TSpot>) {
                return spot_actor_join_response_t::accept ();
            } else {
                return spot_actor_join_response_t::reject ();
            }
        };
        callbacks.on_actor_joined = [] (void *spot, void *actor) {
            if constexpr (requires {
                              static_cast<TSpot *> (spot)->on_actor_joined (
                                *static_cast<TActor *> (actor));
                          }) {
                static_cast<TSpot *> (spot)->on_actor_joined (*static_cast<TActor *> (actor));
            }
        };
        callbacks.onCreateActor = [] (void *spot, void *actor) {
            if constexpr (std::is_base_of_v<entry_spot_t, TSpot> && requires {
                              static_cast<TSpot *> (spot)->onCreateActor (
                                *static_cast<TActor *> (actor));
                          }) {
                static_cast<TSpot *> (spot)->onCreateActor (*static_cast<TActor *> (actor));
            }
        };
        callbacks.onLeaveActor = [] (void *spot, void *actor) {
            if constexpr (requires {
                              static_cast<TSpot *> (spot)->onLeaveActor (
                                *static_cast<TActor *> (actor));
                          }) {
                static_cast<TSpot *> (spot)->onLeaveActor (*static_cast<TActor *> (actor));
            }
        };
        callbacks.onDisconnectActor = [] (void *spot, void *actor) {
            if constexpr (requires {
                              static_cast<TSpot *> (spot)->onDisconnectActor (
                                *static_cast<TActor *> (actor));
                          }) {
                static_cast<TSpot *> (spot)->onDisconnectActor (*static_cast<TActor *> (actor));
            }
        };
        register_actor_admission_erased (std::type_index (typeid (TActor)), std::move (callbacks));
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
                                            spot_actor_message_metadata_t metadata = {}) const;

    void register_actor_admission_erased (std::type_index actor_type,
                                          detail::spot_actor_admission_callbacks_t callbacks);

    std::shared_ptr<detail::spot_context_state_t> _state;
};

class spot_node_manager_t
{
  public:
    spot_node_manager_t ();
    ~spot_node_manager_t ();

    spot_node_manager_t (spot_node_manager_t &&) noexcept;
    spot_node_manager_t &operator= (spot_node_manager_t &&) noexcept;
    spot_node_manager_t (const spot_node_manager_t &) = default;
    spot_node_manager_t &operator= (const spot_node_manager_t &) = default;

    spot_create_result_t create_spot (std::string spot_name);
    spot_create_result_t create_spot_raw (std::string spot_name, zlink::message_t request);
    spot_create_result_t create_spot (std::string spot_name, const message_t &request);
    template <typename TRequest>
    requires (!std::is_same_v<std::remove_cvref_t<TRequest>, zlink::message_t>
              && !std::is_same_v<std::remove_cvref_t<TRequest>, message_t>) spot_create_result_t
      create_spot (std::string spot_name, const TRequest &request)
    {
        return create_spot_raw (std::move (spot_name),
                                serialize_request (std::type_index (typeid (TRequest)), &request));
    }

    spot_create_result_t get_or_create_spot (std::string spot_name, spot_rid_t spot_rid);
    spot_create_result_t
    get_or_create_spot_raw (std::string spot_name, spot_rid_t spot_rid, zlink::message_t request);
    spot_create_result_t
    get_or_create_spot (std::string spot_name, spot_rid_t spot_rid, const message_t &request);
    template <typename TRequest>
    requires (!std::is_same_v<std::remove_cvref_t<TRequest>, zlink::message_t>
              && !std::is_same_v<std::remove_cvref_t<TRequest>, message_t>) spot_create_result_t
      get_or_create_spot (std::string spot_name, spot_rid_t spot_rid, const TRequest &request)
    {
        return get_or_create_spot_raw (
          std::move (spot_name), std::move (spot_rid),
          serialize_request (std::type_index (typeid (TRequest)), &request));
    }

    std::optional<spot_info_t> find_spot (spot_rid_t spot_rid) const;
    std::vector<spot_info_t> list_spots () const;
    task_t<bool> close_spot (spot_rid_t spot_rid);
    std::optional<std::string> spot_name_for (spot_rid_t spot_rid) const;
    std::optional<spot_route_t> resolve_spot (spot_rid_t spot_rid) const;
    std::optional<actor_ref_t> current_actor_ref (const actor_ref_t &actor_ref) const;
    result_t<std::optional<zlink::message_t>>
    relay_actor_packet (const actor_ref_t &actor_ref,
                        actor_context_t actor_context,
                        std::string_view packet_name,
                        const zlink::message_t &message,
                        service_provider_t &services,
                        serializer_registry_t &serializers,
                        spot_actor_message_metadata_t metadata = {});

  private:
    friend class spot_context_t;
    friend class detail::spot_node_runtime_t;
    explicit spot_node_manager_t (std::shared_ptr<detail::spot_node_builder_state_t> state);
    zlink::message_t serialize_request (std::type_index request_type, const void *request) const;

    std::shared_ptr<detail::spot_node_builder_state_t> _state;
};

namespace detail
{
struct spot_lifecycle_callbacks_t
{
    std::function<std::shared_ptr<void> ()> create_instance;
    std::function<void (void *, spot_context_t &)> configure;
    std::function<void (void *, entry_spot_context_t &)> configure_entry;
    std::function<spot_create_response_t (void *, const zlink::message_t &,
                                          serializer_registry_t &)>
      on_create;
    std::function<void (void *)> on_initialize;
    std::function<void (void *)> on_closing;
};

template <typename TSpot>
concept has_configure_callback = requires (TSpot & spot, spot_context_t &context)
{
    spot.configure (context);
};

template <typename TSpot>
concept has_entry_configure_callback = requires (TSpot & spot, entry_spot_context_t &context)
{
    spot.configure (context);
};

template <typename TSpot>
concept has_framework_create_callback = requires (TSpot & spot, const message_t &request)
{
    {
        spot.on_create (request)
    } -> std::same_as<spot_create_response_t>;
};

template <typename TSpot>
concept has_raw_create_callback = requires (TSpot & spot, const zlink::message_t &request)
{
    {
        spot.on_create_raw (request)
    } -> std::same_as<spot_create_response_t>;
};

template <typename TSpot> concept has_initialize_callback = requires (TSpot & spot)
{
    spot.on_initialize ();
};

template <typename TSpot> concept has_closing_callback = requires (TSpot & spot)
{
    spot.on_closing ();
};
} // namespace detail

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
    spot_node_builder_t &connect_peer_pub (std::string endpoint);
    spot_node_builder_t &enable_actor_gateway ();
    spot_node_builder_t &use_discovery (std::string channel_name);
    spot_node_builder_t &use_registry_spot_remote_addresses ();
    spot_node_builder_t &use_registry_spot_remote_addresses (std::string route_channel_name);
    spot_node_builder_t &use_registry_spot_resolver ();
    spot_node_builder_t &use_registry_spot_resolver (std::string route_channel_name);
    spot_node_builder_t &accept_routes_from_channel (std::string route_channel_name,
                                                     std::string endpoint);
    spot_node_builder_t &
    accept_routes_from_channel (std::string route_channel_name,
                                std::vector<std::string> manual_connections = {});
    spot_node_builder_t &attach_publisher (std::string channel_name,
                                           std::vector<std::string> manual_connections = {});

    template <typename TSpot> spot_node_builder_t &add_spot (std::string spot_name)
    {
        static_assert (std::is_base_of_v<spot_t, TSpot>,
                       "SPOT type must derive from zlink::framework::spot_t");
        static_assert (!std::is_base_of_v<entry_spot_t, TSpot>,
                       "Entry SPOT type must be registered with add_entry_spot<TEntrySpot>()");
        const auto registered_name = spot_name;
        auto &builder =
          add_spot_factory (std::move (spot_name), std::type_index (typeid (TSpot)), false);
        register_lifecycle<TSpot> (registered_name);
        return builder;
    }

    template <typename TSpot>
    spot_node_builder_t &add_spot (std::string spot_name,
                                   std::function<std::shared_ptr<TSpot> ()> factory)
    {
        static_assert (std::is_base_of_v<spot_t, TSpot>,
                       "SPOT type must derive from zlink::framework::spot_t");
        static_assert (!std::is_base_of_v<entry_spot_t, TSpot>,
                       "Entry SPOT type must be registered with add_entry_spot<TEntrySpot>()");
        if (!factory) {
            throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                         "SPOT factory must not be empty");
        }
        const auto registered_name = spot_name;
        auto &builder =
          add_spot_factory (std::move (spot_name), std::type_index (typeid (TSpot)), false);
        register_lifecycle<TSpot> (registered_name, std::move (factory));
        return builder;
    }

    template <typename TEntrySpot> spot_node_builder_t &add_entry_spot ()
    {
        static_assert (std::is_base_of_v<entry_spot_t, TEntrySpot>,
                       "Entry SPOT type must derive from zlink::framework::entry_spot_t");
        auto &builder =
          add_spot_factory (std::string ("entry"), std::type_index (typeid (TEntrySpot)), true);
        register_lifecycle<TEntrySpot> ("entry");
        return builder;
    }

    template <typename TEntrySpot>
    spot_node_builder_t &add_entry_spot (std::function<std::shared_ptr<TEntrySpot> ()> factory)
    {
        static_assert (std::is_base_of_v<entry_spot_t, TEntrySpot>,
                       "Entry SPOT type must derive from zlink::framework::entry_spot_t");
        if (!factory) {
            throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                         "Entry SPOT factory must not be empty");
        }
        auto &builder =
          add_spot_factory (std::string ("entry"), std::type_index (typeid (TEntrySpot)), true);
        register_lifecycle<TEntrySpot> ("entry", std::move (factory));
        return builder;
    }

    template <typename TActorFactory>
    spot_node_builder_t &add_actor_factory (std::string actor_type)
    {
        static_assert (std::is_default_constructible_v<TActorFactory>,
                       "C++ actor factories must be default constructible");
        if constexpr (requires (TActorFactory & factory, std::string actor_id) {
                          factory.create (std::move (actor_id));
                      }) {
            using actor_type_t =
              std::remove_cvref_t<decltype (std::declval<TActorFactory &> ().create (
                std::declval<std::string> ()))>;
            return add_actor_factory_erased (
              std::move (actor_type), std::type_index (typeid (actor_type_t)),
              [] (std::string actor_id) -> std::shared_ptr<void> {
                  TActorFactory factory;
                  return std::static_pointer_cast<void> (
                    std::make_shared<actor_type_t> (factory.create (std::move (actor_id))));
              },
              [] (void *actor, const actor_ref_t &actor_ref, void *actor_context) {
                  auto &typed_actor = *static_cast<actor_type_t *> (actor);
                  if constexpr (requires { typed_actor.set_actor_ref (actor_ref); }) {
                      typed_actor.set_actor_ref (actor_ref);
                  }
                  if constexpr (requires {
                                    typed_actor.set_actor_context (
                                      *static_cast<actor_context_t *> (actor_context));
                                }) {
                      if (actor_context != nullptr) {
                          typed_actor.set_actor_context (
                            *static_cast<actor_context_t *> (actor_context));
                      }
                  }
              });
        } else {
            return add_actor_factory_erased (
              std::move (actor_type), std::type_index (typeid (TActorFactory)),
              [] (std::string) -> std::shared_ptr<void> {
                  return std::static_pointer_cast<void> (std::make_shared<TActorFactory> ());
              },
              [] (void *actor, const actor_ref_t &actor_ref, void *actor_context) {
                  auto &typed_actor = *static_cast<TActorFactory *> (actor);
                  if constexpr (requires { typed_actor.set_actor_ref (actor_ref); }) {
                      typed_actor.set_actor_ref (actor_ref);
                  }
                  if constexpr (requires {
                                    typed_actor.set_actor_context (
                                      *static_cast<actor_context_t *> (actor_context));
                                }) {
                      if (actor_context != nullptr) {
                          typed_actor.set_actor_context (
                            *static_cast<actor_context_t *> (actor_context));
                      }
                  }
              });
        }
    }

    spot_node_builder_t &
    add_spot_resolver (std::string name,
                       std::function<std::optional<spot_route_t> (spot_rid_t)> resolver);

    spot_node_snapshot_t snapshot () const;
    spot_create_result_t create_spot (std::string spot_name);
    spot_create_result_t create_spot_raw (std::string spot_name, zlink::message_t request);
    spot_create_result_t create_spot (std::string spot_name, const message_t &request);
    spot_create_result_t get_or_create_spot (std::string spot_name, spot_rid_t spot_rid);
    spot_create_result_t
    get_or_create_spot_raw (std::string spot_name, spot_rid_t spot_rid, zlink::message_t request);
    spot_create_result_t
    get_or_create_spot (std::string spot_name, spot_rid_t spot_rid, const message_t &request);
    std::optional<spot_info_t> find_spot (spot_rid_t spot_rid) const;
    std::vector<spot_info_t> list_spots () const;
    task_t<bool> close_spot (spot_rid_t spot_rid);
    std::optional<std::string> spot_name_for (spot_rid_t spot_rid) const;
    std::optional<spot_route_t> resolve_spot (spot_rid_t spot_rid) const;

  private:
    friend class zlink_builder_t;
    friend class detail::spot_node_runtime_t;
    explicit spot_node_builder_t (std::shared_ptr<detail::spot_node_builder_state_t> state);

    spot_node_builder_t &
    add_spot_factory (std::string spot_name, std::type_index spot_type, bool entry_spot);
    spot_node_builder_t &add_actor_factory_erased (
      std::string actor_type,
      std::type_index actor_instance_type,
      std::function<std::shared_ptr<void> (std::string)> create_instance,
      std::function<void (void *, const actor_ref_t &, void *)> configure_instance);

    template <typename TSpot>
    void register_lifecycle (std::string spot_name,
                             std::function<std::shared_ptr<TSpot> ()> factory = {})
    {
        detail::spot_lifecycle_callbacks_t callbacks;
        if (factory) {
            callbacks.create_instance = [factory = std::move (factory)] {
                return std::static_pointer_cast<void> (factory ());
            };
        } else if constexpr (std::is_default_constructible_v<TSpot>) {
            callbacks.create_instance = [] {
                return std::static_pointer_cast<void> (std::make_shared<TSpot> ());
            };
        }
        if (callbacks.create_instance) {
            if constexpr (std::is_base_of_v<entry_spot_t, TSpot>
                          && detail::has_entry_configure_callback<TSpot>) {
                callbacks.configure_entry = [] (void *spot, entry_spot_context_t &context) {
                    static_cast<TSpot *> (spot)->configure (context);
                };
            } else if constexpr (detail::has_configure_callback<TSpot>) {
                callbacks.configure = [] (void *spot, spot_context_t &context) {
                    static_cast<TSpot *> (spot)->configure (context);
                };
            }
            if constexpr (detail::has_framework_create_callback<TSpot>) {
                callbacks.on_create = [] (void *spot,
                                          const zlink::message_t &request,
                                          serializer_registry_t &serializers) {
                    return static_cast<TSpot *> (spot)->on_create (
                      message_t::from_encoded (request, &serializers));
                };
            } else if constexpr (detail::has_raw_create_callback<TSpot>) {
                callbacks.on_create = [] (void *spot,
                                          const zlink::message_t &request,
                                          serializer_registry_t &) {
                    return static_cast<TSpot *> (spot)->on_create_raw (request);
                };
            }
            if constexpr (detail::has_initialize_callback<TSpot>) {
                callbacks.on_initialize = [] (void *spot) {
                    static_cast<TSpot *> (spot)->on_initialize ();
                };
            }
            if constexpr (detail::has_closing_callback<TSpot>) {
                callbacks.on_closing = [] (void *spot) {
                    static_cast<TSpot *> (spot)->on_closing ();
                };
            }
        }
        register_lifecycle_erased (std::move (spot_name), std::move (callbacks));
    }
    void register_lifecycle_erased (std::string spot_name,
                                    detail::spot_lifecycle_callbacks_t callbacks);

    std::shared_ptr<detail::spot_node_builder_state_t> _state;
};

} // namespace zlink::framework
