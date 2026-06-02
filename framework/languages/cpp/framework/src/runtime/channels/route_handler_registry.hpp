/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "runtime/messaging/envelope_codec.hpp"

#include <zlink/Contracts/Core/routing_id.hpp>
#include <zlink/framework/contracts/channels/channel.hpp>
#include <zlink/framework/contracts/codecs/serializer.hpp>
#include <zlink/framework/contracts/configuration/services.hpp>
#include <zlink/framework/contracts/detail/message_name.hpp>
#include <zlink/framework/contracts/dispatch/task.hpp>
#include <zlink/framework/contracts/errors/result.hpp>

#include <functional>
#include <map>
#include <string>
#include <typeindex>

namespace zlink::framework::detail
{

inline result_t<zlink::message_t>
route_current_exception_to_message_result ()
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
      "routed handler threw an exception");
  }
}

struct route_handler_descriptor_t
{
  runtime::messaging::message_kind_t kind;
  std::string router_channel_id;
  std::string packet_name;
  std::type_index owner_type;
  std::type_index message_type;
  std::type_index reply_type;
};

class route_handler_registry_t
{
public:
  using invoker_t = std::function<task_t<zlink::message_t> (
    service_provider_t &,
    serializer_registry_t &,
    const zlink::message_t &,
    const framework::route_handler_context_t &)>;

  template<typename TOwner, typename TMessage>
  route_handler_registry_t &on_send (
    std::string router_channel_id,
    std::string packet_name,
    void (TOwner::*method) (const TMessage &,
                            const framework::route_handler_context_t &))
  {
    const auto packet = packet_name.empty ()
                          ? framework::detail::message_name<TMessage> ()
                          : std::move (packet_name);
    return add_handler (
      route_handler_descriptor_t {
        runtime::messaging::message_kind_t::command,
        std::move (router_channel_id),
        packet,
        std::type_index (typeid (TOwner)),
        std::type_index (typeid (TMessage)),
        std::type_index (typeid (void)) },
      [method](service_provider_t &services,
               serializer_registry_t &serializers,
               const zlink::message_t &message,
               const framework::route_handler_context_t &context)
        -> task_t<zlink::message_t> {
        try {
          auto &owner = services.get_required<TOwner> ();
          auto payload = serializers.get<TMessage> ().deserialize (message);
          (owner.*method) (payload, context);
          return task_t<zlink::message_t> (
            result_t<zlink::message_t>::success (zlink::message_t {}));
        } catch (...) {
          return task_t<zlink::message_t> (
            route_current_exception_to_message_result ());
        }
      });
  }

  template<typename TOwner, typename TMessage>
  route_handler_registry_t &on_send (
    std::string router_channel_id,
    std::string packet_name,
    task_t<void> (TOwner::*method) (const TMessage &,
                                    const framework::route_handler_context_t &))
  {
    const auto packet = packet_name.empty ()
                          ? framework::detail::message_name<TMessage> ()
                          : std::move (packet_name);
    return add_handler (
      route_handler_descriptor_t {
        runtime::messaging::message_kind_t::command,
        std::move (router_channel_id),
        packet,
        std::type_index (typeid (TOwner)),
        std::type_index (typeid (TMessage)),
        std::type_index (typeid (void)) },
      [method](service_provider_t &services,
               serializer_registry_t &serializers,
               const zlink::message_t &message,
               const framework::route_handler_context_t &context)
        -> task_t<zlink::message_t> {
        try {
          auto &owner = services.get_required<TOwner> ();
          auto payload = serializers.get<TMessage> ().deserialize (message);
          co_await (owner.*method) (payload, context);
          co_return result_t<zlink::message_t>::success (zlink::message_t {});
        } catch (...) {
          co_return route_current_exception_to_message_result ();
        }
      });
  }

  template<typename TOwner, typename TRequest, typename TReply>
  route_handler_registry_t &on_request (
    std::string router_channel_id,
    std::string packet_name,
    TReply (TOwner::*method) (const TRequest &,
                              const framework::route_handler_context_t &))
  {
    const auto packet = packet_name.empty ()
                          ? framework::detail::message_name<TRequest> ()
                          : std::move (packet_name);
    return add_handler (
      route_handler_descriptor_t {
        runtime::messaging::message_kind_t::request,
        std::move (router_channel_id),
        packet,
        std::type_index (typeid (TOwner)),
        std::type_index (typeid (TRequest)),
        std::type_index (typeid (TReply)) },
      [method](service_provider_t &services,
               serializer_registry_t &serializers,
               const zlink::message_t &message,
               const framework::route_handler_context_t &context)
        -> task_t<zlink::message_t> {
        try {
          auto &owner = services.get_required<TOwner> ();
          auto request = serializers.get<TRequest> ().deserialize (message);
          auto reply = (owner.*method) (request, context);
          return task_t<zlink::message_t> (
            result_t<zlink::message_t>::success (
              serializers.get<TReply> ().serialize (reply)));
        } catch (...) {
          return task_t<zlink::message_t> (
            route_current_exception_to_message_result ());
        }
      });
  }

  template<typename TOwner, typename TRequest, typename TReply>
  route_handler_registry_t &on_request (
    std::string router_channel_id,
    std::string packet_name,
    task_t<TReply> (TOwner::*method) (const TRequest &,
                                      const framework::route_handler_context_t &))
  {
    const auto packet = packet_name.empty ()
                          ? framework::detail::message_name<TRequest> ()
                          : std::move (packet_name);
    return add_handler (
      route_handler_descriptor_t {
        runtime::messaging::message_kind_t::request,
        std::move (router_channel_id),
        packet,
        std::type_index (typeid (TOwner)),
        std::type_index (typeid (TRequest)),
        std::type_index (typeid (TReply)) },
      [method](service_provider_t &services,
               serializer_registry_t &serializers,
               const zlink::message_t &message,
               const framework::route_handler_context_t &context)
        -> task_t<zlink::message_t> {
        try {
          auto &owner = services.get_required<TOwner> ();
          auto request = serializers.get<TRequest> ().deserialize (message);
          auto reply = co_await (owner.*method) (request, context);
          co_return result_t<zlink::message_t>::success (
            serializers.get<TReply> ().serialize (reply));
        } catch (...) {
          co_return route_current_exception_to_message_result ();
        }
      });
  }

  route_handler_registry_t &add_handler (
    route_handler_descriptor_t descriptor,
    invoker_t invoker);

  const route_handler_descriptor_t *find (
    std::string_view router_channel_id,
    runtime::messaging::message_kind_t kind,
    std::string_view packet_name) const;

  result_t<zlink::message_t> invoke (
    std::string_view router_channel_id,
    runtime::messaging::message_kind_t kind,
    std::string_view packet_name,
    service_provider_t &services,
    serializer_registry_t &serializers,
    const zlink::message_t &message,
    const framework::route_handler_context_t &context) const;
  task_t<zlink::message_t> invoke_async (
    std::string_view router_channel_id,
    runtime::messaging::message_kind_t kind,
    std::string_view packet_name,
    service_provider_t &services,
    serializer_registry_t &serializers,
    const zlink::message_t &message,
    const framework::route_handler_context_t &context) const;

private:
  struct key_t
  {
    std::string router_channel_id;
    runtime::messaging::message_kind_t kind;
    std::string packet_name;

    friend bool
    operator< (const key_t &left, const key_t &right) noexcept
    {
      if (left.router_channel_id != right.router_channel_id) {
        return left.router_channel_id < right.router_channel_id;
      }
      if (left.kind != right.kind) {
        return static_cast<int> (left.kind) < static_cast<int> (right.kind);
      }
      return left.packet_name < right.packet_name;
    }
  };

  struct entry_t
  {
    route_handler_descriptor_t descriptor;
    invoker_t invoker;
  };

  std::map<key_t, entry_t> _handlers;
};

} // namespace zlink::framework::detail
