/* SPDX-License-Identifier: MPL-2.0 */

#include <zlink/framework.hpp>

#include <algorithm>
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace
{

struct stage_spot_t
{
};

struct stage_packet_t
{
};

struct options_request_t
{
  static constexpr const char *packet_name = "OptionsRequest";
  std::string value;
};

struct options_reply_t
{
  static constexpr const char *packet_name = "OptionsReply";
  std::string value;
};

inline void to_json (nlohmann::json &json, const options_request_t &value)
{
  json = { { "value", value.value } };
}

inline void from_json (const nlohmann::json &json, options_request_t &value)
{
  value.value = json.value ("value", "");
}

inline void to_json (nlohmann::json &json, const options_reply_t &value)
{
  json = { { "value", value.value } };
}

inline void from_json (const nlohmann::json &json, options_reply_t &value)
{
  value.value = json.value ("value", "");
}

class options_request_handler_t
{
public:
  using request_type = options_request_t;
  using reply_type = options_reply_t;
  static constexpr const char *topic_name = "OptionsRequest";

  options_reply_t handle (const options_request_t &request)
  {
    return { "reply:" + request.value };
  }
};

struct stage_state_t
{
  int value = 0;
};

class stage_wrapper_t
{
public:
  stage_wrapper_t (zlink::framework::node_rid_t node,
                   zlink::framework::spot_rid_t spot,
                   zlink::framework::publisher_t publisher,
                   std::size_t packet_count,
                   zlink::framework::timer_options_t timer_options)
    : node_rid (std::move (node)),
      spot_rid (std::move (spot)),
      outbound (std::move (publisher)),
      packet_count (packet_count),
      timer_options (timer_options)
  {
  }

  void apply (int delta) { state.value += delta; }

  stage_state_t state;
  zlink::framework::node_rid_t node_rid;
  zlink::framework::spot_rid_t spot_rid;
  zlink::framework::publisher_t outbound;
  std::size_t packet_count = 0;
  zlink::framework::timer_options_t timer_options;
};

class game_module_t final : public zlink::framework::module_t
{
public:
  void configure_services (
    zlink::framework::service_collection_t &services) override
  {
    services.add_singleton<stage_state_t> ();
    services_configured = true;
  }

  void configure_zlink (zlink::framework::zlink_builder_t &zlink) override
  {
    zlink.node ("module-node")
      .channel ("stage.events",
                [](zlink::framework::channel_builder_t &channel) {
        channel.enable_publisher (
          [](zlink::framework::capability_builder_t &publisher) {
            publisher.bind ("tcp://127.0.0.1:9101");
          });
      });

    zlink::framework::spot_node_builder_t spot_builder;
    zlink.spot_node (
      "stage-node",
      [&spot_builder](zlink::framework::spot_node_builder_t &spot_node) {
        spot_node.add_spot<stage_spot_t> ("stage");
        spot_builder = spot_node;
      });
    auto context = spot_builder.create_spot ("stage");
    context.register_packet<stage_packet_t> ("stage.packet");

    zlink::framework::timer_options_t timer_options;
    timer_options.overrun_policy =
      zlink::framework::timer_overrun_policy_t::catch_up_bounded;
    timer_options.max_catch_up_ticks = 2;
    wrapper.emplace (context.node_rid (),
                     context.spot_rid (),
                     zlink.publisher (),
                     context.packet_registry ().size (),
                     timer_options);
    wrapper->apply (3);
    zlink_configured = true;
  }

  void configure_handlers (
    zlink::framework::handler_registry_t &handlers) override
  {
    (void) handlers;
    handlers_configured = true;
  }

  void configure_monitoring (
    zlink::framework::monitoring_builder_t &monitoring) override
  {
    monitoring.add_spot_events ("stage-node", std::chrono::seconds (1));
    monitoring_configured = true;
  }

  bool services_configured = false;
  bool zlink_configured = false;
  bool handlers_configured = false;
  bool monitoring_configured = false;
  std::optional<stage_wrapper_t> wrapper;
};

struct options_module_counters_t
{
  static inline int services = 0;
  static inline int zlink = 0;
  static inline int handlers = 0;
  static inline int monitoring = 0;
};

class options_module_t
{
public:
  void configure_services (zlink::framework::service_collection_t &services)
  {
    services.add_singleton<stage_state_t> ();
    ++options_module_counters_t::services;
  }

  void configure_zlink (zlink::framework::zlink_builder_t &zlink)
  {
    zlink.node ("options-node");
    ++options_module_counters_t::zlink;
  }

  void configure_handlers (zlink::framework::handler_registry_t &)
  {
    ++options_module_counters_t::handlers;
  }

  void configure_monitoring (zlink::framework::monitoring_builder_t &monitoring)
  {
    monitoring.add_socket_events ("options");
    ++options_module_counters_t::monitoring;
  }
};

class recording_hosted_service_t final
  : public zlink::framework::hosted_service_t
{
public:
  recording_hosted_service_t (std::string name, std::vector<std::string> &events)
    : _name (std::move (name)), _events (events)
  {
  }

  void start (zlink::framework::service_provider_t &services) override
  {
    auto &state = services.get_required<stage_state_t> ();
    state.value += 1;
    _events.push_back ("start:" + _name);
  }

  void stop () noexcept override
  {
    _events.push_back ("stop:" + _name);
  }

private:
  std::string _name;
  std::vector<std::string> &_events;
};

class failing_start_hosted_service_t final
  : public zlink::framework::hosted_service_t
{
public:
  explicit failing_start_hosted_service_t (
    std::vector<std::string> &events)
    : _events (events)
  {
  }

  void start (zlink::framework::service_provider_t &) override
  {
    _events.push_back ("start:failing");
    throw zlink::framework::framework_exception_t (
      zlink::framework::framework_error_kind_t::request_failed,
      "hosted service start failed");
  }

  void stop () noexcept override
  {
    _events.push_back ("stop:failing");
  }

private:
  std::vector<std::string> &_events;
};

} // namespace

int
main ()
{
  zlink::framework::app_t app = zlink::framework::app_t::create ();
  game_module_t module;
  std::vector<std::string> lifecycle;

  app.add_module (module)
    .add_hosted_service (
      std::make_unique<recording_hosted_service_t> ("first", lifecycle))
    .add_hosted_service (
      std::make_unique<recording_hosted_service_t> ("second", lifecycle));

  if (!module.services_configured || !module.zlink_configured ||
      !module.handlers_configured || !module.monitoring_configured ||
      !module.wrapper) {
    return 1;
  }
  if (module.wrapper->packet_count != 1 || module.wrapper->state.value != 3 ||
      module.wrapper->node_rid.empty () || module.wrapper->spot_rid.empty () ||
      module.wrapper->timer_options.overrun_policy !=
        zlink::framework::timer_overrun_policy_t::catch_up_bounded) {
    return 2;
  }

  int argc = 1;
  char program[] = "module-test";
  char *argv[] = { program, nullptr };
  std::thread stopper ([&app] {
    std::this_thread::sleep_for (std::chrono::milliseconds (5));
    app.stop ();
  });
  if (app.run (argc, argv) != 0) {
    stopper.join ();
    return 3;
  }
  stopper.join ();
  const std::vector<std::string> expected {
    "start:first",
    "start:second",
    "stop:second",
    "stop:first"
  };
  if (lifecycle != expected) {
    return 4;
  }

  bool null_hosted_service_failed = false;
  try {
    zlink::framework::app_t invalid = zlink::framework::app_t::create ();
    invalid.add_hosted_service (nullptr);
  } catch (const zlink::framework::framework_exception_t &error) {
    null_hosted_service_failed =
      error.kind () ==
      zlink::framework::framework_error_kind_t::request_protocol_error;
  }
  if (!null_hosted_service_failed) {
    return 5;
  }

  std::vector<std::string> failed_lifecycle;
  bool start_failure_cleaned_up_started_services = false;
  try {
    zlink::framework::app_t failure_app = zlink::framework::app_t::create ();
    failure_app.advanced ().services ().add_singleton<stage_state_t> ();
    failure_app
      .add_hosted_service (
        std::make_unique<recording_hosted_service_t> ("started",
                                                      failed_lifecycle))
      .add_hosted_service (
        std::make_unique<failing_start_hosted_service_t> (failed_lifecycle))
      .add_hosted_service (
        std::make_unique<recording_hosted_service_t> ("never",
                                                      failed_lifecycle));
    failure_app.run (argc, argv);
  } catch (const zlink::framework::framework_exception_t &error) {
    const std::vector<std::string> expected_failed_lifecycle {
      "start:started",
      "start:failing",
      "stop:started"
    };
    start_failure_cleaned_up_started_services =
      error.kind () == zlink::framework::framework_error_kind_t::request_failed &&
      failed_lifecycle == expected_failed_lifecycle;
  }
  if (!start_failure_cleaned_up_started_services) {
    return 12;
  }

  zlink::framework::app_t options_app = zlink::framework::app_t::create ();
  options_app.add_zlink_framework<options_module_t> ();
  if (options_module_counters_t::services != 1 ||
      options_module_counters_t::zlink != 1 ||
      options_module_counters_t::handlers != 1 ||
      options_module_counters_t::monitoring != 1) {
    return 6;
  }

  zlink::framework::service_collection_t services;
  zlink::framework::handler_registry_t handlers;
  zlink::framework::serializer_registry_t serializers;
  zlink::framework::zlink_builder_t zlink;
  zlink::framework::monitoring_builder_t monitoring;
  zlink::framework::zlink_framework_options_t options (
    services, handlers, serializers, zlink, monitoring);

  options.handlers ().add<options_request_handler_t> ("api");
  options.codecs ().add_json ();
  options.discovery ().add ("tcp://127.0.0.1:9102");
  options.client_server_channel ("api-channel")
    .server ("tcp://127.0.0.1:9103")
    .client ()
    .handler_group ("api");
  options.client_server_channel ("play-channel").client ();
  options.apply ();

  const auto *descriptor = handlers.find (
    "api-channel",
    "OptionsRequest",
    options_request_t::packet_name);
  if (descriptor == nullptr ||
      descriptor->execution !=
        zlink::framework::handler_execution_t::offload) {
    return 7;
  }

  auto provider = services.build_provider ();
  auto result = handlers.invoke (
    "api-channel",
    "OptionsRequest",
    options_request_t::packet_name,
    provider,
    serializers,
    zlink::message_t::from_json (options_request_t { "request" }));
  if (!result) {
    return 8;
  }
  const auto reply = result.value ().parse_json<options_reply_t> ();
  if (reply.value != "reply:request") {
    return 9;
  }

  const auto discovery = zlink.discovery_options ();
  if (discovery.registry_endpoints.size () != 1 ||
      discovery.registry_endpoints.front () != "tcp://127.0.0.1:9102") {
    return 10;
  }
  const auto channels = zlink.channels ();
  const auto api_channel = std::find_if (
    channels.begin (), channels.end (), [](const auto &channel) {
      return channel.name == "api-channel";
    });
  const auto play_channel = std::find_if (
    channels.begin (), channels.end (), [](const auto &channel) {
      return channel.name == "play-channel";
    });
  if (channels.size () != 2 ||
      api_channel == channels.end () ||
      play_channel == channels.end () ||
      !api_channel->server.enabled ||
      api_channel->server.bind_endpoints.front () !=
        "tcp://127.0.0.1:9103" ||
      !api_channel->client.enabled ||
      !play_channel->client.enabled) {
    return 11;
  }

  return 0;
}
