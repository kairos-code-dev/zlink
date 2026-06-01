/* SPDX-License-Identifier: MPL-2.0 */

#include <zlink/framework.hpp>

#include <stdexcept>
#include <string>

namespace
{

struct request_t
{
  int value {};
};

struct reply_t
{
  int value {};
};

struct command_t
{
  int value {};
};

struct event_t
{
  int value {};
};

struct async_request_t
{
  int value {};
};

class handler_t
{
public:
  reply_t get_reply (const request_t &request)
  {
    last_request = request.value;
    return { request.value + 1 };
  }

  void on_command (const command_t &command)
  {
    last_command = command.value;
  }

  zlink::framework::task_t<void> on_event (const event_t &event)
  {
    last_event = event.value;
    co_return;
  }

  zlink::framework::task_t<reply_t> get_async_reply (
    const async_request_t &request)
  {
    last_request = request.value;
    co_return reply_t { request.value + 10 };
  }

  reply_t throw_reply (const request_t &)
  {
    throw std::runtime_error ("boom");
  }

  int last_request = 0;
  int last_command = 0;
  int last_event = 0;
};

template<typename T>
void
add_int_serializer (zlink::framework::serializer_registry_t &serializers)
{
  serializers.add<T> (
    [](const T &value) {
      return zlink::message_t::from (std::to_string (value.value));
    },
    [](const zlink::message_t &message) {
      return T { std::stoi (message.to_string ()) };
    });
}

} // namespace

int
main ()
{
  zlink::framework::service_collection_t services;
  services.add_singleton<handler_t> ();
  auto provider = services.build_provider ();

  zlink::framework::serializer_registry_t serializers;
  add_int_serializer<request_t> (serializers);
  add_int_serializer<reply_t> (serializers);
  add_int_serializer<command_t> (serializers);
  add_int_serializer<event_t> (serializers);
  add_int_serializer<async_request_t> (serializers);

  zlink::framework::handler_registry_t handlers;
  int failure_events = 0;
  zlink::framework::framework_error_kind_t last_failure_kind =
    zlink::framework::framework_error_kind_t::request_failed;
  handlers.observe_failures (
    [&failure_events, &last_failure_kind](
      const zlink::framework::handler_failure_event_t &event) {
      ++failure_events;
      last_failure_kind = event.error_kind;
    });
  handlers.on_request<handler_t, request_t, reply_t> (
    "game",
    "move",
    &handler_t::get_reply,
    { .packet_name = "request" });
  handlers.on_send<handler_t, command_t> (
    "game",
    "command",
    &handler_t::on_command,
    { .packet_name = "command" });
  handlers.on_event<handler_t, event_t> (
    "game",
    "event",
    &handler_t::on_event,
    { .packet_name = "event",
      .execution = zlink::framework::handler_execution_t::offload });
  handlers.on_request<handler_t, async_request_t, reply_t> (
    "game",
    "async",
    &handler_t::get_async_reply,
    { .packet_name = "async" });
  handlers.on_request<handler_t, request_t, reply_t> (
    "game",
    "throw",
    &handler_t::throw_reply,
    { .packet_name = "throw" });

  const auto *descriptor = handlers.find ("game", "move", "request");
  if (descriptor == nullptr || descriptor->topic != "move") {
    return 1;
  }
  if (handlers.find ("game", "other-topic", "request") != nullptr) {
    return 2;
  }

  auto request_result = handlers.invoke (
    "game",
    "move",
    "request",
    provider,
    serializers,
    zlink::message_t::from (std::string ("7")));
  if (!request_result ||
      serializers.get<reply_t> ().deserialize (request_result.value ()).value !=
        8) {
    return 3;
  }

  auto send_result = handlers.invoke (
    "game",
    "command",
    "command",
    provider,
    serializers,
    zlink::message_t::from (std::string ("9")));
  if (!send_result || provider.get_required<handler_t> ().last_command != 9) {
    return 4;
  }

  auto event_result = handlers.invoke (
    "game",
    "event",
    "event",
    provider,
    serializers,
    zlink::message_t::from (std::string ("11")));
  if (!event_result || provider.get_required<handler_t> ().last_event != 11) {
    return 5;
  }
  descriptor = handlers.find ("game", "event", "event");
  if (descriptor == nullptr ||
      descriptor->execution !=
        zlink::framework::handler_execution_t::offload) {
    return 6;
  }

  auto async_result = handlers.invoke (
    "game",
    "async",
    "async",
    provider,
    serializers,
    zlink::message_t::from (std::string ("5")));
  if (!async_result ||
      serializers.get<reply_t> ().deserialize (async_result.value ()).value !=
        15) {
    return 7;
  }

  auto missing_result = handlers.invoke (
    "game",
    "missing",
    "request",
    provider,
    serializers,
    zlink::message_t::from (std::string ("1")));
  if (missing_result ||
      missing_result.error_kind () !=
        zlink::framework::framework_error_kind_t::handler_not_found) {
    return 8;
  }

  auto decode_result = handlers.invoke (
    "game",
    "move",
    "request",
    provider,
    serializers,
    zlink::message_t::from (std::string ("bad")));
  if (decode_result ||
      decode_result.error_kind () !=
        zlink::framework::framework_error_kind_t::payload_decode_failed) {
    return 9;
  }
  if (failure_events != 1 ||
      last_failure_kind !=
        zlink::framework::framework_error_kind_t::payload_decode_failed) {
    return 10;
  }

  auto thrown_result = handlers.invoke (
    "game",
    "throw",
    "throw",
    provider,
    serializers,
    zlink::message_t::from (std::string ("1")));
  if (thrown_result ||
      thrown_result.error_kind () !=
        zlink::framework::framework_error_kind_t::request_failed) {
    return 11;
  }
  if (failure_events != 2 ||
      last_failure_kind !=
        zlink::framework::framework_error_kind_t::request_failed) {
    return 12;
  }

  zlink::framework::service_collection_t empty_services;
  auto empty_provider = empty_services.build_provider ();
  auto owner_result = handlers.invoke (
    "game",
    "move",
    "request",
    empty_provider,
    serializers,
    zlink::message_t::from (std::string ("1")));
  if (owner_result ||
      owner_result.error_kind () !=
        zlink::framework::framework_error_kind_t::request_target_not_found) {
    return 13;
  }
  if (failure_events != 3 ||
      last_failure_kind !=
        zlink::framework::framework_error_kind_t::request_target_not_found) {
    return 14;
  }

  bool raw_called = false;
  handlers.send_raw (
    "game",
    "raw-topic",
    "raw",
    [&raw_called](const zlink::framework::payload_view_t &payload) {
      raw_called = payload.copy_message ().to_string () == "raw-body";
      return zlink::framework::result_t<void>::success ();
    });
  auto raw_result = handlers.invoke (
    "game",
    "raw-topic",
    "raw",
    provider,
    serializers,
    zlink::message_t::from (std::string ("raw-body")));
  if (!raw_result || !raw_called) {
    return 15;
  }

  zlink::framework::handler_registry_t default_handlers;
  default_handlers.on_send<handler_t, command_t> (
    "game",
    "default",
    &handler_t::on_command);
  if (default_handlers.find (
        "game",
        "default",
        typeid (command_t).name ()) == nullptr) {
    return 16;
  }

  bool duplicate_failed = false;
  try {
    handlers.on_send<handler_t, command_t> (
      "game",
      "command",
      &handler_t::on_command,
      { .packet_name = "command" });
  } catch (const zlink::framework::framework_exception_t &error) {
    duplicate_failed =
      error.kind () ==
      zlink::framework::framework_error_kind_t::request_protocol_error;
  }
  if (!duplicate_failed) {
    return 17;
  }

  return 0;
}
