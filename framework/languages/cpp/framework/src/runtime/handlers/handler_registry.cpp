/* SPDX-License-Identifier: MPL-2.0 */

#include <zlink/framework/contracts/handlers/handler_registry.hpp>

#include <map>
#include <utility>

namespace zlink::framework::detail
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

  friend bool
  operator< (const handler_key_t &left, const handler_key_t &right) noexcept
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

handler_key_t
make_handler_key (std::string_view channel_name,
                  std::string_view topic,
                  std::string_view packet_name)
{
  return { std::string (channel_name),
           std::string (topic),
           std::string (packet_name) };
}

class handler_registry_state_t
{
public:
  std::map<handler_key_t, handler_entry_t> handlers;
  handler_registry_t::failure_observer_t failure_observer;
};

} // namespace zlink::framework::detail

namespace zlink::framework
{

handler_registry_t::handler_registry_t ()
  : _state (std::make_unique<detail::handler_registry_state_t> ())
{
}

handler_registry_t::~handler_registry_t () = default;

handler_registry_t::handler_registry_t (handler_registry_t &&) noexcept =
  default;

handler_registry_t &handler_registry_t::operator= (
  handler_registry_t &&) noexcept = default;

handler_registry_t &
handler_registry_t::send_raw (std::string channel_name,
                              std::string packet_name,
                              raw_handler_t handler,
                              handler_options_t options)
{
  return send_raw (std::move (channel_name),
                   "",
                   std::move (packet_name),
                   std::move (handler),
                   std::move (options));
}

handler_registry_t &
handler_registry_t::send_raw (std::string channel_name,
                              std::string topic,
                              std::string packet_name,
                              raw_handler_t handler,
                              handler_options_t options)
{
  const auto packet = options.packet_name.value_or (packet_name);
  return add_handler (
    { std::move (channel_name),
      std::move (topic),
      packet,
      handler_kind_t::raw,
      options.execution,
      std::type_index (typeid (void)),
      std::type_index (typeid (zlink::message_t)) },
    [handler = std::move (handler)](service_provider_t &,
                                    serializer_registry_t &,
                                    const zlink::message_t &message)
      -> result_t<zlink::message_t> {
      const auto result = handler (payload_view_t (message));
      if (!result) {
        return result_t<zlink::message_t>::failure (
          result.error_kind (),
          result.error () ? result.error ()->what () : "raw handler failed");
      }
      return result_t<zlink::message_t>::success (zlink::message_t {});
    });
}

handler_registry_t &
handler_registry_t::observe_failures (failure_observer_t observer)
{
  _state->failure_observer = std::move (observer);
  return *this;
}

const handler_descriptor_t *
handler_registry_t::find (std::string_view channel_name,
                          std::string_view packet_name) const
{
  return find (channel_name, "", packet_name);
}

const handler_descriptor_t *
handler_registry_t::find (std::string_view channel_name,
                          std::string_view topic,
                          std::string_view packet_name) const
{
  const auto found = _state->handlers.find (detail::make_handler_key (
    channel_name, topic, packet_name));
  if (found == _state->handlers.end ()) {
    return nullptr;
  }
  return &found->second.descriptor;
}

result_t<zlink::message_t>
handler_registry_t::invoke (std::string_view channel_name,
                            std::string_view packet_name,
                            service_provider_t &services,
                            serializer_registry_t &serializers,
                            const zlink::message_t &message) const
{
  return invoke (channel_name, "", packet_name, services, serializers, message);
}

result_t<zlink::message_t>
handler_registry_t::invoke (std::string_view channel_name,
                            std::string_view topic,
                            std::string_view packet_name,
                            service_provider_t &services,
                            serializer_registry_t &serializers,
                            const zlink::message_t &message) const
{
  const auto found = _state->handlers.find (detail::make_handler_key (
    channel_name, topic, packet_name));
  if (found == _state->handlers.end ()) {
    return result_t<zlink::message_t>::failure (
      framework_error_kind_t::handler_not_found,
      "handler is not registered");
  }
  auto result = found->second.invoker (services, serializers, message);
  if (!result && result.error () != nullptr) {
    emit_failure (found->second.descriptor, *result.error ());
  }
  return result;
}

handler_registry_t &
handler_registry_t::add_handler (handler_descriptor_t descriptor,
                                 invoker_t invoker)
{
  const auto key = detail::make_handler_key (descriptor.channel_name,
                                             descriptor.topic,
                                             descriptor.packet_name);
  const auto [_, inserted] = _state->handlers.emplace (
    key,
    detail::handler_entry_t { std::move (descriptor), std::move (invoker) });
  if (!inserted) {
    throw framework_exception_t (
      framework_error_kind_t::request_protocol_error,
      "duplicate handler registration");
  }
  return *this;
}

void
handler_registry_t::emit_failure (const handler_descriptor_t &descriptor,
                                  const framework_exception_t &error) const
{
  if (!_state->failure_observer) {
    return;
  }
  _state->failure_observer (handler_failure_event_t {
    descriptor,
    error.kind (),
    error.what (),
    error.is_retriable () });
}

} // namespace zlink::framework
