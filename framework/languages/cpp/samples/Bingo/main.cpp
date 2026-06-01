/* SPDX-License-Identifier: MPL-2.0 */

#include <zlink/framework.hpp>

#include <chrono>
#include <stdexcept>
#include <string>

namespace
{

struct bingo_request_t
{
  int value {};
};

struct bingo_reply_t
{
  int value {};
};

struct bingo_event_t
{
  int value {};
};

struct bingo_spot_t
{
};

struct bingo_timer_handler_t
{
};

class bingo_handler_t
{
public:
  bingo_reply_t reply (const bingo_request_t &request)
  {
    return { request.value + 1 };
  }

  zlink::framework::task_t<void> on_event (const bingo_event_t &event)
  {
    last_event = event.value;
    co_return;
  }

  bingo_reply_t fail (const bingo_request_t &)
  {
    throw std::runtime_error ("sample handler failure");
  }

  int last_event = 0;
};

class heartbeat_service_t final : public zlink::framework::hosted_service_t
{
public:
  void start (zlink::framework::service_provider_t &services) override
  {
    services.get_required<bingo_handler_t> ().last_event += 1;
    started = true;
  }

  void stop () noexcept override { stopped = true; }

  bool started = false;
  bool stopped = false;
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

zlink::framework::task_t<void>
publish_with_coroutine (zlink::framework::publisher_t &publisher)
{
  co_await publisher
    .publish ("bingo.events", "bingo.called", bingo_event_t { 5 })
    .submit ();
}

} // namespace

int
main (int argc, char **argv)
{
  using zlink::framework::framework_error_kind_t;

  auto app = zlink::framework::app_t::create ();
  auto heartbeat = std::make_unique<heartbeat_service_t> ();
  auto *heartbeat_ptr = heartbeat.get ();

  app.services ().add_singleton<bingo_handler_t> ();
  app.add_hosted_service (std::move (heartbeat));
  app.monitoring ()
    .add_socket_events ("bingo.server")
    .add_registry_events ("registry", std::chrono::seconds (1))
    .add_spot_timer_events ("bingo.timer");
  app.use_zlink ([](zlink::framework::zlink_builder_t &zlink) {
    zlink.node ("bingo-node")
      .discovery ([](zlink::framework::discovery_builder_t &discovery) {
        discovery.connect_registry ("tcp://registry:5551");
      })
      .channel ("bingo", [](zlink::framework::channel_builder_t &channel) {
        channel.enable_server (
          [](zlink::framework::capability_builder_t &server) {
            server.bind ("tcp://0.0.0.0:7100");
          });
        channel.enable_client (
          [](zlink::framework::capability_builder_t &client) {
            client.connect ("tcp://127.0.0.1:7100");
          });
      })
      .channel ("bingo.outbound",
                [](zlink::framework::channel_builder_t &channel) {
        channel.enable_client (
          [](zlink::framework::capability_builder_t &client) {
            client.connect ("tcp://127.0.0.1:7200");
          });
      })
      .channel ("bingo.events",
                [](zlink::framework::channel_builder_t &channel) {
        channel.enable_publisher (
          [](zlink::framework::capability_builder_t &publisher) {
            publisher.bind ("tcp://0.0.0.0:7300");
          });
        channel.enable_subscriber (
          [](zlink::framework::capability_builder_t &subscriber) {
            subscriber.use_discovery ();
          });
      })
      .spot_node ("bingo-spot",
                  [](zlink::framework::spot_node_builder_t &spot_node) {
        spot_node.bind ("tcp://0.0.0.0:7400")
          .use_discovery ("bingo.events")
          .add_spot<bingo_spot_t> ("bingo");
      });
  });

  zlink::framework::serializer_registry_t serializers;
  add_int_serializer<bingo_request_t> (serializers);
  add_int_serializer<bingo_reply_t> (serializers);
  add_int_serializer<bingo_event_t> (serializers);

  app.handlers ()
    .on_request<bingo_handler_t, bingo_request_t, bingo_reply_t> (
      "bingo", "call", &bingo_handler_t::reply, { .packet_name = "request" })
    .on_event<bingo_handler_t, bingo_event_t> (
      "bingo.events",
      "bingo.called",
      &bingo_handler_t::on_event,
      { .packet_name = "event",
        .execution = zlink::framework::handler_execution_t::offload })
    .on_request<bingo_handler_t, bingo_request_t, bingo_reply_t> (
      "bingo", "fail", &bingo_handler_t::fail, { .packet_name = "fail" });

  int failures = 0;
  app.handlers ().observe_failures (
    [&failures](const zlink::framework::handler_failure_event_t &) {
      ++failures;
    });

  auto provider = app.services ().build_provider ();
  auto reply = app.handlers ().invoke (
    "bingo",
    "call",
    "request",
    provider,
    serializers,
    zlink::message_t::from (std::string ("41")));
  if (!reply ||
      serializers.get<bingo_reply_t> ().deserialize (reply.value ()).value !=
        42) {
    return 1;
  }

  auto handler_error = app.handlers ().invoke (
    "bingo",
    "fail",
    "fail",
    provider,
    serializers,
    zlink::message_t::from (std::string ("1")));
  if (handler_error ||
      handler_error.error_kind () != framework_error_kind_t::request_failed ||
      failures != 1) {
    return 2;
  }

  zlink::framework::zlink_builder_t outbound_only;
  outbound_only.node ("bingo-client")
    .channel ("bingo", [](zlink::framework::channel_builder_t &channel) {
      channel.enable_client (
        [](zlink::framework::capability_builder_t &client) {
          client.connect ("tcp://127.0.0.1:7100");
        });
    });
  if (outbound_only.channels ().size () != 1 ||
      !outbound_only.channels ()[0].client.enabled ||
      outbound_only.channels ()[0].server.enabled) {
    return 3;
  }

  zlink::framework::zlink_builder_t runtime;
  runtime.channel ("bingo.events",
                   [](zlink::framework::channel_builder_t &channel) {
    channel.enable_publisher (
      [](zlink::framework::capability_builder_t &publisher) {
        publisher.bind ("tcp://0.0.0.0:7300");
      });
  });
  auto publisher = runtime.publisher ();
  bool callback_submit_seen = false;
  publisher.publish ("bingo.events", "bingo.called", bingo_event_t { 1 })
    .submit ([&](zlink::framework::result_t<void> result) {
      callback_submit_seen = static_cast<bool> (result);
    });
  if (!callback_submit_seen ||
      !publish_with_coroutine (publisher).result ()) {
    return 4;
  }

  zlink::framework::spot_node_builder_t spot_node;
  spot_node.add_spot<bingo_spot_t> ("bingo");
  auto spot = spot_node.create_spot ("bingo");
  auto timer = spot.add_timer<bingo_timer_handler_t> (
    "bingo-tick",
    std::chrono::milliseconds (16),
    { .overrun_policy =
        zlink::framework::timer_overrun_policy_t::skip_late_ticks });
  if (timer.is_disposed ()) {
    return 5;
  }

  app.stop ();
  if (app.run (argc, argv) != 0 || !heartbeat_ptr->started ||
      !heartbeat_ptr->stopped) {
    return 6;
  }

  return 0;
}
