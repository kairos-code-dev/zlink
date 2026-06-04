/* SPDX-License-Identifier: MPL-2.0 */

#include <zlink/framework.hpp>

#include <algorithm>
#include <chrono>
#include <limits>
#include <map>
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

struct options_send_t
{
  static constexpr const char *packet_name = "OptionsSend";
  std::string value;
};

struct options_event_t
{
  static constexpr const char *packet_name = "OptionsEvent";
  std::string value;
};

struct late_options_request_t
{
  static constexpr const char *packet_name = "LateOptionsRequest";
  std::string value;
};

struct late_options_reply_t
{
  static constexpr const char *packet_name = "LateOptionsReply";
  std::string value;
};

struct context_options_request_t
{
  static constexpr const char *packet_name = "ContextOptionsRequest";
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

inline void to_json (nlohmann::json &json, const options_send_t &value)
{
  json = { { "value", value.value } };
}

inline void from_json (const nlohmann::json &json, options_send_t &value)
{
  value.value = json.value ("value", "");
}

inline void to_json (nlohmann::json &json, const options_event_t &value)
{
  json = { { "value", value.value } };
}

inline void from_json (const nlohmann::json &json, options_event_t &value)
{
  value.value = json.value ("value", "");
}

inline void to_json (nlohmann::json &json, const late_options_request_t &value)
{
  json = { { "value", value.value } };
}

inline void from_json (const nlohmann::json &json,
                       late_options_request_t &value)
{
  value.value = json.value ("value", "");
}

inline void to_json (nlohmann::json &json,
                     const context_options_request_t &value)
{
  json = { { "value", value.value } };
}

inline void from_json (const nlohmann::json &json,
                       context_options_request_t &value)
{
  value.value = json.value ("value", "");
}

inline void to_json (nlohmann::json &json, const late_options_reply_t &value)
{
  json = { { "value", value.value } };
}

inline void from_json (const nlohmann::json &json, late_options_reply_t &value)
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

class late_options_request_handler_t
{
public:
  using request_type = late_options_request_t;
  using reply_type = late_options_reply_t;
  static constexpr const char *topic_name = "LateOptionsRequest";

  late_options_reply_t handle (const late_options_request_t &request)
  {
    return { "late:" + request.value };
  }
};

class context_options_request_handler_t
{
public:
  using request_type = context_options_request_t;
  using reply_type = options_reply_t;
  static constexpr const char *topic_name = "ContextOptionsRequest";

  options_reply_t handle (
    const context_options_request_t &request,
    const zlink::framework::request_context_t &context)
  {
    return { context.channel_name + ":" + context.packet_name + ":" +
             request.value };
  }
};

class options_send_handler_t
{
public:
  using message_type = options_send_t;
  static constexpr const char *topic_name = "OptionsSend";

  void handle (const options_send_t &message,
               const zlink::framework::send_context_t &context)
  {
    last_value = context.channel_name + ":" + context.packet_name + ":" +
                 message.value;
  }

  static inline std::string last_value;
};

class alias_options_send_handler_t
{
public:
  using message_type = options_send_t;
  static constexpr const char *topic_name = "OptionsSendAlias";

  void handle (const options_send_t &message)
  {
    last_value = "alias:" + message.value;
  }

  static inline std::string last_value;
};

class options_publish_handler_t
{
public:
  using event_type = options_event_t;
  static constexpr const char *topic_name = "OptionsEvent";

  void handle (const options_event_t &event,
               const zlink::framework::publish_context_t &context)
  {
    last_value = context.channel_name + ":" + context.topic + ":" +
                 context.packet_name + ":" + event.value;
  }

  static inline std::string last_value;
};

class options_stream_session_t final
  : public zlink::framework::packet_stream_session_t
{
public:
  static constexpr const char *session_name = "options.session";

  zlink::framework::task_t<void> on_connected (
    zlink::framework::stream_t &) override
  {
    return zlink::framework::task_t<void> (
      zlink::framework::result_t<void>::success ());
  }

  zlink::framework::task_t<void> on_disconnected (
    zlink::framework::stream_t &) override
  {
    return zlink::framework::task_t<void> (
      zlink::framework::result_t<void>::success ());
  }

  zlink::framework::task_t<void> on_error (
    zlink::framework::stream_t &,
    const zlink::framework::stream_error_t &) override
  {
    return zlink::framework::task_t<void> (
      zlink::framework::result_t<void>::success ());
  }

  zlink::framework::task_t<void> on_packet (
    zlink::framework::stream_t &,
    const zlink::framework::stream_header_t &,
    const zlink::message_t &) override
  {
    return zlink::framework::task_t<void> (
      zlink::framework::result_t<void>::success ());
  }
};

class options_filter_t
{
public:
  zlink::framework::task_t<zlink::message_t> invoke (
    const zlink::framework::handler_invocation_context_t &,
    zlink::framework::handler_next_t next)
  {
    ++invoke_count;
    auto message = co_await next ();
    co_return message;
  }

  static inline int invoke_count = 0;
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

  options_filter_t::invoke_count = 0;
  options_send_handler_t::last_value.clear ();
  options_publish_handler_t::last_value.clear ();
  options.handlers ().add<options_request_handler_t> ("api");
  options.codecs ().add_json ();
  options.handlers ().add<late_options_request_handler_t> ("api");
  options.handlers ().add<context_options_request_handler_t> ("api");
  options.handlers ().add_send<options_send_handler_t> ("api");
  options.handlers ().add_publish<options_publish_handler_t> ("events");
  options.use_filter<options_filter_t> ();
  options.metadata ().forward ("trace-id");
  options.configure_dispatch ([](zlink::framework::dispatch_options_t &dispatch) {
    dispatch.spot_dispatch_mode = zlink::framework::dispatch_mode_t::compiled;
    dispatch.stream_dispatch_mode = zlink::framework::dispatch_mode_t::dynamic;
    dispatch.unhandled.request =
      zlink::framework::unhandled_dispatch_action_t::reply_error;
    dispatch.unhandled.send =
      zlink::framework::unhandled_dispatch_action_t::log_and_drop;
    dispatch.unhandled.publish =
      zlink::framework::unhandled_dispatch_action_t::drop;
    dispatch.diagnostics.message_flow =
      zlink::framework::message_flow_log_mode_t::key_transitions;
    dispatch.diagnostics.sample_rate = 0.5;
    dispatch.diagnostics.include_message_sizes = true;
    dispatch.diagnostics.include_native_diagnostics = true;
  });
  options.discovery ().add ("tcp://127.0.0.1:9102");
  options.client_server_channel ("api-channel")
    .server ("tcp://127.0.0.1:9103")
    .client ()
    .handler_group ("api");
  options.fanout_channel ("event-channel")
    .bind ("tcp://127.0.0.1:9104")
    .subscriber ("tcp://127.0.0.1:9105")
    .subscriber ("tcp://127.0.0.1:9108")
    .handler_group ("events");
  options.dealer_mesh_channel ("mesh-channel")
    .bind ("tcp://127.0.0.1:9106")
    .connect ("tcp://127.0.0.1:9107")
    .handler_group ("api");
  options.client_server_channel ("play-channel").client ();
  options.client_server_channel ("profile-channel")
    .client ("tcp://127.0.0.1:9109")
    .client ("tcp://127.0.0.1:9110");
  options.stream_node ("options.stream")
    .bind ("tcp://127.0.0.1:9111")
    .register_session<options_stream_session_t> ();
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
  const auto streams = zlink.streams ();
  if (streams.size () != 1 || streams[0].name != "options.stream" ||
      streams[0].packet_session_name != "options.session") {
    return 63;
  }
  auto stream_scope =
    provider.create_scope (zlink::framework::service_scope_kind_t::stream_session);
  (void) stream_scope.get_required<options_stream_session_t> ();
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
  auto late_result = handlers.invoke (
    "api-channel",
    "LateOptionsRequest",
    late_options_request_t::packet_name,
    provider,
    serializers,
    zlink::message_t::from_json (late_options_request_t { "request" }));
  if (!late_result) {
    return 13;
  }
  const auto late_reply = late_result.value ().parse_json<late_options_reply_t> ();
  if (late_reply.value != "late:request") {
    return 14;
  }
  auto context_result = handlers.invoke (
    "api-channel",
    "ContextOptionsRequest",
    context_options_request_t::packet_name,
    provider,
    serializers,
    zlink::message_t::from_json (context_options_request_t { "request" }));
  if (!context_result) {
    return 15;
  }
  const auto context_reply =
    context_result.value ().parse_json<options_reply_t> ();
  if (context_reply.value !=
      "api-channel:" + std::string (context_options_request_t::packet_name) +
        ":request") {
    return 16;
  }
  auto send_result = handlers.invoke (
    "api-channel",
    "OptionsSend",
    options_send_t::packet_name,
    provider,
    serializers,
    zlink::message_t::from_json (options_send_t { "sent" }));
  if (!send_result ||
      options_send_handler_t::last_value != "api-channel:OptionsSend:sent") {
    return 17;
  }
  auto publish_result = handlers.invoke (
    "event-channel",
    "OptionsEvent",
    options_event_t::packet_name,
    provider,
    serializers,
    zlink::message_t::from_json (options_event_t { "published" }));
  if (!publish_result ||
      options_publish_handler_t::last_value !=
        "event-channel:OptionsEvent:OptionsEvent:published") {
    return 18;
  }
  if (options_filter_t::invoke_count != 5) {
    return 19;
  }
  const auto &metadata_policy =
    provider.get_required<zlink::framework::message_metadata_policy_t> ();
  std::map<std::string, std::string> metadata {
    { "trace-id", "trace-options" },
    { "tenant-id", "tenant-options" }
  };
  const auto projected_metadata = metadata_policy.project (metadata);
  const auto projected_trace = projected_metadata.find ("trace-id");
  if (!projected_trace || *projected_trace != "trace-options" ||
      projected_metadata.contains ("tenant-id") ||
      metadata_policy.can_forward ("tenant-id")) {
    return 20;
  }
  const auto dispatch = options.dispatch_options ();
  if (dispatch.spot_dispatch_mode !=
        zlink::framework::dispatch_mode_t::compiled ||
      dispatch.stream_dispatch_mode !=
        zlink::framework::dispatch_mode_t::dynamic ||
      dispatch.unhandled.publish !=
        zlink::framework::unhandled_dispatch_action_t::drop ||
      dispatch.diagnostics.message_flow !=
        zlink::framework::message_flow_log_mode_t::key_transitions ||
      dispatch.diagnostics.sample_rate != 0.5 ||
      !dispatch.diagnostics.include_message_sizes ||
      !dispatch.diagnostics.include_native_diagnostics) {
    return 21;
  }
  bool invalid_dispatch_failed = false;
  try {
    zlink::framework::service_collection_t invalid_services;
    zlink::framework::handler_registry_t invalid_handlers;
    zlink::framework::serializer_registry_t invalid_serializers;
    zlink::framework::zlink_builder_t invalid_zlink;
    zlink::framework::monitoring_builder_t invalid_monitoring;
    zlink::framework::zlink_framework_options_t invalid_options (
      invalid_services,
      invalid_handlers,
      invalid_serializers,
      invalid_zlink,
      invalid_monitoring);
    invalid_options.configure_dispatch (
      [](zlink::framework::dispatch_options_t &dispatch) {
        dispatch.diagnostics.sample_rate = 1.5;
      });
  } catch (const zlink::framework::framework_exception_t &error) {
    invalid_dispatch_failed =
      error.kind () ==
      zlink::framework::framework_error_kind_t::request_protocol_error;
  }
  if (!invalid_dispatch_failed) {
    return 22;
  }

  bool invalid_send_reply_error_failed = false;
  try {
    zlink::framework::service_collection_t invalid_services;
    zlink::framework::handler_registry_t invalid_handlers;
    zlink::framework::serializer_registry_t invalid_serializers;
    zlink::framework::zlink_builder_t invalid_zlink;
    zlink::framework::monitoring_builder_t invalid_monitoring;
    zlink::framework::zlink_framework_options_t invalid_options (
      invalid_services,
      invalid_handlers,
      invalid_serializers,
      invalid_zlink,
      invalid_monitoring);
    invalid_options.configure_dispatch (
      [](zlink::framework::dispatch_options_t &dispatch) {
        dispatch.unhandled.send =
          zlink::framework::unhandled_dispatch_action_t::reply_error;
      });
  } catch (const zlink::framework::framework_exception_t &error) {
    invalid_send_reply_error_failed =
      error.kind () ==
        zlink::framework::framework_error_kind_t::request_protocol_error &&
      std::string (error.what ()).find ("send has no reply path") !=
        std::string::npos;
  }
  if (!invalid_send_reply_error_failed) {
    return 25;
  }

  bool invalid_publish_reply_error_failed = false;
  try {
    zlink::framework::service_collection_t invalid_services;
    zlink::framework::handler_registry_t invalid_handlers;
    zlink::framework::serializer_registry_t invalid_serializers;
    zlink::framework::zlink_builder_t invalid_zlink;
    zlink::framework::monitoring_builder_t invalid_monitoring;
    zlink::framework::zlink_framework_options_t invalid_options (
      invalid_services,
      invalid_handlers,
      invalid_serializers,
      invalid_zlink,
      invalid_monitoring);
    invalid_options.configure_dispatch (
      [](zlink::framework::dispatch_options_t &dispatch) {
        dispatch.unhandled.publish =
          zlink::framework::unhandled_dispatch_action_t::reply_error;
      });
  } catch (const zlink::framework::framework_exception_t &error) {
    invalid_publish_reply_error_failed =
      error.kind () ==
        zlink::framework::framework_error_kind_t::request_protocol_error &&
      std::string (error.what ()).find ("publish has no reply path") !=
        std::string::npos;
  }
  if (!invalid_publish_reply_error_failed) {
    return 26;
  }

  bool invalid_nan_sample_rate_failed = false;
  try {
    zlink::framework::service_collection_t invalid_services;
    zlink::framework::handler_registry_t invalid_handlers;
    zlink::framework::serializer_registry_t invalid_serializers;
    zlink::framework::zlink_builder_t invalid_zlink;
    zlink::framework::monitoring_builder_t invalid_monitoring;
    zlink::framework::zlink_framework_options_t invalid_options (
      invalid_services,
      invalid_handlers,
      invalid_serializers,
      invalid_zlink,
      invalid_monitoring);
    invalid_options.configure_dispatch (
      [](zlink::framework::dispatch_options_t &dispatch) {
        dispatch.diagnostics.sample_rate =
          std::numeric_limits<double>::quiet_NaN ();
      });
  } catch (const zlink::framework::framework_exception_t &error) {
    invalid_nan_sample_rate_failed =
      error.kind () ==
      zlink::framework::framework_error_kind_t::request_protocol_error;
  }
  if (!invalid_nan_sample_rate_failed) {
    return 27;
  }

  bool incompatible_existing_handler_group_failed = false;
  try {
    zlink::framework::service_collection_t invalid_services;
    zlink::framework::handler_registry_t invalid_handlers;
    zlink::framework::serializer_registry_t invalid_serializers;
    zlink::framework::zlink_builder_t invalid_zlink;
    zlink::framework::monitoring_builder_t invalid_monitoring;
    zlink::framework::zlink_framework_options_t invalid_options (
      invalid_services,
      invalid_handlers,
      invalid_serializers,
      invalid_zlink,
      invalid_monitoring);
    invalid_options.handlers ().add_send<options_send_handler_t> ("commands");
    invalid_options.fanout_channel ("bad-events").handler_group ("commands");
  } catch (const zlink::framework::framework_exception_t &error) {
    incompatible_existing_handler_group_failed =
      error.kind () ==
        zlink::framework::framework_error_kind_t::request_protocol_error &&
      std::string (error.what ()).find ("incompatible handler kind") !=
        std::string::npos;
  }
  if (!incompatible_existing_handler_group_failed) {
    return 29;
  }

  bool incompatible_late_handler_group_failed = false;
  try {
    zlink::framework::service_collection_t invalid_services;
    zlink::framework::handler_registry_t invalid_handlers;
    zlink::framework::serializer_registry_t invalid_serializers;
    zlink::framework::zlink_builder_t invalid_zlink;
    zlink::framework::monitoring_builder_t invalid_monitoring;
    zlink::framework::zlink_framework_options_t invalid_options (
      invalid_services,
      invalid_handlers,
      invalid_serializers,
      invalid_zlink,
      invalid_monitoring);
    invalid_options.client_server_channel ("bad-api").handler_group ("events");
    invalid_options.handlers ().add_publish<options_publish_handler_t> (
      "events");
  } catch (const zlink::framework::framework_exception_t &error) {
    incompatible_late_handler_group_failed =
      error.kind () ==
        zlink::framework::framework_error_kind_t::request_protocol_error &&
      std::string (error.what ()).find ("incompatible handler kind") !=
        std::string::npos;
  }
  if (!incompatible_late_handler_group_failed) {
    return 30;
  }

  bool missing_client_server_capability_failed = false;
  try {
    zlink::framework::service_collection_t invalid_services;
    zlink::framework::handler_registry_t invalid_handlers;
    zlink::framework::serializer_registry_t invalid_serializers;
    zlink::framework::zlink_builder_t invalid_zlink;
    zlink::framework::monitoring_builder_t invalid_monitoring;
    zlink::framework::zlink_framework_options_t invalid_options (
      invalid_services,
      invalid_handlers,
      invalid_serializers,
      invalid_zlink,
      invalid_monitoring);
    invalid_options.client_server_channel ("empty-channel");
    invalid_options.apply ();
  } catch (const zlink::framework::framework_exception_t &error) {
    missing_client_server_capability_failed =
      error.kind () ==
        zlink::framework::framework_error_kind_t::request_protocol_error &&
      std::string (error.what ()).find (
        "must enable server or client capability") != std::string::npos;
  }
  if (!missing_client_server_capability_failed) {
    return 55;
  }

  bool missing_fanout_capability_failed = false;
  try {
    zlink::framework::service_collection_t invalid_services;
    zlink::framework::handler_registry_t invalid_handlers;
    zlink::framework::serializer_registry_t invalid_serializers;
    zlink::framework::zlink_builder_t invalid_zlink;
    zlink::framework::monitoring_builder_t invalid_monitoring;
    zlink::framework::zlink_framework_options_t invalid_options (
      invalid_services,
      invalid_handlers,
      invalid_serializers,
      invalid_zlink,
      invalid_monitoring);
    invalid_options.fanout_channel ("empty-fanout");
    invalid_options.apply ();
  } catch (const zlink::framework::framework_exception_t &error) {
    missing_fanout_capability_failed =
      error.kind () ==
        zlink::framework::framework_error_kind_t::request_protocol_error &&
      std::string (error.what ()).find (
        "must enable publisher or subscriber capability") !=
        std::string::npos;
  }
  if (!missing_fanout_capability_failed) {
    return 56;
  }

  bool duplicate_high_level_send_handler_failed = false;
  try {
    zlink::framework::service_collection_t invalid_services;
    zlink::framework::handler_registry_t invalid_handlers;
    zlink::framework::serializer_registry_t invalid_serializers;
    zlink::framework::zlink_builder_t invalid_zlink;
    zlink::framework::monitoring_builder_t invalid_monitoring;
    zlink::framework::zlink_framework_options_t invalid_options (
      invalid_services,
      invalid_handlers,
      invalid_serializers,
      invalid_zlink,
      invalid_monitoring);
    invalid_options.client_server_channel ("duplicate-api")
      .server ("tcp://127.0.0.1:9340")
      .handler_group ("api");
    invalid_options.handlers ().add_send<options_send_handler_t> ("api");
    invalid_options.handlers ().add_send<options_send_handler_t> ("api");
  } catch (const zlink::framework::framework_exception_t &error) {
    duplicate_high_level_send_handler_failed =
      error.kind () ==
        zlink::framework::framework_error_kind_t::request_protocol_error &&
      std::string (error.what ()).find ("duplicate handler registration") !=
        std::string::npos;
  }
  if (!duplicate_high_level_send_handler_failed) {
    return 57;
  }

  bool duplicate_late_high_level_request_handler_failed = false;
  try {
    zlink::framework::service_collection_t invalid_services;
    zlink::framework::handler_registry_t invalid_handlers;
    zlink::framework::serializer_registry_t invalid_serializers;
    zlink::framework::zlink_builder_t invalid_zlink;
    zlink::framework::monitoring_builder_t invalid_monitoring;
    zlink::framework::zlink_framework_options_t invalid_options (
      invalid_services,
      invalid_handlers,
      invalid_serializers,
      invalid_zlink,
      invalid_monitoring);
    invalid_options.handlers ().add<options_request_handler_t> ("api");
    invalid_options.handlers ().add<options_request_handler_t> ("api");
    invalid_options.client_server_channel ("late-duplicate-api")
      .server ("tcp://127.0.0.1:9341")
      .handler_group ("api");
  } catch (const zlink::framework::framework_exception_t &error) {
    duplicate_late_high_level_request_handler_failed =
      error.kind () ==
        zlink::framework::framework_error_kind_t::request_protocol_error &&
      std::string (error.what ()).find ("duplicate handler registration") !=
        std::string::npos;
  }
  if (!duplicate_late_high_level_request_handler_failed) {
    return 58;
  }

  bool duplicate_cross_group_packet_handler_failed = false;
  try {
    zlink::framework::service_collection_t invalid_services;
    zlink::framework::handler_registry_t invalid_handlers;
    zlink::framework::serializer_registry_t invalid_serializers;
    zlink::framework::zlink_builder_t invalid_zlink;
    zlink::framework::monitoring_builder_t invalid_monitoring;
    zlink::framework::zlink_framework_options_t invalid_options (
      invalid_services,
      invalid_handlers,
      invalid_serializers,
      invalid_zlink,
      invalid_monitoring);
    invalid_options.handlers ().add_send<options_send_handler_t> ("api");
    invalid_options.handlers ().add_send<alias_options_send_handler_t> (
      "api-alias");
    invalid_options.client_server_channel ("duplicate-packet-api")
      .server ("tcp://127.0.0.1:9342")
      .handler_group ("api")
      .handler_group ("api-alias");
    invalid_options.apply ();
  } catch (const zlink::framework::framework_exception_t &error) {
    duplicate_cross_group_packet_handler_failed =
      error.kind () ==
        zlink::framework::framework_error_kind_t::request_protocol_error &&
      std::string (error.what ()).find ("duplicate handler registration") !=
        std::string::npos;
  }
  if (!duplicate_cross_group_packet_handler_failed) {
    return 59;
  }

  bool missing_server_handler_group_failed = false;
  try {
    zlink::framework::service_collection_t invalid_services;
    zlink::framework::handler_registry_t invalid_handlers;
    zlink::framework::serializer_registry_t invalid_serializers;
    zlink::framework::zlink_builder_t invalid_zlink;
    zlink::framework::monitoring_builder_t invalid_monitoring;
    zlink::framework::zlink_framework_options_t invalid_options (
      invalid_services,
      invalid_handlers,
      invalid_serializers,
      invalid_zlink,
      invalid_monitoring);
    invalid_options.client_server_channel ("empty-server")
      .server ("tcp://127.0.0.1:9300");
    invalid_options.apply ();
  } catch (const zlink::framework::framework_exception_t &error) {
    missing_server_handler_group_failed =
      error.kind () ==
        zlink::framework::framework_error_kind_t::request_protocol_error &&
      std::string (error.what ()).find (
        "server must map a request or send handler group") !=
        std::string::npos;
  }
  if (!missing_server_handler_group_failed) {
    return 31;
  }

  bool accepted_spot_route_without_handlers_succeeded = true;
  try {
    zlink::framework::service_collection_t valid_services;
    zlink::framework::handler_registry_t valid_handlers;
    zlink::framework::serializer_registry_t valid_serializers;
    zlink::framework::zlink_builder_t valid_zlink;
    zlink::framework::monitoring_builder_t valid_monitoring;
    zlink::framework::zlink_framework_options_t valid_options (
      valid_services,
      valid_handlers,
      valid_serializers,
      valid_zlink,
      valid_monitoring);
    valid_options.discovery ().add ("tcp://127.0.0.1:9304");
    valid_options.client_server_channel ("spot-route")
      .server ("tcp://127.0.0.1:9301");
    valid_options.spot_mesh ("spot-routes")
      .node ("route-node")
      .enable_router ("tcp://127.0.0.1:9302")
      .accept_routes_from_channel ("spot-route");
    valid_options.apply ();
  } catch (const zlink::framework::framework_exception_t &) {
    accepted_spot_route_without_handlers_succeeded = false;
  }
  if (!accepted_spot_route_without_handlers_succeeded) {
    return 32;
  }

  bool accepted_spot_route_manual_without_discovery_succeeded = true;
  try {
    zlink::framework::service_collection_t valid_services;
    zlink::framework::handler_registry_t valid_handlers;
    zlink::framework::serializer_registry_t valid_serializers;
    zlink::framework::zlink_builder_t valid_zlink;
    zlink::framework::monitoring_builder_t valid_monitoring;
    zlink::framework::zlink_framework_options_t valid_options (
      valid_services,
      valid_handlers,
      valid_serializers,
      valid_zlink,
      valid_monitoring);
    valid_options.client_server_channel ("manual-spot-route")
      .server ("tcp://127.0.0.1:9343");
    valid_options.spot_mesh ("manual-spots")
      .node ("route-node")
      .enable_router ("tcp://127.0.0.1:9344")
      .accept_routes_from_channel (
        "manual-spot-route",
        [](zlink::framework::accepted_spot_route_channel_builder_t &routes) {
          routes.connect ("tcp://127.0.0.1:9343");
        });
    valid_options.apply ();
    const auto spots = valid_zlink.spot_nodes ();
    accepted_spot_route_manual_without_discovery_succeeded =
      spots.size () == 1 &&
      spots[0].accepted_route_channels.size () == 1 &&
      spots[0].accepted_route_channels[0].channel_name ==
        "manual-spot-route" &&
      spots[0].accepted_route_channels[0].manual_connections.size () == 1 &&
      spots[0].accepted_route_channels[0].manual_connections[0] ==
        "tcp://127.0.0.1:9343";
  } catch (const zlink::framework::framework_exception_t &) {
    accepted_spot_route_manual_without_discovery_succeeded = false;
  }
  if (!accepted_spot_route_manual_without_discovery_succeeded) {
    return 60;
  }

  bool missing_subscriber_handler_group_failed = false;
  try {
    zlink::framework::service_collection_t invalid_services;
    zlink::framework::handler_registry_t invalid_handlers;
    zlink::framework::serializer_registry_t invalid_serializers;
    zlink::framework::zlink_builder_t invalid_zlink;
    zlink::framework::monitoring_builder_t invalid_monitoring;
    zlink::framework::zlink_framework_options_t invalid_options (
      invalid_services,
      invalid_handlers,
      invalid_serializers,
      invalid_zlink,
      invalid_monitoring);
    invalid_options.discovery ().add ("tcp://127.0.0.1:9303");
    invalid_options.fanout_channel ("empty-events").subscriber ();
    invalid_options.apply ();
  } catch (const zlink::framework::framework_exception_t &error) {
    missing_subscriber_handler_group_failed =
      error.kind () ==
        zlink::framework::framework_error_kind_t::request_protocol_error &&
      std::string (error.what ()).find (
        "subscriber must map a publish handler group") != std::string::npos;
  }
  if (!missing_subscriber_handler_group_failed) {
    return 33;
  }

  bool missing_route_bind_failed = false;
  try {
    zlink::framework::service_collection_t invalid_services;
    zlink::framework::handler_registry_t invalid_handlers;
    zlink::framework::serializer_registry_t invalid_serializers;
    zlink::framework::zlink_builder_t invalid_zlink;
    zlink::framework::monitoring_builder_t invalid_monitoring;
    zlink::framework::zlink_framework_options_t invalid_options (
      invalid_services,
      invalid_handlers,
      invalid_serializers,
      invalid_zlink,
      invalid_monitoring);
    invalid_options.route_mesh_channel ("missing-route")
      .routing_id (zlink::routing_id_t::from ("missing"));
    invalid_options.apply ();
  } catch (const zlink::framework::framework_exception_t &error) {
    missing_route_bind_failed =
      error.kind () ==
        zlink::framework::framework_error_kind_t::request_protocol_error &&
      std::string (error.what ()).find ("requires a bind endpoint") !=
        std::string::npos;
  }
  if (!missing_route_bind_failed) {
    return 34;
  }

  bool missing_spot_capability_failed = false;
  try {
    zlink::framework::service_collection_t invalid_services;
    zlink::framework::handler_registry_t invalid_handlers;
    zlink::framework::serializer_registry_t invalid_serializers;
    zlink::framework::zlink_builder_t invalid_zlink;
    zlink::framework::monitoring_builder_t invalid_monitoring;
    zlink::framework::zlink_framework_options_t invalid_options (
      invalid_services,
      invalid_handlers,
      invalid_serializers,
      invalid_zlink,
      invalid_monitoring);
    invalid_options.spot_mesh ("empty-spots")
      .node ("empty-node")
      .add_spot<stage_spot_t> ("stage");
    invalid_options.apply ();
  } catch (const zlink::framework::framework_exception_t &error) {
    missing_spot_capability_failed =
      error.kind () ==
        zlink::framework::framework_error_kind_t::request_protocol_error &&
      std::string (error.what ()).find (
        "must enable router or pub/sub capability") != std::string::npos;
  }
  if (!missing_spot_capability_failed) {
    return 35;
  }

  auto accepted_route_failure_contains =
    [](auto configure, const std::string &needle) {
      bool failed = false;
      try {
        zlink::framework::service_collection_t invalid_services;
        zlink::framework::handler_registry_t invalid_handlers;
        zlink::framework::serializer_registry_t invalid_serializers;
        zlink::framework::zlink_builder_t invalid_zlink;
        zlink::framework::monitoring_builder_t invalid_monitoring;
        zlink::framework::zlink_framework_options_t invalid_options (
          invalid_services,
          invalid_handlers,
          invalid_serializers,
          invalid_zlink,
          invalid_monitoring);
        configure (invalid_options);
        invalid_options.apply ();
      } catch (const zlink::framework::framework_exception_t &error) {
        failed =
          error.kind () ==
            zlink::framework::framework_error_kind_t::request_protocol_error &&
          std::string (error.what ()).find (needle) != std::string::npos;
      }
      return failed;
    };

  auto options_failure_contains =
    [](auto configure, const std::string &needle) {
      bool failed = false;
      try {
        zlink::framework::service_collection_t invalid_services;
        zlink::framework::handler_registry_t invalid_handlers;
        zlink::framework::serializer_registry_t invalid_serializers;
        zlink::framework::zlink_builder_t invalid_zlink;
        zlink::framework::monitoring_builder_t invalid_monitoring;
        zlink::framework::zlink_framework_options_t invalid_options (
          invalid_services,
          invalid_handlers,
          invalid_serializers,
          invalid_zlink,
          invalid_monitoring);
        configure (invalid_options);
        invalid_options.apply ();
      } catch (const zlink::framework::framework_exception_t &error) {
        failed =
          error.kind () ==
            zlink::framework::framework_error_kind_t::request_protocol_error &&
          std::string (error.what ()).find (needle) != std::string::npos;
      }
      return failed;
    };

  if (!options_failure_contains (
        [](zlink::framework::zlink_framework_options_t &invalid_options) {
          invalid_options.client_server_channel (" ");
        },
        "channel name is required")) {
    return 41;
  }

  if (!options_failure_contains (
        [](zlink::framework::zlink_framework_options_t &invalid_options) {
          invalid_options.client_server_channel ("api").server (" ");
        },
        "server endpoint is required")) {
    return 42;
  }

  if (!options_failure_contains (
        [](zlink::framework::zlink_framework_options_t &invalid_options) {
          invalid_options.handlers ().add<options_request_handler_t> (" ");
        },
        "handler group name is required")) {
    return 43;
  }

  if (!options_failure_contains (
        [](zlink::framework::zlink_framework_options_t &invalid_options) {
          invalid_options.spot_mesh (" ");
        },
        "SPOT mesh channel name is required")) {
    return 44;
  }

  if (!options_failure_contains (
        [](zlink::framework::zlink_framework_options_t &invalid_options) {
          invalid_options.stream_node (" ");
        },
        "STREAM node name is required")) {
    return 45;
  }

  if (!options_failure_contains (
        [](zlink::framework::zlink_framework_options_t &invalid_options) {
          invalid_options.use_registry_spot_remote_addresses (" ");
        },
        "route channel is required")) {
    return 46;
  }

  if (!options_failure_contains (
        [](zlink::framework::zlink_framework_options_t &invalid_options) {
          invalid_options.discovery ().add (" ");
        },
        "discovery endpoint is required")) {
    return 60;
  }

  if (!options_failure_contains (
        [](zlink::framework::zlink_framework_options_t &invalid_options) {
          invalid_options.metadata ().forward (" ");
        },
        "metadata key must not be empty")) {
    return 61;
  }

  if (!options_failure_contains (
        [](zlink::framework::zlink_framework_options_t &invalid_options) {
          invalid_options.client_server_channel ("api")
            .server ("tcp://127.0.0.1:9320")
            .enable_spot_route_egress ("play.route");
        },
        "routed SPOT egress requires client capability")) {
    return 62;
  }

  bool attach_publisher_succeeded = true;
  try {
    zlink::framework::service_collection_t valid_services;
    zlink::framework::handler_registry_t valid_handlers;
    zlink::framework::serializer_registry_t valid_serializers;
    zlink::framework::zlink_builder_t valid_zlink;
    zlink::framework::monitoring_builder_t valid_monitoring;
    zlink::framework::zlink_framework_options_t valid_options (
      valid_services,
      valid_handlers,
      valid_serializers,
      valid_zlink,
      valid_monitoring);
    valid_options.discovery ().add ("tcp://127.0.0.1:9319");
    valid_options.publisher_channel ("events")
      .bind ("tcp://127.0.0.1:9320");
    valid_options.spot_mesh ("spots")
      .node ("spot-node")
      .enable_pub_sub ("tcp://127.0.0.1:9321")
      .attach_publisher ("events");
    valid_options.apply ();
    const auto snapshots = valid_zlink.spot_nodes ();
    attach_publisher_succeeded =
      snapshots.size () == 1 &&
      snapshots[0].attached_publishers.size () == 1 &&
      snapshots[0].attached_publishers[0] == "events";
  } catch (const zlink::framework::framework_exception_t &) {
    attach_publisher_succeeded = false;
  }
  if (!attach_publisher_succeeded) {
    return 47;
  }

  bool attach_channel_client_manual_succeeded = true;
  try {
    zlink::framework::service_collection_t valid_services;
    zlink::framework::handler_registry_t valid_handlers;
    zlink::framework::serializer_registry_t valid_serializers;
    zlink::framework::zlink_builder_t valid_zlink;
    zlink::framework::monitoring_builder_t valid_monitoring;
    zlink::framework::zlink_framework_options_t valid_options (
      valid_services,
      valid_handlers,
      valid_serializers,
      valid_zlink,
      valid_monitoring);
    valid_options.handlers ().add_send<options_send_handler_t> ("api");
    valid_options.client_server_channel ("manual-api")
      .server ("tcp://127.0.0.1:9345")
      .handler_group ("api");
    valid_options.spot_mesh ("spots")
      .node ("spot-node")
      .enable_router ("tcp://127.0.0.1:9346")
      .attach_channel_client (
        "manual-api",
        [](zlink::framework::attached_channel_client_builder_t &client) {
          client.connect ("tcp://127.0.0.1:9345");
        });
    valid_options.apply ();
    const auto snapshots = valid_zlink.spot_nodes ();
    attach_channel_client_manual_succeeded =
      snapshots.size () == 1 &&
      snapshots[0].attached_channel_clients.size () == 1 &&
      snapshots[0].attached_channel_clients[0] == "manual-api" &&
      snapshots[0].attached_channel_client_details.size () == 1 &&
      snapshots[0].attached_channel_client_details[0].channel_name ==
        "manual-api" &&
      snapshots[0].attached_channel_client_details[0]
          .manual_connections.size () == 1 &&
      snapshots[0].attached_channel_client_details[0]
          .manual_connections[0] == "tcp://127.0.0.1:9345";
  } catch (const zlink::framework::framework_exception_t &) {
    attach_channel_client_manual_succeeded = false;
  }
  if (!attach_channel_client_manual_succeeded) {
    return 61;
  }

  bool attach_publisher_manual_succeeded = true;
  try {
    zlink::framework::service_collection_t valid_services;
    zlink::framework::handler_registry_t valid_handlers;
    zlink::framework::serializer_registry_t valid_serializers;
    zlink::framework::zlink_builder_t valid_zlink;
    zlink::framework::monitoring_builder_t valid_monitoring;
    zlink::framework::zlink_framework_options_t valid_options (
      valid_services,
      valid_handlers,
      valid_serializers,
      valid_zlink,
      valid_monitoring);
    valid_options.publisher_channel ("manual-events")
      .bind ("tcp://127.0.0.1:9347");
    valid_options.spot_mesh ("spots")
      .node ("spot-node")
      .enable_pub_sub ("tcp://127.0.0.1:9348")
      .attach_publisher (
        "manual-events",
        [](zlink::framework::attached_publisher_builder_t &publisher) {
          publisher.connect ("tcp://127.0.0.1:9347");
        });
    valid_options.apply ();
    const auto snapshots = valid_zlink.spot_nodes ();
    attach_publisher_manual_succeeded =
      snapshots.size () == 1 &&
      snapshots[0].attached_publishers.size () == 1 &&
      snapshots[0].attached_publishers[0] == "manual-events" &&
      snapshots[0].attached_publisher_details.size () == 1 &&
      snapshots[0].attached_publisher_details[0].channel_name ==
        "manual-events" &&
      snapshots[0].attached_publisher_details[0]
          .manual_connections.size () == 1 &&
      snapshots[0].attached_publisher_details[0]
          .manual_connections[0] == "tcp://127.0.0.1:9347";
  } catch (const zlink::framework::framework_exception_t &) {
    attach_publisher_manual_succeeded = false;
  }
  if (!attach_publisher_manual_succeeded) {
    return 62;
  }

  if (!options_failure_contains (
        [](zlink::framework::zlink_framework_options_t &invalid_options) {
          invalid_options.discovery ().add ("tcp://127.0.0.1:9322");
          invalid_options.spot_mesh ("spots")
            .node ("spot-node")
            .enable_router ("tcp://127.0.0.1:9323")
            .attach_channel_client ("missing");
        },
        "attached channel client 'missing' is not registered")) {
    return 48;
  }

  bool attach_channel_client_server_with_discovery_succeeded = true;
  try {
    zlink::framework::service_collection_t valid_services;
    zlink::framework::handler_registry_t valid_handlers;
    zlink::framework::serializer_registry_t valid_serializers;
    zlink::framework::zlink_builder_t valid_zlink;
    zlink::framework::monitoring_builder_t valid_monitoring;
    zlink::framework::zlink_framework_options_t valid_options (
      valid_services,
      valid_handlers,
      valid_serializers,
      valid_zlink,
      valid_monitoring);
    valid_options.discovery ().add ("tcp://127.0.0.1:9324");
    valid_options.handlers ().add_send<options_send_handler_t> ("api");
    valid_options.client_server_channel ("api")
      .server ("tcp://127.0.0.1:9325")
      .handler_group ("api");
    valid_options.spot_mesh ("spots")
      .node ("spot-node")
      .enable_router ("tcp://127.0.0.1:9326")
      .attach_channel_client ("api");
    valid_options.apply ();
    const auto snapshots = valid_zlink.spot_nodes ();
    attach_channel_client_server_with_discovery_succeeded =
      snapshots.size () == 1 &&
      snapshots[0].attached_channel_clients.size () == 1 &&
      snapshots[0].attached_channel_clients[0] == "api";
  } catch (const zlink::framework::framework_exception_t &) {
    attach_channel_client_server_with_discovery_succeeded = false;
  }
  if (!attach_channel_client_server_with_discovery_succeeded) {
    return 49;
  }

  if (!options_failure_contains (
        [](zlink::framework::zlink_framework_options_t &invalid_options) {
          invalid_options.client_server_channel ("api")
            .server ("tcp://127.0.0.1:9327");
          invalid_options.spot_mesh ("spots")
            .node ("spot-node")
            .enable_router ("tcp://127.0.0.1:9328")
            .attach_channel_client ("api");
        },
        "requires registry discovery or manual connections")) {
    return 50;
  }

  if (!options_failure_contains (
        [](zlink::framework::zlink_framework_options_t &invalid_options) {
          invalid_options.discovery ().add ("tcp://127.0.0.1:9328");
          invalid_options.publisher_channel ("events")
            .bind ("tcp://127.0.0.1:9329");
          invalid_options.spot_mesh ("spots")
            .node ("spot-node")
            .enable_router ("tcp://127.0.0.1:9330")
            .attach_publisher ("events");
        },
        "must enable pub/sub capability")) {
    return 51;
  }

  if (!options_failure_contains (
        [](zlink::framework::zlink_framework_options_t &invalid_options) {
          invalid_options.discovery ().add ("tcp://127.0.0.1:9331");
          invalid_options.spot_mesh ("spots")
            .node ("spot-node")
            .enable_pub_sub ("tcp://127.0.0.1:9332")
            .attach_publisher ("missing");
        },
        "publisher client 'missing' is not registered")) {
    return 52;
  }

  if (!options_failure_contains (
        [](zlink::framework::zlink_framework_options_t &invalid_options) {
          invalid_options.discovery ().add ("tcp://127.0.0.1:9333");
          invalid_options.publisher_channel ("events")
            .subscriber ("tcp://127.0.0.1:9334");
          invalid_options.spot_mesh ("spots")
            .node ("spot-node")
            .enable_pub_sub ("tcp://127.0.0.1:9335")
            .attach_publisher ("events");
        },
        "requires publisher capability")) {
    return 53;
  }

  if (!options_failure_contains (
        [](zlink::framework::zlink_framework_options_t &invalid_options) {
          invalid_options.discovery ().add ("tcp://127.0.0.1:9336");
          invalid_options.publisher_channel ("events")
            .bind ("tcp://127.0.0.1:9337");
          invalid_options.spot_mesh ("spots")
            .node ("first")
            .enable_pub_sub ("tcp://127.0.0.1:9338")
            .attach_publisher ("events");
          invalid_options.spot_node ("second")
            .enable_pub_sub ("tcp://127.0.0.1:9339")
            .attach_publisher ("events");
        },
        "duplicate SPOT publisher client channel")) {
    return 54;
  }

  if (!accepted_route_failure_contains (
        [](zlink::framework::zlink_framework_options_t &invalid_options) {
          invalid_options.discovery ().add ("tcp://127.0.0.1:9305");
          invalid_options.client_server_channel ("api")
            .server ("tcp://127.0.0.1:9306");
          invalid_options.spot_mesh ("spots")
            .node ("spot-node")
            .enable_pub_sub ("tcp://127.0.0.1:9307")
            .accept_routes_from_channel ("api");
        },
        "must enable router capability")) {
    return 36;
  }

  if (!accepted_route_failure_contains (
        [](zlink::framework::zlink_framework_options_t &invalid_options) {
          invalid_options.discovery ().add ("tcp://127.0.0.1:9308");
          invalid_options.spot_mesh ("spots")
            .node ("spot-node")
            .enable_router ("tcp://127.0.0.1:9309")
            .accept_routes_from_channel ("missing");
        },
        "is not registered")) {
    return 37;
  }

  if (!accepted_route_failure_contains (
        [](zlink::framework::zlink_framework_options_t &invalid_options) {
          invalid_options.discovery ().add ("tcp://127.0.0.1:9310");
          invalid_options.fanout_channel ("events")
            .bind ("tcp://127.0.0.1:9311");
          invalid_options.spot_mesh ("spots")
            .node ("spot-node")
            .enable_router ("tcp://127.0.0.1:9312")
            .accept_routes_from_channel ("events");
        },
        "must be a client/server channel or a route mesh channel")) {
    return 38;
  }

  if (!accepted_route_failure_contains (
        [](zlink::framework::zlink_framework_options_t &invalid_options) {
          invalid_options.discovery ().add ("tcp://127.0.0.1:9313");
          invalid_options.client_server_channel ("route")
            .server ("tcp://127.0.0.1:9314");
          invalid_options.route_mesh_channel ("route")
            .bind ("tcp://127.0.0.1:9315");
          invalid_options.spot_mesh ("spots")
            .node ("spot-node")
            .enable_router ("tcp://127.0.0.1:9316")
            .accept_routes_from_channel ("route");
        },
        "ambiguous")) {
    return 39;
  }

  if (!accepted_route_failure_contains (
        [](zlink::framework::zlink_framework_options_t &invalid_options) {
          invalid_options.client_server_channel ("api")
            .server ("tcp://127.0.0.1:9317");
          invalid_options.spot_mesh ("spots")
            .node ("spot-node")
            .enable_router ("tcp://127.0.0.1:9318")
            .accept_routes_from_channel ("api");
        },
        "requires registry discovery or manual connections")) {
    return 40;
  }

  bool missing_discovery_failed = false;
  try {
    zlink::framework::service_collection_t invalid_services;
    zlink::framework::handler_registry_t invalid_handlers;
    zlink::framework::serializer_registry_t invalid_serializers;
    zlink::framework::zlink_builder_t invalid_zlink;
    zlink::framework::monitoring_builder_t invalid_monitoring;
    zlink::framework::zlink_framework_options_t invalid_options (
      invalid_services,
      invalid_handlers,
      invalid_serializers,
      invalid_zlink,
      invalid_monitoring);
    invalid_options.client_server_channel ("missing-discovery").client ();
    invalid_options.apply ();
  } catch (const zlink::framework::framework_exception_t &error) {
    missing_discovery_failed =
      error.kind () ==
        zlink::framework::framework_error_kind_t::request_protocol_error &&
      std::string (error.what ()).find ("requires registry discovery") !=
        std::string::npos;
  }
  if (!missing_discovery_failed) {
    return 23;
  }

  bool duplicate_session_failed = false;
  try {
    zlink::framework::service_collection_t invalid_services;
    zlink::framework::handler_registry_t invalid_handlers;
    zlink::framework::serializer_registry_t invalid_serializers;
    zlink::framework::zlink_builder_t invalid_zlink;
    zlink::framework::monitoring_builder_t invalid_monitoring;
    zlink::framework::zlink_framework_options_t invalid_options (
      invalid_services,
      invalid_handlers,
      invalid_serializers,
      invalid_zlink,
      invalid_monitoring);
    invalid_options.stream_node ("client.stream")
      .bind ("tcp://127.0.0.1:9200")
      .packet_session ("first")
      .packet_session ("second");
  } catch (const zlink::framework::framework_exception_t &error) {
    duplicate_session_failed =
      error.kind () ==
        zlink::framework::framework_error_kind_t::request_protocol_error &&
      std::string (error.what ()).find ("already has a packet session") !=
        std::string::npos;
  }
  if (!duplicate_session_failed) {
    return 24;
  }

  bool duplicate_typed_session_failed = false;
  try {
    zlink::framework::service_collection_t invalid_services;
    zlink::framework::handler_registry_t invalid_handlers;
    zlink::framework::serializer_registry_t invalid_serializers;
    zlink::framework::zlink_builder_t invalid_zlink;
    zlink::framework::monitoring_builder_t invalid_monitoring;
    zlink::framework::zlink_framework_options_t invalid_options (
      invalid_services,
      invalid_handlers,
      invalid_serializers,
      invalid_zlink,
      invalid_monitoring);
    invalid_options.stream_node ("typed.stream")
      .bind ("tcp://127.0.0.1:9201")
      .register_session<options_stream_session_t> ()
      .packet_session ("second");
  } catch (const zlink::framework::framework_exception_t &error) {
    duplicate_typed_session_failed =
      error.kind () ==
        zlink::framework::framework_error_kind_t::request_protocol_error &&
      std::string (error.what ()).find ("already has a packet session") !=
        std::string::npos;
  }
  if (!duplicate_typed_session_failed) {
    return 64;
  }

  bool missing_dealer_peer_path_failed = false;
  try {
    zlink::framework::service_collection_t invalid_services;
    zlink::framework::handler_registry_t invalid_handlers;
    zlink::framework::serializer_registry_t invalid_serializers;
    zlink::framework::zlink_builder_t invalid_zlink;
    zlink::framework::monitoring_builder_t invalid_monitoring;
    zlink::framework::zlink_framework_options_t invalid_options (
      invalid_services,
      invalid_handlers,
      invalid_serializers,
      invalid_zlink,
      invalid_monitoring);
    invalid_options.dealer_mesh_channel ("missing-mesh");
    invalid_options.apply ();
  } catch (const zlink::framework::framework_exception_t &error) {
    missing_dealer_peer_path_failed =
      error.kind () ==
        zlink::framework::framework_error_kind_t::request_protocol_error &&
      std::string (error.what ()).find ("requires a bind or connect endpoint") !=
        std::string::npos;
  }
  if (!missing_dealer_peer_path_failed) {
    return 28;
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
  const auto event_channel = std::find_if (
    channels.begin (), channels.end (), [](const auto &channel) {
      return channel.name == "event-channel";
    });
  const auto mesh_channel = std::find_if (
    channels.begin (), channels.end (), [](const auto &channel) {
      return channel.name == "mesh-channel";
    });
  const auto profile_channel = std::find_if (
    channels.begin (), channels.end (), [](const auto &channel) {
      return channel.name == "profile-channel";
    });
  if (channels.size () != 5 ||
      api_channel == channels.end () ||
      play_channel == channels.end () ||
      event_channel == channels.end () ||
      mesh_channel == channels.end () ||
      profile_channel == channels.end () ||
      !api_channel->server.enabled ||
      api_channel->server.bind_endpoints.front () !=
        "tcp://127.0.0.1:9103" ||
      !api_channel->client.enabled ||
      !api_channel->client.discovery ||
      !event_channel->publisher.enabled ||
      event_channel->publisher.bind_endpoints.front () !=
        "tcp://127.0.0.1:9104" ||
      !event_channel->subscriber.enabled ||
      event_channel->subscriber.connect_endpoints.size () != 2 ||
      event_channel->subscriber.connect_endpoints[0] !=
        "tcp://127.0.0.1:9105" ||
      event_channel->subscriber.connect_endpoints[1] !=
        "tcp://127.0.0.1:9108" ||
      !mesh_channel->client.enabled ||
      mesh_channel->client.bind_endpoints.front () !=
        "tcp://127.0.0.1:9106" ||
      mesh_channel->client.connect_endpoints.front () !=
        "tcp://127.0.0.1:9107" ||
      !play_channel->client.enabled ||
      !play_channel->client.discovery ||
      !profile_channel->client.enabled ||
      profile_channel->client.discovery ||
      profile_channel->client.connect_endpoints.size () != 2 ||
      profile_channel->client.connect_endpoints[0] !=
        "tcp://127.0.0.1:9109" ||
      profile_channel->client.connect_endpoints[1] !=
        "tcp://127.0.0.1:9110") {
    return 11;
  }

  return 0;
}
