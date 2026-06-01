/* SPDX-License-Identifier: MPL-2.0 */

#include <zlink/framework.hpp>

#include "runtime/channels/channel_runtime.hpp"

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

struct event_t
{
  int value {};
};

class local_handler_t
{
public:
  reply_t handle_request (const request_t &request)
  {
    last_request = request.value;
    return { request.value + 100 };
  }

  void handle_send (const event_t &event)
  {
    last_event = event.value;
  }

  int last_request = 0;
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
  zlink::framework::zlink_builder_t zlink;
  zlink.node ("outbound-node")
    .channel ("profile", [](zlink::framework::channel_builder_t &channel) {
      channel.enable_client ([](zlink::framework::capability_builder_t &client) {
        client.connect ("tcp://127.0.0.1:7101");
      });
    })
    .channel ("events", [](zlink::framework::channel_builder_t &channel) {
      channel.enable_publisher (
        [](zlink::framework::capability_builder_t &publisher) {
          publisher.bind ("tcp://127.0.0.1:7201");
        });
    });

  const auto channels = zlink.channels ();
  if (channels.size () != 2) {
    return 1;
  }

  auto client = zlink.request_client ("profile");
  auto request_result =
    client.request<request_t, reply_t> ({ 1 }).submit ().result ();
  if (request_result ||
      request_result.error_kind () !=
        zlink::framework::framework_error_kind_t::timeout) {
    return 2;
  }

  auto bus = zlink.message_bus ();
  auto send_result = bus.send ("profile", request_t { 2 }).submit ().result ();
  if (!send_result) {
    return 3;
  }

  auto publish_result =
    zlink.publisher ().publish ("events", "profile.changed", event_t { 3 })
      .submit ()
      .result ();
  if (!publish_result) {
    return 4;
  }

  auto disconnected_result =
    bus.send ("missing", request_t { 4 }).submit ().result ();
  if (disconnected_result ||
      disconnected_result.error_kind () !=
        zlink::framework::framework_error_kind_t::disconnected) {
    return 5;
  }

  zlink::framework::zlink_builder_t full_queue;
  full_queue.max_pending (0)
    .channel ("profile", [](zlink::framework::channel_builder_t &channel) {
      channel.enable_client ([](zlink::framework::capability_builder_t &client) {
        client.use_discovery ();
      });
    });
  auto queue_full_result =
    full_queue.message_bus ()
      .request<request_t, reply_t> ("profile", { 5 })
      .submit ()
      .result ();
  if (queue_full_result ||
      queue_full_result.error_kind () !=
        zlink::framework::framework_error_kind_t::request_rejected) {
    return 6;
  }

  bool mixed_connection_failed = false;
  try {
    zlink::framework::zlink_builder_t invalid;
    invalid.channel ("bad", [](zlink::framework::channel_builder_t &channel) {
      channel.enable_client ([](zlink::framework::capability_builder_t &client) {
        client.connect ("tcp://127.0.0.1:7301").use_discovery ();
      });
    });
  } catch (const zlink::framework::framework_exception_t &error) {
    mixed_connection_failed =
      error.kind () ==
      zlink::framework::framework_error_kind_t::request_protocol_error;
  }
  if (!mixed_connection_failed) {
    return 7;
  }

  zlink::framework::zlink_builder_t outbound_only;
  outbound_only.channel (
    "client-only",
    [](zlink::framework::channel_builder_t &channel) {
      channel.enable_client ([](zlink::framework::capability_builder_t &client) {
        client.use_discovery ();
      });
      channel.enable_publisher (
        [](zlink::framework::capability_builder_t &publisher) {
          publisher.use_discovery ();
        });
    });
  const auto outbound_channels = outbound_only.channels ();
  if (outbound_channels.size () != 1 ||
      outbound_channels[0].server.enabled ||
      !outbound_channels[0].client.enabled ||
      !outbound_channels[0].publisher.enabled) {
    return 8;
  }

  zlink::framework::zlink_builder_t local_server;
  local_server.channel (
    "local",
    [](zlink::framework::channel_builder_t &channel) {
      channel.enable_server ([](zlink::framework::capability_builder_t &server) {
        server.bind ("tcp://127.0.0.1:7401");
      });
    });

  zlink::framework::service_collection_t services;
  services.add_singleton<local_handler_t> ();
  auto provider = services.build_provider ();

  zlink::framework::serializer_registry_t serializers;
  add_int_serializer<request_t> (serializers);
  add_int_serializer<reply_t> (serializers);
  add_int_serializer<event_t> (serializers);

  zlink::framework::handler_registry_t handlers;
  handlers.on_request<local_handler_t, request_t, reply_t> (
    "local",
    "request",
    &local_handler_t::handle_request,
    { .packet_name = "request" });
  handlers.on_send<local_handler_t, event_t> (
    "local",
    "send",
    &local_handler_t::handle_send,
    { .packet_name = "event" });

  auto local_runtime = zlink::framework::detail::channel_runtime_t::from (
    local_server.message_bus ());
  auto local_reply = local_runtime.dispatch_request (
    "local",
    "request",
    "request",
    provider,
    serializers,
    handlers,
    zlink::message_t::from (std::string ("23")));
  if (!local_reply ||
      serializers.get<reply_t> ().deserialize (local_reply.value ()).value !=
        123) {
    return 9;
  }

  auto local_send = local_runtime.dispatch_send (
    "local",
    "send",
    "event",
    provider,
    serializers,
    handlers,
    zlink::message_t::from (std::string ("31")));
  if (!local_send || provider.get_required<local_handler_t> ().last_event != 31) {
    return 10;
  }

  auto not_server = local_runtime.dispatch_request (
    "profile",
    "request",
    "request",
    provider,
    serializers,
    handlers,
    zlink::message_t::from (std::string ("1")));
  if (not_server ||
      not_server.error_kind () !=
        zlink::framework::framework_error_kind_t::route_not_connected) {
    return 11;
  }

  auto outbound_runtime = zlink::framework::detail::channel_runtime_t::from (
    zlink.message_bus ());
  auto reservation = outbound_runtime.reserve_outbound_request ("profile");
  if (!reservation || outbound_runtime.pending_count () != 1) {
    return 12;
  }
  auto unmatched_reply = outbound_runtime.complete_outbound_reply (
    reservation.value () + 1000);
  if (unmatched_reply ||
      unmatched_reply.error_kind () !=
        zlink::framework::framework_error_kind_t::request_protocol_error) {
    return 13;
  }
  auto matched_reply = outbound_runtime.complete_outbound_reply (
    reservation.value ());
  if (!matched_reply || outbound_runtime.pending_count () != 0) {
    return 14;
  }

  return 0;
}
