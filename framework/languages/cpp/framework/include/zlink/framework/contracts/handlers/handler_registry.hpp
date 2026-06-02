/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/framework/contracts/codecs/serializer.hpp>
#include <zlink/framework/contracts/configuration/services.hpp>
#include <zlink/framework/contracts/detail/message_name.hpp>
#include <zlink/framework/contracts/dispatch/execution.hpp>
#include <zlink/framework/contracts/dispatch/task.hpp>
#include <zlink/framework/contracts/errors/result.hpp>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <typeindex>
#include <utility>

namespace zlink::framework
{

namespace detail
{
class handler_registry_state_t;

inline result_t<zlink::message_t>
current_exception_to_message_result ()
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
      "handler threw an exception");
  }
}
} // namespace detail

enum class handler_kind_t
{
  request,
  send,
  event,
  raw
};

struct handler_options_t
{
  std::optional<std::string> packet_name;
  handler_execution_t execution = handler_execution_t::inline_on_runtime;
};

struct handler_descriptor_t
{
  std::string channel_name;
  std::string topic;
  std::string packet_name;
  handler_kind_t kind;
  handler_execution_t execution;
  std::type_index owner_type;
  std::type_index payload_type;
};

struct handler_failure_event_t
{
  handler_descriptor_t descriptor;
  framework_error_kind_t error_kind;
  std::string message;
  bool retriable;
};

class handler_registry_t
{
public:
  using raw_handler_t =
    std::function<result_t<void> (const payload_view_t &)>;
  using invoker_t =
    std::function<result_t<zlink::message_t> (service_provider_t &,
                                              serializer_registry_t &,
                                              const zlink::message_t &)>;
  using failure_observer_t =
    std::function<void (const handler_failure_event_t &)>;

  handler_registry_t ();
  ~handler_registry_t ();

  handler_registry_t (handler_registry_t &&) noexcept;
  handler_registry_t &operator= (handler_registry_t &&) noexcept;
  handler_registry_t (const handler_registry_t &) = delete;
  handler_registry_t &operator= (const handler_registry_t &) = delete;

  template<typename TOwner, typename TRequest, typename TReply>
  handler_registry_t &on_request (
    std::string channel_name,
    std::string topic,
    TReply (TOwner::*method) (const TRequest &),
    handler_options_t options = {})
  {
    const auto packet = options.packet_name.value_or (
      detail::message_name<TRequest> ());
    return add_handler (
      { std::move (channel_name),
        std::move (topic),
        packet,
        handler_kind_t::request,
        options.execution,
        std::type_index (typeid (TOwner)),
        std::type_index (typeid (TRequest)) },
      [method](service_provider_t &services,
               serializer_registry_t &serializers,
               const zlink::message_t &message) -> result_t<zlink::message_t> {
        try {
          auto &owner = services.get_required<TOwner> ();
          auto request = serializers.get<TRequest> ().deserialize (message);
          auto reply = (owner.*method) (request);
          return result_t<zlink::message_t>::success (
            serializers.get<TReply> ().serialize (reply));
        } catch (...) {
          return detail::current_exception_to_message_result ();
        }
      });
  }

  template<typename TOwner, typename TRequest, typename TReply>
  handler_registry_t &on_request (
    std::string channel_name,
    std::string topic,
    task_t<TReply> (TOwner::*method) (const TRequest &),
    handler_options_t options = {})
  {
    const auto packet = options.packet_name.value_or (
      detail::message_name<TRequest> ());
    return add_handler (
      { std::move (channel_name),
        std::move (topic),
        packet,
        handler_kind_t::request,
        options.execution,
        std::type_index (typeid (TOwner)),
        std::type_index (typeid (TRequest)) },
      [method](service_provider_t &services,
               serializer_registry_t &serializers,
               const zlink::message_t &message) -> result_t<zlink::message_t> {
        try {
          auto &owner = services.get_required<TOwner> ();
          auto request = serializers.get<TRequest> ().deserialize (message);
          auto reply = (owner.*method) (request).result ().value ();
          return result_t<zlink::message_t>::success (
            serializers.get<TReply> ().serialize (reply));
        } catch (...) {
          return detail::current_exception_to_message_result ();
        }
      });
  }

  template<typename TOwner, typename TRequest, typename TReply>
  handler_registry_t &request (
    std::string channel_name,
    std::string topic,
    TReply (TOwner::*method) (const TRequest &),
    handler_options_t options = {})
  {
    return on_request<TOwner, TRequest, TReply> (
      std::move (channel_name), std::move (topic), method, std::move (options));
  }

  template<typename TOwner, typename TRequest, typename TReply>
  handler_registry_t &request (
    std::string channel_name,
    std::string topic,
    task_t<TReply> (TOwner::*method) (const TRequest &),
    handler_options_t options = {})
  {
    return on_request<TOwner, TRequest, TReply> (
      std::move (channel_name), std::move (topic), method, std::move (options));
  }

  template<typename TOwner, typename TMessage>
  handler_registry_t &on_send (std::string channel_name,
                               std::string topic,
                               void (TOwner::*method) (const TMessage &),
                               handler_options_t options = {})
  {
    return add_void_member_handler<TOwner, TMessage> (
      std::move (channel_name),
      std::move (topic),
      handler_kind_t::send,
      method,
      std::move (options));
  }

  template<typename TOwner, typename TMessage>
  handler_registry_t &on_send (std::string channel_name,
                               std::string topic,
                               task_t<void> (TOwner::*method) (const TMessage &),
                               handler_options_t options = {})
  {
    return add_task_member_handler<TOwner, TMessage> (
      std::move (channel_name),
      std::move (topic),
      handler_kind_t::send,
      method,
      std::move (options));
  }

  template<typename TOwner, typename TEvent>
  handler_registry_t &on_event (std::string channel_name,
                                std::string topic,
                                void (TOwner::*method) (const TEvent &),
                                handler_options_t options = {})
  {
    return add_void_member_handler<TOwner, TEvent> (
      std::move (channel_name),
      std::move (topic),
      handler_kind_t::event,
      method,
      std::move (options));
  }

  template<typename TOwner, typename TEvent>
  handler_registry_t &on_event (std::string channel_name,
                                std::string topic,
                                task_t<void> (TOwner::*method) (const TEvent &),
                                handler_options_t options = {})
  {
    return add_task_member_handler<TOwner, TEvent> (
      std::move (channel_name),
      std::move (topic),
      handler_kind_t::event,
      method,
      std::move (options));
  }

  handler_registry_t &send_raw (std::string channel_name,
                                std::string packet_name,
                                raw_handler_t handler,
                                handler_options_t options = {});
  handler_registry_t &send_raw (std::string channel_name,
                                std::string topic,
                                std::string packet_name,
                                raw_handler_t handler,
                                handler_options_t options = {});
  handler_registry_t &observe_failures (failure_observer_t observer);

  const handler_descriptor_t *find (std::string_view channel_name,
                                    std::string_view packet_name) const;
  const handler_descriptor_t *find (std::string_view channel_name,
                                    std::string_view topic,
                                    std::string_view packet_name) const;
  result_t<zlink::message_t> invoke (std::string_view channel_name,
                                     std::string_view packet_name,
                                     service_provider_t &services,
                                     serializer_registry_t &serializers,
                                     const zlink::message_t &message) const;
  result_t<zlink::message_t> invoke (std::string_view channel_name,
                                     std::string_view topic,
                                     std::string_view packet_name,
                                     service_provider_t &services,
                                     serializer_registry_t &serializers,
                                     const zlink::message_t &message) const;

private:
  template<typename TOwner, typename TPayload>
  handler_registry_t &add_void_member_handler (
    std::string channel_name,
    std::string topic,
    handler_kind_t kind,
    void (TOwner::*method) (const TPayload &),
    handler_options_t options)
  {
    const auto packet = options.packet_name.value_or (
      detail::message_name<TPayload> ());
    return add_handler (
      { std::move (channel_name),
        std::move (topic),
        packet,
        kind,
        options.execution,
        std::type_index (typeid (TOwner)),
        std::type_index (typeid (TPayload)) },
      [method](service_provider_t &services,
               serializer_registry_t &serializers,
               const zlink::message_t &message) -> result_t<zlink::message_t> {
        try {
          auto &owner = services.get_required<TOwner> ();
          auto payload = serializers.get<TPayload> ().deserialize (message);
          (owner.*method) (payload);
          return result_t<zlink::message_t>::success (zlink::message_t {});
        } catch (...) {
          return detail::current_exception_to_message_result ();
        }
      });
  }

  template<typename TOwner, typename TPayload>
  handler_registry_t &add_task_member_handler (
    std::string channel_name,
    std::string topic,
    handler_kind_t kind,
    task_t<void> (TOwner::*method) (const TPayload &),
    handler_options_t options)
  {
    const auto packet = options.packet_name.value_or (
      detail::message_name<TPayload> ());
    return add_handler (
      { std::move (channel_name),
        std::move (topic),
        packet,
        kind,
        options.execution,
        std::type_index (typeid (TOwner)),
        std::type_index (typeid (TPayload)) },
      [method](service_provider_t &services,
               serializer_registry_t &serializers,
               const zlink::message_t &message) -> result_t<zlink::message_t> {
        try {
          auto &owner = services.get_required<TOwner> ();
          auto payload = serializers.get<TPayload> ().deserialize (message);
          (owner.*method) (payload).result ().value ();
          return result_t<zlink::message_t>::success (zlink::message_t {});
        } catch (...) {
          return detail::current_exception_to_message_result ();
        }
      });
  }

  handler_registry_t &add_handler (handler_descriptor_t descriptor,
                                   invoker_t invoker);
  void emit_failure (const handler_descriptor_t &descriptor,
                     const framework_exception_t &error) const;

  std::unique_ptr<detail::handler_registry_state_t> _state;
};

} // namespace zlink::framework
