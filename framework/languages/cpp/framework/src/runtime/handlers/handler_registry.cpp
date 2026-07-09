/* SPDX-License-Identifier: MPL-2.0 */

#include <zlink/framework/contracts/handlers/handler_registry.hpp>

#include "runtime/actors/actor_gateway_runtime.hpp"
#include "runtime/dispatch/offload_executor.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

namespace zlink::framework
{

namespace
{

runtime::offload_executor_t &handler_invocation_executor ()
{
    static runtime::offload_executor_t executor (
      0, std::max<std::size_t> (1, std::thread::hardware_concurrency ()), 4096,
      std::chrono::seconds (30));
    return executor;
}

} // namespace

namespace detail
{

struct handler_entry_t
{
    handler_descriptor_t descriptor;
    handler_registry_t::invoker_t invoker;
};

struct handler_key_t
{
    std::string channel_name;
    std::string topic;
    std::string packet_name;

    friend bool operator< (const handler_key_t &left, const handler_key_t &right) noexcept
    {
        if (left.channel_name != right.channel_name) {
            return left.channel_name < right.channel_name;
        }
        if (left.topic != right.topic) {
            return left.topic < right.topic;
        }
        return left.packet_name < right.packet_name;
    }
};

handler_key_t make_handler_key (std::string_view channel_name,
                                std::string_view topic,
                                std::string_view packet_name)
{
    return {std::string (channel_name), std::string (topic), std::string (packet_name)};
}

const handler_entry_t *
find_by_channel_packet (const std::map<handler_key_t, handler_entry_t> &handlers,
                        std::string_view channel_name,
                        std::string_view packet_name)
{
    for (const auto &[_, entry] : handlers) {
        if (entry.descriptor.channel_name == channel_name
            && entry.descriptor.packet_name == packet_name) {
            return &entry;
        }
    }
    return nullptr;
}

class handler_registry_state_t
{
  public:
    std::map<handler_key_t, handler_entry_t> handlers;
    std::vector<handler_registry_t::filter_invoker_t> filters;
    handler_registry_t::failure_observer_t failure_observer;
};

} // namespace detail

handler_registry_t::handler_registry_t () :
    _state (std::make_unique<detail::handler_registry_state_t> ())
{
}

handler_registry_t::~handler_registry_t () = default;

handler_registry_t::handler_registry_t (handler_registry_t &&) noexcept = default;

handler_registry_t &handler_registry_t::operator= (handler_registry_t &&) noexcept = default;

handler_registry_t &handler_registry_t::send_raw (std::string channel_name,
                                                  std::string packet_name,
                                                  raw_handler_t handler,
                                                  handler_options_t options)
{
    return send_raw (std::move (channel_name), "", std::move (packet_name), std::move (handler),
                     std::move (options));
}

handler_registry_t &handler_registry_t::send_raw (std::string channel_name,
                                                  std::string topic,
                                                  std::string packet_name,
                                                  raw_handler_t handler,
                                                  handler_options_t options)
{
    const auto packet = options.packet_name.value_or (packet_name);
    return add_handler (
      {std::move (channel_name), std::move (topic), packet, handler_kind_t::raw, options.execution,
       std::type_index (typeid (void)), std::type_index (typeid (zlink::message_t))},
      [handler = std::move (handler)] (service_provider_t &, serializer_registry_t &,
                                       const zlink::message_t &message,
                                       std::string_view) -> task_t<zlink::message_t> {
          const auto result = handler (payload_view_t (detail::encoded_payload_from_raw (message)));
          if (!result) {
              return task_t<zlink::message_t> (result_t<zlink::message_t>::failure (
                result.error_kind (),
                result.error () ? result.error ()->what () : "raw handler failed"));
          }
          return task_t<zlink::message_t> (
            result_t<zlink::message_t>::success (zlink::message_t{}));
      });
}

handler_registry_t &handler_registry_t::observe_failures (failure_observer_t observer)
{
    _state->failure_observer = std::move (observer);
    return *this;
}

handler_registry_t &handler_registry_t::add_filter (filter_invoker_t filter)
{
    _state->filters.push_back (std::move (filter));
    return *this;
}

const handler_descriptor_t *handler_registry_t::find (std::string_view channel_name,
                                                      std::string_view packet_name) const
{
    return find (channel_name, "", packet_name);
}

const handler_descriptor_t *handler_registry_t::find (std::string_view channel_name,
                                                      std::string_view topic,
                                                      std::string_view packet_name) const
{
    const auto found =
      _state->handlers.find (detail::make_handler_key (channel_name, topic, packet_name));
    if (found == _state->handlers.end ()) {
        return nullptr;
    }
    return &found->second.descriptor;
}

result_t<zlink::message_t> handler_registry_t::invoke (std::string_view channel_name,
                                                       std::string_view packet_name,
                                                       service_provider_t &services,
                                                       serializer_registry_t &serializers,
                                                       const zlink::message_t &message,
                                                       std::string_view content_type) const
{
    return invoke (channel_name, "", packet_name, services, serializers, message, content_type);
}

result_t<zlink::message_t> handler_registry_t::invoke (std::string_view channel_name,
                                                       std::string_view topic,
                                                       std::string_view packet_name,
                                                       service_provider_t &services,
                                                       serializer_registry_t &serializers,
                                                       const zlink::message_t &message,
                                                       std::string_view content_type) const
{
    return invoke_async (channel_name, topic, packet_name, services, serializers, message,
                         content_type)
      .result ();
}

task_t<zlink::message_t> handler_registry_t::invoke_async (std::string_view channel_name,
                                                           std::string_view packet_name,
                                                           service_provider_t &services,
                                                           serializer_registry_t &serializers,
                                                           const zlink::message_t &message,
                                                           std::string_view content_type) const
{
    return invoke_async (channel_name, "", packet_name, services, serializers, message,
                         content_type);
}

task_t<zlink::message_t> handler_registry_t::invoke_async (std::string_view channel_name,
                                                           std::string_view topic,
                                                           std::string_view packet_name,
                                                           service_provider_t &services,
                                                           serializer_registry_t &serializers,
                                                           const zlink::message_t &message,
                                                           std::string_view content_type) const
{
    const auto found =
      _state->handlers.find (detail::make_handler_key (channel_name, topic, packet_name));
    const detail::handler_entry_t *entry = nullptr;
    if (found != _state->handlers.end ()) {
        entry = &found->second;
    } else if (topic.empty ()) {
        entry = detail::find_by_channel_packet (_state->handlers, channel_name, packet_name);
    }
    if (entry == nullptr) {
        return task_t<zlink::message_t> (result_t<zlink::message_t>::failure (
          framework_error_kind_t::handler_not_found, "handler is not registered"));
    }
    auto owned_message = std::make_shared<zlink::message_t> (message);
    auto owned_content_type = std::string (content_type);
    auto filters = _state->filters;
    auto invoke_body = [this, entry, filters = std::move (filters), &services, &serializers,
                        owned_message = std::move (owned_message),
                        owned_content_type = std::move (owned_content_type)] () mutable {
        result_t<zlink::message_t> result = result_t<zlink::message_t>::failure (
          framework_error_kind_t::request_failed, "handler failed");
        try {
            handler_invocation_context_t context{
              entry->descriptor, make_handler_context<handler_context_t> (entry->descriptor),
              owned_message};
            context.context.content_type = owned_content_type;
            using chain_t = std::function<task_t<zlink::message_t> (std::size_t)>;
            auto chain = std::make_shared<chain_t> ();
            *chain = [&services, &serializers, &context, &filters, entry, owned_message,
                      &owned_content_type, chain] (std::size_t index) -> task_t<zlink::message_t> {
                if (index >= filters.size ()) {
                    return entry->invoker (services, serializers, *owned_message,
                                           owned_content_type);
                }
                return filters[index](
                  services, serializers, context,
                  [chain, next_index = index + 1] () mutable { return (*chain) (next_index); });
            };
            auto handler_task = (*chain) (0);
            result = handler_task.result ();
        }
        catch (const framework_exception_t &error) {
            result = result_t<zlink::message_t>::failure (error.kind (), error.what (),
                                                          error.is_retriable ());
        }
        catch (...) {
            result = result_t<zlink::message_t>::failure (framework_error_kind_t::request_failed,
                                                          "handler threw an exception");
        }
        if (!result && result.error () != nullptr) {
            emit_failure (entry->descriptor, *result.error ());
        }
        return result;
    };

    if (detail::current_stream_relay_dispatch ()) {
        return task_t<zlink::message_t> (invoke_body ());
    }

    detail::task_completion_source_t<zlink::message_t> completion;
    auto task = completion.task ();
    try {
        handler_invocation_executor ().submit (
          [completion = std::move (completion), invoke_body = std::move (invoke_body)] () mutable {
              completion.complete (invoke_body ());
          });
    }
    catch (const std::exception &error) {
        completion.complete (result_t<zlink::message_t>::failure (
          framework_error_kind_t::request_failed, error.what ()));
    }
    catch (...) {
        completion.complete (result_t<zlink::message_t>::failure (
          framework_error_kind_t::request_failed, "handler executor rejected invocation"));
    }
    return task;
}

handler_registry_t &handler_registry_t::add_handler (handler_descriptor_t descriptor,
                                                     invoker_t invoker)
{
    const auto duplicate_packet =
      std::any_of (_state->handlers.begin (), _state->handlers.end (), [&] (const auto &entry) {
          const auto &existing = entry.second.descriptor;
          if (existing.channel_name != descriptor.channel_name || existing.kind != descriptor.kind
              || existing.packet_name != descriptor.packet_name) {
              return false;
          }
          return existing.topic == descriptor.topic || existing.topic.empty ()
                 || descriptor.topic.empty ();
      });
    if (duplicate_packet) {
        throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                     "duplicate handler registration");
    }
    const auto key =
      detail::make_handler_key (descriptor.channel_name, descriptor.topic, descriptor.packet_name);
    const auto [_, inserted] = _state->handlers.emplace (
      key, detail::handler_entry_t{std::move (descriptor), std::move (invoker)});
    if (!inserted) {
        throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                     "duplicate handler registration");
    }
    return *this;
}

void handler_registry_t::emit_failure (const handler_descriptor_t &descriptor,
                                       const framework_exception_t &error) const
{
    if (!_state->failure_observer) {
        return;
    }
    _state->failure_observer (
      handler_failure_event_t{descriptor, error.kind (), error.what (), error.is_retriable ()});
}

} // namespace zlink::framework
