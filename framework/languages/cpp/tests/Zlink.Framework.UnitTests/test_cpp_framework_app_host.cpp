/* SPDX-License-Identifier: MPL-2.0 */

#include <zlink/framework.hpp>
#include <zlink/http_client.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <vector>
#include <string>
#include <thread>
#include <type_traits>

#ifndef ZLINK_FRAMEWORK_HTTP_TEST_HTTP_ENDPOINT
#define ZLINK_FRAMEWORK_HTTP_TEST_HTTP_ENDPOINT "http://127.0.0.1:18080"
#endif

#ifndef ZLINK_FRAMEWORK_HTTP_TEST_HTTPS_INVALID_ENDPOINT
#define ZLINK_FRAMEWORK_HTTP_TEST_HTTPS_INVALID_ENDPOINT "https://127.0.0.1:18443"
#endif

#ifndef ZLINK_FRAMEWORK_HTTP_TEST_HTTPS_ENDPOINT
#define ZLINK_FRAMEWORK_HTTP_TEST_HTTPS_ENDPOINT "https://127.0.0.1:18444"
#endif

#ifndef ZLINK_FRAMEWORK_HTTP_TEST_HTTPS_CLIENT_BASE_URL
#define ZLINK_FRAMEWORK_HTTP_TEST_HTTPS_CLIENT_BASE_URL "https://localhost:18444"
#endif

static_assert (std::is_same_v<decltype (zlink::framework::app_t::create ()),
                              zlink::framework::app_t>);

namespace
{

struct create_game_http_handler_t
{
  struct request_type
  {
    std::string id;
    std::string name;
    std::string filter;
    std::string correlationId;
  };

  struct reply_type
  {
    std::string id;
    std::string name;
    std::string filter;
    std::string correlationId;
  };

  reply_type handle (const request_type &request,
                     zlink::framework::http_context_t &context)
  {
    if (request.name == "timeout") {
      throw zlink::framework::framework_exception_t (
        zlink::framework::framework_error_kind_t::timeout,
        "handler timeout");
    }
    if (request.name == "shutdown") {
      throw zlink::framework::framework_exception_t (
        zlink::framework::framework_error_kind_t::shutdown,
        "handler shutdown");
    }
    if (request.name == "protocol") {
      throw zlink::framework::framework_exception_t (
        zlink::framework::framework_error_kind_t::request_protocol_error,
        "handler protocol error");
    }
    if (request.name == "failed") {
      throw zlink::framework::framework_exception_t (
        zlink::framework::framework_error_kind_t::request_failed,
        "handler failure");
    }
    return {
      .id = request.id,
      .name = request.name,
      .filter = request.filter,
      .correlationId = context.correlation_id
    };
  }
};

struct async_game_http_handler_t
{
  using request_type = create_game_http_handler_t::request_type;
  using reply_type = create_game_http_handler_t::reply_type;

  zlink::framework::task_t<reply_type> handle (
    const request_type &request,
    zlink::framework::http_context_t &context)
  {
    if (request.name == "async-timeout") {
      co_return zlink::framework::result_t<reply_type>::failure (
        zlink::framework::framework_error_kind_t::timeout,
        "async handler timeout");
    }
    co_return reply_type {
      .id = request.id,
      .name = request.name,
      .filter = request.filter,
      .correlationId = context.correlation_id
    };
  }
};

struct http_name_prefix_t
{
  std::string value = "di:";
};

struct scoped_http_counter_t
{
  int count = 0;
};

struct injected_game_http_handler_t
{
  using request_type = create_game_http_handler_t::request_type;
  using reply_type = create_game_http_handler_t::reply_type;
  using dependency_types =
    zlink::framework::dependency_list_t<http_name_prefix_t,
                                        scoped_http_counter_t>;

  explicit injected_game_http_handler_t (http_name_prefix_t &prefix,
                                         scoped_http_counter_t &counter)
    : _prefix (&prefix), _counter (&counter)
  {
  }

  reply_type handle (const request_type &request,
                     zlink::framework::http_context_t &context)
  {
    return {
      .id = request.id,
      .name = _prefix->value + request.name + ":" +
              std::to_string (++_counter->count),
      .filter = request.filter,
      .correlationId = context.correlation_id
    };
  }

private:
  http_name_prefix_t *_prefix;
  scoped_http_counter_t *_counter;
};

void
to_json (nlohmann::json &json,
         const create_game_http_handler_t::request_type &value)
{
  json = nlohmann::json {
    { "id", value.id },
    { "name", value.name },
    { "filter", value.filter },
    { "correlationId", value.correlationId }
  };
}

void
from_json (const nlohmann::json &json,
           create_game_http_handler_t::request_type &value)
{
  if (json.contains ("id")) {
    value.id = json.at ("id").get<std::string> ();
  }
  if (json.contains ("name")) {
    value.name = json.at ("name").get<std::string> ();
  }
  if (json.contains ("filter")) {
    value.filter = json.at ("filter").get<std::string> ();
  }
  if (json.contains ("correlationId")) {
    value.correlationId = json.at ("correlationId").get<std::string> ();
  }
}

void
to_json (nlohmann::json &json,
         const create_game_http_handler_t::reply_type &value)
{
  json = nlohmann::json {
    { "id", value.id },
    { "name", value.name },
    { "filter", value.filter },
    { "correlationId", value.correlationId }
  };
}

void
from_json (const nlohmann::json &json,
           create_game_http_handler_t::reply_type &value)
{
  value.id = json.value ("id", "");
  value.name = json.value ("name", "");
  value.filter = json.value ("filter", "");
  value.correlationId = json.value ("correlationId", "");
}

struct correlation_middleware_t
{
  static inline int before_count = 0;
  static inline int after_count = 0;

  void before (zlink::framework::http_context_t &context)
  {
    ++before_count;
    if (!context.correlation_id.empty ()) {
      context.response_header ("X-Middleware-Before", "seen");
    }
    if (context.path == "/games/blocked") {
      context.json_response (
        202,
        R"({"id":"blocked","name":"short","filter":"","correlationId":"short-circuit"})");
    }
  }

  void after (zlink::framework::http_context_t &context)
  {
    ++after_count;
    context.response_header ("X-Middleware-After", "seen");
    context.response_header ("X-Context-Path", context.path);
  }
};

struct request_state_middleware_t
{
  bool before_seen = false;

  void before (zlink::framework::http_context_t &)
  {
    before_seen = true;
  }

  void after (zlink::framework::http_context_t &context)
  {
    if (before_seen) {
      context.response_header ("X-Middleware-State", "preserved");
    }
  }
};

struct health_http_reply_t
{
  std::string status;
  std::string readiness;
  std::string liveness;
};

void
from_json (const nlohmann::json &json, health_http_reply_t &value)
{
  value.status = json.value ("status", "");
  value.readiness = json.value ("readiness", "");
  value.liveness = json.value ("liveness", "");
}

zlink::http_client::client_t
make_app_host_test_client (
  std::string base_url,
  std::optional<std::string> trust_certificate_file = std::nullopt)
{
  auto builder = zlink::http_client::client_t::create ()
                   .base_url (std::move (base_url))
                   .json ()
                   .timeout (std::chrono::milliseconds (500));
  if (trust_certificate_file) {
    builder.trust_certificate_file (std::move (*trust_certificate_file));
  }
  return builder.build ();
}

bool
wait_for_ready (const zlink::http_client::client_t &client)
{
  for (int attempt = 0; attempt < 100; ++attempt) {
    auto result = client.get ("/ready")
                    .submit<health_http_reply_t> ()
                    .result ();
    if (result.has_value () && result.value ().body.readiness == "healthy") {
      return true;
    }
    std::this_thread::sleep_for (std::chrono::milliseconds (10));
  }
  return false;
}

} // namespace

int
main ()
{
  bool duplicate_route_rejected = false;
  try {
    auto duplicate_app = zlink::framework::app_t::create ();
    duplicate_app.add_zlink_framework (
      [](zlink::framework::zlink_framework_options_t &options) {
        options.http ()
          .listen ("http://127.0.0.1:18081")
          .map_get<create_game_http_handler_t> ("/duplicate")
          .map_get<create_game_http_handler_t> ("/duplicate");
      });
  } catch (const zlink::framework::framework_exception_t &ex) {
    duplicate_route_rejected =
      ex.kind () == zlink::framework::framework_error_kind_t::request_protocol_error;
  }
  if (!duplicate_route_rejected) {
    return 31;
  }

  bool system_route_conflict_rejected = false;
  try {
    auto conflict_app = zlink::framework::app_t::create ();
    conflict_app.add_zlink_framework (
      [](zlink::framework::zlink_framework_options_t &options) {
        options.http ()
          .listen ("http://127.0.0.1:18082")
          .map_readiness ("/ready")
          .map_get<create_game_http_handler_t> ("/ready");
      });
  } catch (const zlink::framework::framework_exception_t &ex) {
    system_route_conflict_rejected =
      ex.kind () == zlink::framework::framework_error_kind_t::request_protocol_error;
  }
  if (!system_route_conflict_rejected) {
    return 32;
  }

  bool duplicate_system_route_rejected = false;
  try {
    auto duplicate_system_app = zlink::framework::app_t::create ();
    duplicate_system_app.add_zlink_framework (
      [](zlink::framework::zlink_framework_options_t &options) {
        options.http ()
          .listen ("http://127.0.0.1:18083")
          .map_health ("/status")
          .map_readiness ("/status");
      });
  } catch (const zlink::framework::framework_exception_t &ex) {
    duplicate_system_route_rejected =
      ex.kind () == zlink::framework::framework_error_kind_t::request_protocol_error;
  }
  if (!duplicate_system_route_rejected) {
    return 37;
  }

  auto app = zlink::framework::app_t::create ();
  const auto config_path =
    std::filesystem::temp_directory_path () / "zlink_cpp_framework_app.json";
  {
    std::ofstream config (config_path);
    config << R"({"node":"json-node","limits":{"queue":42},"enabled":true})";
  }
  setenv ("ZLINK_REGION", "local", 1);

  app.config ()
    .load_json (config_path.string ())
    .load_env ("ZLINK_");
  const auto log_path =
    std::filesystem::temp_directory_path () / "zlink_cpp_framework_app.log";
  std::vector<zlink::framework::log_record_t> observed_logs;
  app.logging ()
    .use_console ()
    .use_file (log_path.string ())
    .use_callback_sink ([&](const zlink::framework::log_record_t &record) {
      observed_logs.push_back (record);
    })
    .use_async ({ .queue_capacity = 128 })
    .use_backend (zlink::framework::logging_backend_t::spdlog)
    .set_min_level (zlink::framework::log_level_t::debug);
  auto logger = app.logging ().factory ().create ("app-host-test");
  logger.debug ("startup", { { "node", "alpha" } });
  logger.trace ("filtered");

  bool zlink_configured = false;
  correlation_middleware_t::before_count = 0;
  correlation_middleware_t::after_count = 0;
  app.advanced ().use_zlink ([&](zlink::framework::zlink_builder_t &) {
    zlink_configured = true;
  });
  app.health ()
    .add_zlink_runtime_check ()
    .add_channel_check ("games.channel")
    .add_hosted_service_check ("http.host");
  app.add_zlink_framework ([](zlink::framework::zlink_framework_options_t &options) {
    options.services ().add_singleton<http_name_prefix_t> ();
    options.services ().add_scoped<scoped_http_counter_t> ();
    options.http ()
      .listen (ZLINK_FRAMEWORK_HTTP_TEST_HTTP_ENDPOINT)
      .map_health ("/health")
      .map_readiness ("/ready")
      .map_liveness ("/live")
      .map_post<create_game_http_handler_t> ("/games")
      .map_get<create_game_http_handler_t> ("/games/{id}")
      .map_put<create_game_http_handler_t> ("/games/{id}")
      .map_delete<create_game_http_handler_t> ("/games/{id}")
      .map_post<async_game_http_handler_t> ("/async-games")
      .map_post<injected_game_http_handler_t> ("/injected-games")
      .use<correlation_middleware_t> ()
      .use<request_state_middleware_t> ();
  });

  const char *argv_raw[] = { "app", "--node=alpha", "--dry-run" };
  auto **argv = const_cast<char **> (argv_raw);
  int exit_code = -1;
  std::thread app_thread ([&] {
    exit_code = app.run (3, argv);
  });
  auto http_client =
    make_app_host_test_client (ZLINK_FRAMEWORK_HTTP_TEST_HTTP_ENDPOINT);
  if (!wait_for_ready (http_client)) {
    app.stop ();
    app_thread.join ();
    return 13;
  }

  const auto post_result =
    http_client.post ("/games")
      .header ("X-Correlation-Id", "corr-post")
      .body (create_game_http_handler_t::request_type { .name = "post" })
      .submit<create_game_http_handler_t::reply_type> ()
      .result ();
  const auto get_result =
    http_client.get ("/games/1?filter=active")
      .submit<create_game_http_handler_t::reply_type> ()
      .result ();
  const auto put_result =
    http_client.put ("/games/1")
      .body (create_game_http_handler_t::request_type {
        .id = "body-id",
        .name = "put",
        .filter = "body-filter" })
      .submit<create_game_http_handler_t::reply_type> ()
      .result ();
  const auto query_overrides_body_result =
    http_client.put ("/games/1?filter=query-filter")
      .body (create_game_http_handler_t::request_type {
        .id = "body-id",
        .name = "put",
        .filter = "body-filter" })
      .submit<create_game_http_handler_t::reply_type> ()
      .result ();
  const auto delete_result =
    http_client.delete_ ("/games/1")
      .submit<create_game_http_handler_t::reply_type> ()
      .result ();
  const auto method_mismatch_result =
    http_client.post ("/games/1")
      .body (create_game_http_handler_t::request_type { .name = "wrong-method" })
      .submit_raw ()
      .result ();
  const auto missing_route_result =
    http_client.get ("/missing-route")
      .submit_raw ()
      .result ();
  const auto invalid_json_shape_result =
    http_client.post ("/games")
      .body (std::string ("not-an-object"))
      .submit_raw ()
      .result ();
  const auto system_method_mismatch_result =
    http_client.post ("/ready")
      .body (create_game_http_handler_t::request_type { .name = "wrong-method" })
      .submit_raw ()
      .result ();
  const auto timeout_mapping_result =
    http_client.post ("/games")
      .body (create_game_http_handler_t::request_type { .name = "timeout" })
      .submit_raw ()
      .result ();
  const auto shutdown_mapping_result =
    http_client.post ("/games")
      .body (create_game_http_handler_t::request_type { .name = "shutdown" })
      .submit_raw ()
      .result ();
  const auto protocol_mapping_result =
    http_client.post ("/games")
      .body (create_game_http_handler_t::request_type { .name = "protocol" })
      .submit_raw ()
      .result ();
  const auto failed_mapping_result =
    http_client.post ("/games")
      .body (create_game_http_handler_t::request_type { .name = "failed" })
      .submit_raw ()
      .result ();
  const auto async_post_result =
    http_client.post ("/async-games")
      .header ("X-Correlation-Id", "corr-async")
      .body (create_game_http_handler_t::request_type { .name = "async" })
      .submit<create_game_http_handler_t::reply_type> ()
      .result ();
  const auto async_timeout_mapping_result =
    http_client.post ("/async-games")
      .body (create_game_http_handler_t::request_type { .name = "async-timeout" })
      .submit_raw ()
      .result ();
  const auto injected_post_result =
    http_client.post ("/injected-games")
      .body (create_game_http_handler_t::request_type { .name = "handler" })
      .submit<create_game_http_handler_t::reply_type> ()
      .result ();
  const auto injected_second_post_result =
    http_client.post ("/injected-games")
      .body (create_game_http_handler_t::request_type { .name = "handler" })
      .submit<create_game_http_handler_t::reply_type> ()
      .result ();
  const auto short_circuit_result =
    http_client.get ("/games/blocked")
      .submit<create_game_http_handler_t::reply_type> ()
      .result ();
  const auto health_result =
    http_client.get ("/health")
      .submit<health_http_reply_t> ()
      .result ();
  const auto liveness_result =
    http_client.get ("/live")
      .submit<health_http_reply_t> ()
      .result ();
  app.stop ();
  app_thread.join ();

  if (!post_result || post_result.value ().body.name != "post" ||
      post_result.value ().body.correlationId != "corr-post" ||
      post_result.value ().headers.at ("X-Correlation-Id") != "corr-post" ||
      post_result.value ().headers.at ("X-Middleware-Before") != "seen" ||
      post_result.value ().headers.at ("X-Middleware-After") != "seen" ||
      post_result.value ().headers.at ("X-Middleware-State") != "preserved") {
    return 14;
  }
  if (!get_result || get_result.value ().status != 200 ||
      get_result.value ().body.id != "1" ||
      get_result.value ().body.filter != "active" ||
      get_result.value ().headers.at ("X-Context-Path") != "/games/1") {
    return 18;
  }
  if (!put_result || put_result.value ().body.name != "put" ||
      put_result.value ().body.id != "1" ||
      put_result.value ().body.filter != "body-filter") {
    return 19;
  }
  if (!query_overrides_body_result ||
      query_overrides_body_result.value ().body.name != "put" ||
      query_overrides_body_result.value ().body.id != "1" ||
      query_overrides_body_result.value ().body.filter != "query-filter") {
    return 36;
  }
  if (!delete_result || delete_result.value ().status != 200) {
    return 20;
  }
  if (!method_mismatch_result ||
      method_mismatch_result.value ().status != 405) {
    return 21;
  }
  if (!missing_route_result ||
      missing_route_result.value ().status != 404) {
    return 26;
  }
  if (!invalid_json_shape_result ||
      invalid_json_shape_result.value ().status != 400 ||
      invalid_json_shape_result.value ().body.find ("payload_decode_failed") ==
        std::string::npos) {
    return 22;
  }
  if (!system_method_mismatch_result ||
      system_method_mismatch_result.value ().status != 405) {
    return 23;
  }
  if (!timeout_mapping_result ||
      timeout_mapping_result.value ().status != 504 ||
      timeout_mapping_result.value ().body.find ("timeout") ==
        std::string::npos) {
    return 27;
  }
  if (!shutdown_mapping_result ||
      shutdown_mapping_result.value ().status != 503 ||
      shutdown_mapping_result.value ().body.find ("shutdown") ==
        std::string::npos) {
    return 28;
  }
  if (!protocol_mapping_result ||
      protocol_mapping_result.value ().status != 400 ||
      protocol_mapping_result.value ().body.find ("request_protocol_error") ==
        std::string::npos) {
    return 29;
  }
  if (!failed_mapping_result ||
      failed_mapping_result.value ().status != 500 ||
      failed_mapping_result.value ().body.find ("request_failed") ==
        std::string::npos) {
    return 30;
  }
  if (!async_post_result ||
      async_post_result.value ().status != 200 ||
      async_post_result.value ().body.name != "async" ||
      async_post_result.value ().body.correlationId != "corr-async") {
    return 33;
  }
  if (!async_timeout_mapping_result ||
      async_timeout_mapping_result.value ().status != 504 ||
      async_timeout_mapping_result.value ().body.find ("timeout") ==
        std::string::npos) {
    return 34;
  }
  if (!injected_post_result ||
      injected_post_result.value ().status != 200 ||
      injected_post_result.value ().body.name != "di:handler:1" ||
      !injected_second_post_result ||
      injected_second_post_result.value ().status != 200 ||
      injected_second_post_result.value ().body.name != "di:handler:1") {
    return 35;
  }
  if (!short_circuit_result ||
      short_circuit_result.value ().status != 202 ||
      short_circuit_result.value ().body.name != "short") {
    return 24;
  }
  if (!health_result ||
      health_result.value ().body.status != "healthy" ||
      !liveness_result ||
      liveness_result.value ().body.liveness != "healthy") {
    return 25;
  }
  if (correlation_middleware_t::before_count < 4 ||
      correlation_middleware_t::after_count < 4) {
    return 17;
  }

  if (exit_code != 0 || !zlink_configured) {
    return 1;
  }
  if (app.config ().model ().get ("config.json.path") !=
      config_path.string ()) {
    return 2;
  }
  if (app.config ().model ().get ("config.env.prefix") != "ZLINK_") {
    return 3;
  }
  if (app.config ().model ().get ("cli.node") != "alpha") {
    return 4;
  }
  if (app.config ().model ().get ("cli.dry-run") != "true") {
    return 5;
  }
  if (app.config ().model ().get ("node") != "json-node" ||
      app.config ().model ().get ("limits.queue") != "42" ||
      app.config ().model ().get ("enabled") != "true" ||
      app.config ().model ().get ("env.REGION") != "local" ||
      app.config ().model ().get ("host.signal_handlers") != "installed") {
    return 10;
  }
  if (!app.logging ().console_enabled () || app.logging ().level () != "debug") {
    return 6;
  }
  if (!app.logging ().async_enabled () ||
      app.logging ().backend () != zlink::framework::logging_backend_t::spdlog ||
      app.logging ().file_paths ().empty () ||
      observed_logs.size () != 1 ||
      observed_logs.front ().category != "app-host-test" ||
      observed_logs.front ().fields.front ().key != "node" ||
      app.logging ().captured_records ().size () != 1) {
    return 7;
  }
  {
    std::ifstream log_file (log_path);
    std::string line;
    std::getline (log_file, line);
    if (line.find ("startup") == std::string::npos ||
        line.find ("node=alpha") == std::string::npos) {
      return 11;
    }
  }

  struct singleton_service_t {
    int value = 7;
  };
  app.advanced ().services ().add_singleton<singleton_service_t> ();
  auto provider = app.advanced ().services ().build_provider ();
  if (provider.get_required<singleton_service_t> ().value != 7) {
    return 8;
  }

  auto restartable = zlink::framework::app_t::create ();
  restartable.stop ();
  if (restartable.run (1, argv) != 0) {
    return 9;
  }

  bool https_without_tls_rejected = false;
  try {
    auto invalid = zlink::framework::app_t::create ();
    invalid.add_zlink_framework (
      [](zlink::framework::zlink_framework_options_t &options) {
        options.http ().listen (ZLINK_FRAMEWORK_HTTP_TEST_HTTPS_INVALID_ENDPOINT);
      });
  } catch (const zlink::framework::framework_exception_t &error) {
    https_without_tls_rejected =
      error.kind () ==
      zlink::framework::framework_error_kind_t::request_protocol_error;
  }
  if (!https_without_tls_rejected) {
    return 12;
  }

  auto secure = zlink::framework::app_t::create ();
  secure.add_zlink_framework (
    [](zlink::framework::zlink_framework_options_t &options) {
      options.http ()
        .listen (ZLINK_FRAMEWORK_HTTP_TEST_HTTPS_INVALID_ENDPOINT)
        .tls ([](zlink::framework::http_tls_options_builder_t &tls) {
          tls.certificate_file ("server.crt").private_key_file ("server.key");
        })
        .map_post<create_game_http_handler_t> ("/games");
    });

#ifdef ZLINK_FRAMEWORK_HTTP_TEST_WITH_OPENSSL
  auto secure_host = zlink::framework::app_t::create ();
  secure_host.add_zlink_framework (
    [](zlink::framework::zlink_framework_options_t &options) {
      options.http ()
        .listen (ZLINK_FRAMEWORK_HTTP_TEST_HTTPS_ENDPOINT)
        .tls ([](zlink::framework::http_tls_options_builder_t &tls) {
          tls.certificate_file (ZLINK_FRAMEWORK_HTTP_TEST_CERT)
            .private_key_file (ZLINK_FRAMEWORK_HTTP_TEST_KEY);
        })
        .map_readiness ("/ready")
        .map_get<create_game_http_handler_t> ("/games/{id}")
        .map_post<create_game_http_handler_t> ("/games");
    });
  int secure_exit_code = -1;
  std::thread secure_thread ([&] {
    secure_exit_code = secure_host.run (3, argv);
  });
  auto secure_client = make_app_host_test_client (
    ZLINK_FRAMEWORK_HTTP_TEST_HTTPS_CLIENT_BASE_URL,
    std::string (ZLINK_FRAMEWORK_HTTP_TEST_CERT));
  if (!wait_for_ready (secure_client)) {
    secure_host.stop ();
    secure_thread.join ();
    return 15;
  }
  const auto secure_post_result =
    secure_client.post ("/games")
      .body (create_game_http_handler_t::request_type { .name = "secure" })
      .submit<create_game_http_handler_t::reply_type> ()
      .result ();
  secure_host.stop ();
  secure_thread.join ();
  if (secure_exit_code != 0 ||
      !secure_post_result ||
      secure_post_result.value ().body.name != "secure") {
    return 16;
  }
#endif

  return 0;
}
