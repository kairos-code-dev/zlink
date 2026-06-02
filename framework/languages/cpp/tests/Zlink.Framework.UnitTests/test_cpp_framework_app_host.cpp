/* SPDX-License-Identifier: MPL-2.0 */

#include <zlink/framework.hpp>
#include <zlink/http_client.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
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
    return {
      .id = request.id,
      .name = request.name,
      .filter = request.filter,
      .correlationId = context.correlation_id
    };
  }
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

} // namespace

int
main ()
{
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
    options.http ()
      .listen (ZLINK_FRAMEWORK_HTTP_TEST_HTTP_ENDPOINT)
      .map_health ("/health")
      .map_readiness ("/ready")
      .map_liveness ("/live")
      .map_post<create_game_http_handler_t> ("/games")
      .map_get<create_game_http_handler_t> ("/games/{id}")
      .map_put<create_game_http_handler_t> ("/games/{id}")
      .map_delete<create_game_http_handler_t> ("/games/{id}")
      .use<correlation_middleware_t> ();
  });

  const char *argv_raw[] = { "app", "--node=alpha", "--dry-run" };
  auto **argv = const_cast<char **> (argv_raw);
  int exit_code = -1;
  std::thread app_thread ([&] {
    exit_code = app.run (3, argv);
  });
  auto http_client = zlink::http_client::client_t::create ()
                       .base_url (ZLINK_FRAMEWORK_HTTP_TEST_HTTP_ENDPOINT)
                       .json ()
                       .timeout (std::chrono::milliseconds (500))
                       .build ();
  bool http_ready = false;
  for (int attempt = 0; attempt < 100 && !http_ready; ++attempt) {
    auto result = http_client.get ("/ready")
                    .submit<health_http_reply_t> ()
                    .result ();
    http_ready = result.has_value () &&
                 result.value ().body.readiness == "healthy";
    if (!http_ready) {
      std::this_thread::sleep_for (std::chrono::milliseconds (10));
    }
  }
  if (!http_ready) {
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
  const auto delete_result =
    http_client.delete_ ("/games/1")
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
      !get_result || get_result.value ().status != 200 ||
      get_result.value ().body.id != "1" ||
      get_result.value ().body.filter != "active" ||
      !put_result || put_result.value ().body.name != "put" ||
      put_result.value ().body.id != "1" ||
      put_result.value ().body.filter != "body-filter" ||
      !delete_result || delete_result.value ().status != 200 ||
      !short_circuit_result ||
      short_circuit_result.value ().status != 202 ||
      short_circuit_result.value ().body.name != "short" ||
      !health_result ||
      health_result.value ().body.status != "healthy" ||
      !liveness_result ||
      liveness_result.value ().body.liveness != "healthy") {
    return 14;
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
        .map_get<create_game_http_handler_t> ("/games/{id}")
        .map_post<create_game_http_handler_t> ("/games");
    });
  int secure_exit_code = -1;
  std::thread secure_thread ([&] {
    secure_exit_code = secure_host.run (3, argv);
  });
  auto secure_client = zlink::http_client::client_t::create ()
                         .base_url (ZLINK_FRAMEWORK_HTTP_TEST_HTTPS_CLIENT_BASE_URL)
                         .json ()
                         .timeout (std::chrono::milliseconds (500))
                         .trust_certificate_file (ZLINK_FRAMEWORK_HTTP_TEST_CERT)
                         .build ();
  bool secure_ready = false;
  for (int attempt = 0; attempt < 100 && !secure_ready; ++attempt) {
    auto result = secure_client.get ("/games/ready")
                    .submit<create_game_http_handler_t::reply_type> ()
                    .result ();
    secure_ready = result.has_value ();
    if (!secure_ready) {
      std::this_thread::sleep_for (std::chrono::milliseconds (10));
    }
  }
  if (!secure_ready) {
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
