/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#include <zlink/framework.hpp>
#include <zlink/http_client.hpp>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <vector>
#include <string>
#include <thread>
#include <type_traits>

#ifdef _WIN32
#include <process.h>
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

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

static_assert (
  std::is_same_v<decltype (zlink::framework::app_t::create ()), zlink::framework::app_t>);

namespace
{

struct failure_trace_t
{
    const char *phase = "startup";
    bool success = false;

    ~failure_trace_t ()
    {
        if (!success) {
            std::cerr << "app host test failed during phase: " << phase << '\n';
        }
    }
};

std::uint32_t current_process_id () noexcept
{
#ifdef _WIN32
    return static_cast<std::uint32_t> (_getpid ());
#else
    return static_cast<std::uint32_t> (getpid ());
#endif
}

std::uint16_t process_unique_port (std::uint16_t base_port, std::uint16_t salt)
{
    const auto offset = static_cast<std::uint16_t> ((current_process_id () % 1500U) * 23U);
    const auto first = static_cast<std::uint16_t> (base_port + offset + salt);
#ifdef _WIN32
    return first;
#else
    for (std::uint16_t attempt = 0; attempt < 200; ++attempt) {
        const auto candidate = static_cast<std::uint16_t> (first + attempt * 13U);
        const int descriptor = ::socket (AF_INET, SOCK_STREAM, 0);
        if (descriptor < 0) {
            return first;
        }
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons (candidate);
        address.sin_addr.s_addr = htonl (INADDR_LOOPBACK);
        const bool bindable =
          ::bind (descriptor, reinterpret_cast<sockaddr *> (&address), sizeof (address)) == 0;
        ::close (descriptor);
        if (bindable) {
            return candidate;
        }
    }
    return first;
#endif
}

std::uint16_t port_from_endpoint (const std::string &endpoint)
{
    const auto scheme = endpoint.find ("://");
    const auto authority_start = scheme == std::string::npos ? 0 : scheme + 3;
    const auto slash = endpoint.find ('/', authority_start);
    const auto authority = endpoint.substr (
      authority_start, slash == std::string::npos ? std::string::npos : slash - authority_start);
    const auto colon = authority.rfind (':');
    if (colon == std::string::npos || colon + 1 >= authority.size ()) {
        return 0;
    }
    return static_cast<std::uint16_t> (std::stoi (authority.substr (colon + 1)));
}

std::string endpoint_with_port (const std::string &endpoint, std::uint16_t port)
{
    const auto scheme = endpoint.find ("://");
    const auto authority_start = scheme == std::string::npos ? 0 : scheme + 3;
    const auto slash = endpoint.find ('/', authority_start);
    const auto colon = endpoint.rfind (':', slash == std::string::npos ? endpoint.size () : slash);
    if (colon == std::string::npos || colon < authority_start) {
        return endpoint;
    }
    const auto port_end = slash == std::string::npos ? endpoint.size () : slash;
    return endpoint.substr (0, colon + 1) + std::to_string (port) + endpoint.substr (port_end);
}

std::string process_unique_endpoint (const std::string &endpoint, std::uint16_t salt)
{
    return endpoint_with_port (endpoint, process_unique_port (port_from_endpoint (endpoint), salt));
}

bool http_keep_alive_round_trip (const std::string &endpoint)
{
#ifdef _WIN32
    (void) endpoint;
    return true;
#else
    const auto port = port_from_endpoint (endpoint);
    const int descriptor = ::socket (AF_INET, SOCK_STREAM, 0);
    if (descriptor < 0) {
        return false;
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons (port);
    address.sin_addr.s_addr = htonl (INADDR_LOOPBACK);
    if (::connect (descriptor, reinterpret_cast<sockaddr *> (&address), sizeof (address)) != 0) {
        ::close (descriptor);
        return false;
    }
    auto send_all = [descriptor] (const std::string &request) {
        const char *data = request.data ();
        std::size_t remaining = request.size ();
        while (remaining > 0) {
            const auto sent = ::send (descriptor, data, remaining, 0);
            if (sent <= 0) {
                return false;
            }
            data += sent;
            remaining -= static_cast<std::size_t> (sent);
        }
        return true;
    };
    auto recv_until = [descriptor] (std::string &response, const std::string &needle) {
        char buffer[4096];
        while (response.find (needle) == std::string::npos) {
            const auto received = ::recv (descriptor, buffer, sizeof (buffer), 0);
            if (received <= 0) {
                return false;
            }
            response.append (buffer, static_cast<std::size_t> (received));
        }
        return true;
    };
    std::string response;
    if (!send_all (
          "GET /numbers/11?page=1 HTTP/1.1\r\nHost: localhost\r\nConnection: keep-alive\r\n\r\n")
        || !recv_until (response, R"("id":11)")) {
        ::close (descriptor);
        return false;
    }
    if (!send_all (
          "GET /numbers/12?page=2 HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n")
        || !recv_until (response, R"("id":12)")) {
        ::close (descriptor);
        return false;
    }
    ::close (descriptor);
    return response.find ("HTTP/1.1 200 OK") != std::string::npos
           && response.find (R"("id":11)") != std::string::npos
           && response.find (R"("id":12)") != std::string::npos;
#endif
}

bool http_raw_request_contains (const std::string &endpoint,
                                const std::string &request,
                                const std::string &needle)
{
#ifdef _WIN32
    (void) endpoint;
    (void) request;
    (void) needle;
    return true;
#else
    const auto port = port_from_endpoint (endpoint);
    const int descriptor = ::socket (AF_INET, SOCK_STREAM, 0);
    if (descriptor < 0) {
        return false;
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons (port);
    address.sin_addr.s_addr = htonl (INADDR_LOOPBACK);
    if (::connect (descriptor, reinterpret_cast<sockaddr *> (&address), sizeof (address)) != 0) {
        ::close (descriptor);
        return false;
    }
    const char *data = request.data ();
    std::size_t remaining = request.size ();
    while (remaining > 0) {
        const auto sent = ::send (descriptor, data, remaining, 0);
        if (sent <= 0) {
            ::close (descriptor);
            return false;
        }
        data += sent;
        remaining -= static_cast<std::size_t> (sent);
    }
    std::string response;
    char buffer[4096];
    while (response.find (needle) == std::string::npos) {
        const auto received = ::recv (descriptor, buffer, sizeof (buffer), 0);
        if (received <= 0) {
            break;
        }
        response.append (buffer, static_cast<std::size_t> (received));
    }
    ::close (descriptor);
    return response.find (needle) != std::string::npos;
#endif
}

struct create_game_http_handler_t
{
    struct request_type
    {
        std::string id;
        std::string name;
        std::string filter;
        std::string correlationId;

        void validate (const zlink::framework::http_context_t &context) const
        {
            if (context.method == zlink::framework::http_method_t::post && name.empty ()) {
                throw zlink::framework::framework_exception_t (
                  zlink::framework::framework_error_kind_t::request_protocol_error,
                  "name is required");
            }
            if (name == "invalid") {
                throw zlink::framework::framework_exception_t (
                  zlink::framework::framework_error_kind_t::request_protocol_error,
                  "name is invalid");
            }
        }
    };

    struct reply_type
    {
        std::string id;
        std::string name;
        std::string filter;
        std::string correlationId;
    };

    reply_type handle (const request_type &request, zlink::framework::http_context_t &context)
    {
        if (request.name == "timeout") {
            throw zlink::framework::detail::make_boundary_exception (zlink::framework::detail::boundary_error_t::timed_out, "handler timeout");
        }
        if (request.name == "shutdown") {
            throw zlink::framework::detail::make_boundary_exception (zlink::framework::detail::boundary_error_t::shutdown, "handler shutdown");
        }
        if (request.name == "protocol") {
            throw zlink::framework::framework_exception_t (
              zlink::framework::framework_error_kind_t::request_protocol_error,
              "handler protocol error");
        }
        if (request.name == "failed") {
            throw zlink::framework::framework_exception_t (
              zlink::framework::framework_error_kind_t::request_failed, "handler failure");
        }
        return {.id = request.id,
                .name = request.name,
                .filter = request.filter,
                .correlationId = context.correlation_id};
    }
};

struct async_game_http_handler_t
{
    using request_type = create_game_http_handler_t::request_type;
    using reply_type = create_game_http_handler_t::reply_type;

    zlink::framework::task_t<reply_type> handle (const request_type &request,
                                                 zlink::framework::http_context_t &context)
    {
        if (request.name == "async-timeout") {
            co_return zlink::framework::detail::boundary_failure<reply_type> (zlink::framework::detail::boundary_error_t::timed_out, "async handler timeout");
        }
        co_return reply_type{.id = request.id,
                             .name = request.name,
                             .filter = request.filter,
                             .correlationId = context.correlation_id};
    }
};

struct nested_app_http_handler_t
{
    using request_type = create_game_http_handler_t::request_type;
    using reply_type = create_game_http_handler_t::reply_type;

    reply_type handle (const request_type &request)
    {
        class stop_service_t final : public zlink::framework::hosted_service_t
        {
          public:
            explicit stop_service_t (zlink::framework::app_t &app) : _app (app) {}

            void start (zlink::framework::service_provider_t &) override { _app.stop (); }

            void stop () noexcept override {}

          private:
            zlink::framework::app_t &_app;
        };

        auto nested = zlink::framework::app_t::create ();
        nested.add_zlink_framework ([] (zlink::framework::zlink_framework_options_t &) {});
        nested.add_hosted_service (std::make_unique<stop_service_t> (nested));
        const auto exit_code = nested.run (0, nullptr);
        if (exit_code != 0) {
            throw zlink::framework::framework_exception_t (
              zlink::framework::framework_error_kind_t::request_failed,
              "nested app failed");
        }
        return {.id = request.id, .name = "nested:" + request.name};
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

struct auth_policy_t
{
    std::string token = "Bearer test-token";
};

struct app_host_config_options_t
{
    std::string endpoint;
    int queue_limit = 0;
    bool enabled = false;

    static app_host_config_options_t bind (const zlink::framework::configuration_section_t &section)
    {
        return {.endpoint = section.require ("endpoint"),
                .queue_limit = std::stoi (section.require ("queue")),
                .enabled = section.require ("enabled") == "true"};
    }
};

struct injected_game_http_handler_t
{
    using request_type = create_game_http_handler_t::request_type;
    using reply_type = create_game_http_handler_t::reply_type;
    using dependency_types =
      zlink::framework::dependency_list_t<http_name_prefix_t, scoped_http_counter_t>;

    explicit injected_game_http_handler_t (http_name_prefix_t &prefix,
                                           scoped_http_counter_t &counter) :
        _prefix (&prefix), _counter (&counter)
    {
    }

    reply_type handle (const request_type &request, zlink::framework::http_context_t &context)
    {
        return {.id = request.id,
                .name = _prefix->value + request.name + ":" + std::to_string (++_counter->count),
                .filter = request.filter,
                .correlationId = context.correlation_id};
    }

  private:
    http_name_prefix_t *_prefix;
    scoped_http_counter_t *_counter;
};

struct number_http_handler_t
{
    struct request_type
    {
        int id = 0;
        int page = 0;
    };

    struct reply_type
    {
        int id = 0;
        int page = 0;
    };

    reply_type handle (const request_type &request)
    {
        return {.id = request.id, .page = request.page};
    }
};

struct response_game_http_handler_t
{
    using request_type = create_game_http_handler_t::request_type;
    using reply_type = create_game_http_handler_t::reply_type;

    zlink::framework::http_response_t handle (const request_type &request,
                                              const zlink::framework::http_request_t &http,
                                              zlink::framework::http_context_t &)
    {
        zlink::framework::http_response_t response;
        response.status = 202;
        response.body =
          zlink::message_t::from_json (reply_type{.id = request.id,
                                                  .name = "response:" + request.name,
                                                  .filter = request.filter,
                                                  .correlationId = http.correlation_id})
            .to_string ();
        response.header ("X-Http-Target", http.target);
        return response;
    }
};

struct response_context_http_handler_t
{
    using request_type = create_game_http_handler_t::request_type;
    using reply_type = create_game_http_handler_t::reply_type;

    zlink::framework::http_response_t handle (const request_type &request,
                                              zlink::framework::http_context_t &context)
    {
        context.json_response (
          299, R"({"id":"context","name":"context","filter":"","correlationId":"context"})");
        zlink::framework::http_response_t response;
        response.status = 207;
        response.body =
          zlink::message_t::from_json (reply_type{.id = request.id,
                                                  .name = "response-context:" + request.name,
                                                  .filter = request.filter,
                                                  .correlationId = context.correlation_id})
            .to_string ();
        response.header ("X-Response-Wins", "true");
        return response;
    }
};

struct request_task_http_handler_t
{
    using request_type = create_game_http_handler_t::request_type;
    using reply_type = create_game_http_handler_t::reply_type;

    zlink::framework::task_t<reply_type> handle (const request_type &request,
                                                 const zlink::framework::http_request_t &http)
    {
        co_return reply_type{.id = http.route_values.at ("id"),
                             .name = "request-task:" + request.name,
                             .filter = http.query_values.at ("filter"),
                             .correlationId = http.correlation_id};
    }
};

struct raw_echo_http_handler_t
{
    zlink::framework::http_response_t handle (const zlink::framework::http_request_t &request)
    {
        zlink::framework::http_response_t response;
        response.status = 206;
        response.content_type = "text/plain";
        response.body = request.route_values.at ("id") + "|" + request.query_values.at ("mode")
                        + "|" + request.content_type + "|" + request.body;
        response.header ("X-Raw-Path", request.path);
        return response;
    }
};

struct raw_async_http_handler_t
{
    zlink::framework::task_t<zlink::framework::http_response_t>
    handle (const zlink::framework::http_request_t &request)
    {
        zlink::framework::http_response_t response;
        response.status = 208;
        response.content_type = "text/plain";
        response.body =
          "raw-async|" + request.route_values.at ("id") + "|" + request.query_values.at ("mode");
        co_return response;
    }
};

int parse_required_int_field (const nlohmann::json &json, const char *field, const char *message)
{
    try {
        const auto text = json.at (field).get<std::string> ();
        std::size_t consumed = 0;
        const auto parsed = std::stoi (text, &consumed);
        if (consumed == text.size ()) {
            return parsed;
        }
    }
    catch (const std::exception &) {
    }
    throw zlink::framework::framework_exception_t (
      zlink::framework::framework_error_kind_t::payload_decode_failed, message);
}

void to_json (nlohmann::json &json, const create_game_http_handler_t::request_type &value)
{
    json = nlohmann::json{{"id", value.id},
                          {"name", value.name},
                          {"filter", value.filter},
                          {"correlationId", value.correlationId}};
}

void from_json (const nlohmann::json &json, create_game_http_handler_t::request_type &value)
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

void to_json (nlohmann::json &json, const create_game_http_handler_t::reply_type &value)
{
    json = nlohmann::json{{"id", value.id},
                          {"name", value.name},
                          {"filter", value.filter},
                          {"correlationId", value.correlationId}};
}

void from_json (const nlohmann::json &json, create_game_http_handler_t::reply_type &value)
{
    value.id = json.value ("id", "");
    value.name = json.value ("name", "");
    value.filter = json.value ("filter", "");
    value.correlationId = json.value ("correlationId", "");
}

void to_json (nlohmann::json &json, const number_http_handler_t::request_type &value)
{
    json = nlohmann::json{{"id", std::to_string (value.id)}, {"page", std::to_string (value.page)}};
}

void from_json (const nlohmann::json &json, number_http_handler_t::request_type &value)
{
    value.id = parse_required_int_field (json, "id", "invalid route id");
    value.page = parse_required_int_field (json, "page", "invalid query page");
}

void to_json (nlohmann::json &json, const number_http_handler_t::reply_type &value)
{
    json = nlohmann::json{{"id", value.id}, {"page", value.page}};
}

void from_json (const nlohmann::json &json, number_http_handler_t::reply_type &value)
{
    value.id = json.value ("id", 0);
    value.page = json.value ("page", 0);
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

    void before (zlink::framework::http_context_t &) { before_seen = true; }

    void after (zlink::framework::http_context_t &context)
    {
        if (before_seen) {
            context.response_header ("X-Middleware-State", "preserved");
        }
    }
};

struct auth_middleware_t
{
    explicit auth_middleware_t (auth_policy_t &policy) : _policy (&policy) {}

    void before (zlink::framework::http_context_t &context)
    {
        if (context.path.rfind ("/secure-games/", 0) != 0) {
            return;
        }
        const auto found = context.request_headers.find ("authorization");
        if (found == context.request_headers.end () || found->second != _policy->token) {
            context.json_response (
              401, R"({"error":"unauthorized","message":"missing or invalid token"})");
        }
    }

  private:
    auth_policy_t *_policy;
};

struct health_http_reply_t
{
    std::string status;
    std::string readiness;
    std::string liveness;
};

void from_json (const nlohmann::json &json, health_http_reply_t &value)
{
    value.status = json.value ("status", "");
    value.readiness = json.value ("readiness", "");
    value.liveness = json.value ("liveness", "");
}

zlink::http_client::client_t
make_app_host_test_client (std::string base_url,
                           std::optional<std::string> trust_certificate_file = std::nullopt)
{
    auto builder = zlink::http_client::client_t::create ()
                     .base_url (std::move (base_url))
                     .timeout (std::chrono::milliseconds (500));
    if (trust_certificate_file) {
        builder.trust_certificate_file (std::move (*trust_certificate_file));
    }
    return builder.build ();
}

bool wait_for_ready (const zlink::http_client::client_t &client,
                     std::string *last_error = nullptr)
{
    for (int attempt = 0; attempt < 100; ++attempt) {
        auto result = client.get ("/ready").async<health_http_reply_t> ().result ();
        if (result.has_value () && result.value ().body.readiness == "healthy") {
            return true;
        }
        if (last_error != nullptr) {
            if (result.error ()) {
                *last_error = result.error ()->what ();
            } else if (result.has_value ()) {
                *last_error = "readiness returned status "
                              + std::to_string (result.value ().status);
            }
        }
        std::this_thread::sleep_for (std::chrono::milliseconds (10));
    }
    return false;
}

bool wait_for_raw_status (const zlink::http_client::client_t &client, std::string path, int status)
{
    for (int attempt = 0; attempt < 100; ++attempt) {
        auto result = client.get (path).async_raw ().result ();
        if (result.has_value () && result.value ().status == status) {
            return true;
        }
        std::this_thread::sleep_for (std::chrono::milliseconds (10));
    }
    return false;
}

} // namespace

int main ()
{
    failure_trace_t trace;
    bool duplicate_route_rejected = false;
    try {
        auto duplicate_app = zlink::framework::app_t::create ();
        duplicate_app.add_zlink_framework (
          [] (zlink::framework::zlink_framework_options_t &options) {
              options.http ()
                .listen ("http://127.0.0.1:18081")
                .map_get<create_game_http_handler_t> ("/duplicate")
                .map_get<create_game_http_handler_t> ("/duplicate");
          });
    }
    catch (const zlink::framework::framework_exception_t &ex) {
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
          [] (zlink::framework::zlink_framework_options_t &options) {
              options.http ()
                .listen ("http://127.0.0.1:18082")
                .map_readiness ("/ready")
                .map_get<create_game_http_handler_t> ("/ready");
          });
    }
    catch (const zlink::framework::framework_exception_t &ex) {
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
          [] (zlink::framework::zlink_framework_options_t &options) {
              options.http ()
                .listen ("http://127.0.0.1:18083")
                .map_health ("/status")
                .map_readiness ("/status");
          });
    }
    catch (const zlink::framework::framework_exception_t &ex) {
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
        config
          << R"({"node":"json-node","server":{"endpoint":"tcp://json:7100","queue":42,"enabled":true}})";
    }
    setenv ("ZLINK_REGION", "local", 1);
    setenv ("ZLINK_server__queue", "64", 1);

    app.config ()
      .use_environment ("development")
      .load_json (config_path.string ())
      .load_env ("ZLINK_");
    auto default_environment_config = zlink::framework::config_builder_t{};
    if (default_environment_config.environment () != "production"
        || !default_environment_config.is_environment ("Production")) {
        return 41;
    }
    auto optional_config = zlink::framework::config_builder_t{};
    optional_config.load_json ("missing.optional.appsettings.json",
                               zlink::framework::optional_t::yes);
    bool missing_required_json_rejected = false;
    try {
        auto required_config = zlink::framework::config_builder_t{};
        required_config.load_json ("missing.required.appsettings.json");
    }
    catch (const zlink::framework::framework_exception_t &ex) {
        missing_required_json_rejected =
          ex.kind () == zlink::framework::framework_error_kind_t::request_protocol_error;
    }
    if (!missing_required_json_rejected) {
        return 40;
    }
    const auto log_path = std::filesystem::temp_directory_path () / "zlink_cpp_framework_app.log";
    const auto rotating_log_path =
      std::filesystem::temp_directory_path () / "zlink_cpp_framework_app_rotating.log";
    {
        std::ofstream existing_rotating_log (rotating_log_path);
        existing_rotating_log << "old";
    }
    std::vector<zlink::framework::log_record_t> observed_logs;
    app.logging ()
      .use_console ()
      .use_file (log_path.string ())
      .use_rotating_file (rotating_log_path.string (), {.max_file_size = 1, .max_files = 2})
      .use_callback_sink (
        [&] (const zlink::framework::log_record_t &record) { observed_logs.push_back (record); })
      .use_async ({.queue_capacity = 128})
      .use_backend (zlink::framework::logging_backend_t::structured)
      .set_min_level (zlink::framework::log_level_t::debug);
    auto logger = app.logging ().factory ().create ("app-host-test");
    logger.debug ("startup", {{"node", "alpha"}});
    logger.trace ("filtered");

    bool zlink_configured = false;
    correlation_middleware_t::before_count = 0;
    correlation_middleware_t::after_count = 0;
    const auto http_endpoint = process_unique_endpoint (ZLINK_FRAMEWORK_HTTP_TEST_HTTP_ENDPOINT, 0);
    const auto https_invalid_endpoint =
      process_unique_endpoint (ZLINK_FRAMEWORK_HTTP_TEST_HTTPS_INVALID_ENDPOINT, 1);
    const auto https_endpoint =
      process_unique_endpoint (ZLINK_FRAMEWORK_HTTP_TEST_HTTPS_ENDPOINT, 2);
    const auto https_client_base_url = endpoint_with_port (
      ZLINK_FRAMEWORK_HTTP_TEST_HTTPS_CLIENT_BASE_URL, port_from_endpoint (https_endpoint));
    (void) app.advanced ().zlink ();
    zlink_configured = true;
    app.health ()
      .add_zlink_runtime_check ()
      .add_channel_check ("games.channel")
      .add_hosted_service_check ("http.host");
    app.add_zlink_framework ([&] (zlink::framework::zlink_framework_options_t &options) {
        options.services ().add_singleton<http_name_prefix_t> ();
        options.services ().add_singleton<auth_policy_t> ();
        options.services ().add_scoped<scoped_http_counter_t> ();
        options.services ().add_scoped<auth_middleware_t, auth_policy_t> ();
        options.http ()
          .configure_server ([] (zlink::framework::http_server_options_builder_t &server) {
              server.set_max_connections (64)
                .set_max_request_body_size (1024 * 1024)
                .set_max_header_size (64 * 1024)
                .set_request_headers_timeout (std::chrono::milliseconds (5000))
                .set_request_body_timeout (std::chrono::milliseconds (5000))
                .set_write_timeout (std::chrono::milliseconds (5000))
                .set_keep_alive_timeout (std::chrono::milliseconds (5000))
                .set_graceful_shutdown_timeout (std::chrono::milliseconds (5000))
                .set_max_keep_alive_requests (100);
          })
          .listen (http_endpoint)
          .map_health ("/health")
          .map_readiness ("/ready")
          .map_liveness ("/live")
          .map_post<create_game_http_handler_t> ("/games")
          .map_post<nested_app_http_handler_t> ("/nested-app")
          .map_get<create_game_http_handler_t> ("/games/{id}")
          .map_put<create_game_http_handler_t> ("/games/{id}")
          .map_delete<create_game_http_handler_t> ("/games/{id}")
          .map_get<number_http_handler_t> ("/numbers/{id}")
          .map_get<response_game_http_handler_t> ("/response-games/{id}")
          .map_get<response_context_http_handler_t> ("/response-context-games/{id}")
          .map_get<request_task_http_handler_t> ("/request-task-games/{id}")
          .map_post<raw_echo_http_handler_t> ("/raw/{id}")
          .map_post<raw_async_http_handler_t> ("/raw-async/{id}")
          .map_get<create_game_http_handler_t> ("/secure-games/{id}")
          .map_post<async_game_http_handler_t> ("/async-games")
          .map_post<injected_game_http_handler_t> ("/injected-games")
          .use<correlation_middleware_t> ()
          .use<request_state_middleware_t> ()
          .use<auth_middleware_t> ();
    });

    const char *argv_raw[] = {"app", "--node=alpha", "--server.endpoint=tcp://cli:7100",
                              "--dry-run"};
    auto **argv = const_cast<char **> (argv_raw);
    int exit_code = -1;
    std::thread app_thread ([&] { exit_code = app.run (4, argv); });
    auto http_client = make_app_host_test_client (http_endpoint);
    std::string readiness_error;
    trace.phase = "main app readiness";
    if (!wait_for_ready (http_client, &readiness_error)) {
        std::cerr << "main HTTP app did not become ready at " << http_endpoint
                  << ": " << readiness_error << '\n';
        app.stop ();
        app_thread.join ();
        return 13;
    }

    trace.phase = "main app http requests";
    const auto post_result = http_client.post ("/games")
                               .header ("X-Correlation-Id", "corr-post")
                               .body (create_game_http_handler_t::request_type{.name = "post"})
                               .async<create_game_http_handler_t::reply_type> ()
                               .result ();
    const auto nested_result = http_client.post ("/nested-app")
                                 .body (create_game_http_handler_t::request_type{.name = "app"})
                                 .async<create_game_http_handler_t::reply_type> ()
                                 .result ();
    const auto post_after_nested_result =
      http_client.post ("/games")
        .body (create_game_http_handler_t::request_type{.name = "after-nested"})
        .async<create_game_http_handler_t::reply_type> ()
        .result ();
    const auto get_result = http_client.get ("/games/1?filter=active")
                              .async<create_game_http_handler_t::reply_type> ()
                              .result ();
    const auto number_get_result =
      http_client.get ("/numbers/41?page=2").async<number_http_handler_t::reply_type> ().result ();
    const auto response_object_result =
      http_client.get ("/response-games/9?filter=direct&name=object")
        .async<create_game_http_handler_t::reply_type> ()
        .result ();
    const auto response_context_result =
      http_client.get ("/response-context-games/10?filter=context&name=object")
        .async<create_game_http_handler_t::reply_type> ()
        .result ();
    const auto request_task_result =
      http_client.get ("/request-task-games/13?filter=request&name=task")
        .async<create_game_http_handler_t::reply_type> ()
        .result ();
    const auto raw_result = http_client.post ("/raw/42?mode=echo")
                              .header ("content-type", "text/plain")
                              .body (std::string ("raw body"))
                              .async_raw ()
                              .result ();
    const auto raw_async_result = http_client.post ("/raw-async/43?mode=echo")
                                    .header ("content-type", "text/plain")
                                    .body (std::string ("raw body"))
                                    .async_raw ()
                                    .result ();
    const auto secure_get_result = http_client.get ("/secure-games/7")
                                     .header ("authorization", "Bearer test-token")
                                     .async<create_game_http_handler_t::reply_type> ()
                                     .result ();
    const auto unauthorized_get_result =
      http_client.get ("/secure-games/7").async_raw ().result ();
    const auto put_result = http_client.put ("/games/1")
                              .body (create_game_http_handler_t::request_type{
                                .id = "body-id", .name = "put", .filter = "body-filter"})
                              .async<create_game_http_handler_t::reply_type> ()
                              .result ();
    const auto query_overrides_body_result =
      http_client.put ("/games/1?filter=query-filter")
        .body (create_game_http_handler_t::request_type{
          .id = "body-id", .name = "put", .filter = "body-filter"})
        .async<create_game_http_handler_t::reply_type> ()
        .result ();
    const auto delete_result =
      http_client.delete_ ("/games/1").async<create_game_http_handler_t::reply_type> ().result ();
    const auto method_mismatch_result =
      http_client.post ("/games/1")
        .body (create_game_http_handler_t::request_type{.name = "wrong-method"})
        .async_raw ()
        .result ();
    const auto missing_route_result = http_client.get ("/missing-route").async_raw ().result ();
    const auto invalid_json_shape_result = http_client.post ("/games")
                                             .header ("X-Correlation-Id", "corr-invalid-json")
                                             .body (std::string ("not-an-object"))
                                             .async_raw ()
                                             .result ();
    const auto route_parse_failure_result =
      http_client.get ("/numbers/not-a-number?page=2").async_raw ().result ();
    const auto query_parse_failure_result =
      http_client.get ("/numbers/41?page=bad").async_raw ().result ();
    const auto missing_required_field_result = http_client.post ("/games")
                                                 .body (create_game_http_handler_t::request_type{})
                                                 .async_raw ()
                                                 .result ();
    const auto dto_validation_result =
      http_client.post ("/games")
        .body (create_game_http_handler_t::request_type{.name = "invalid"})
        .async_raw ()
        .result ();
    const auto unsupported_content_type_result =
      http_client.post ("/games")
        .body (create_game_http_handler_t::request_type{.name = "plain"})
        .header ("content-type", "text/plain")
        .async_raw ()
        .result ();
    const auto system_method_mismatch_result =
      http_client.post ("/ready")
        .body (create_game_http_handler_t::request_type{.name = "wrong-method"})
        .async_raw ()
        .result ();
    const auto timeout_mapping_result =
      http_client.post ("/games")
        .header ("X-Correlation-Id", "corr-timeout")
        .body (create_game_http_handler_t::request_type{.name = "timeout"})
        .async_raw ()
        .result ();
    const auto shutdown_mapping_result =
      http_client.post ("/games")
        .body (create_game_http_handler_t::request_type{.name = "shutdown"})
        .async_raw ()
        .result ();
    const auto protocol_mapping_result =
      http_client.post ("/games")
        .body (create_game_http_handler_t::request_type{.name = "protocol"})
        .async_raw ()
        .result ();
    const auto failed_mapping_result =
      http_client.post ("/games")
        .body (create_game_http_handler_t::request_type{.name = "failed"})
        .async_raw ()
        .result ();
    const auto async_post_result =
      http_client.post ("/async-games")
        .header ("X-Correlation-Id", "corr-async")
        .body (create_game_http_handler_t::request_type{.name = "async"})
        .async<create_game_http_handler_t::reply_type> ()
        .result ();
    const auto async_timeout_mapping_result =
      http_client.post ("/async-games")
        .body (create_game_http_handler_t::request_type{.name = "async-timeout"})
        .async_raw ()
        .result ();
    const auto injected_post_result =
      http_client.post ("/injected-games")
        .body (create_game_http_handler_t::request_type{.name = "handler"})
        .async<create_game_http_handler_t::reply_type> ()
        .result ();
    const auto injected_second_post_result =
      http_client.post ("/injected-games")
        .body (create_game_http_handler_t::request_type{.name = "handler"})
        .async<create_game_http_handler_t::reply_type> ()
        .result ();
    const auto short_circuit_result = http_client.get ("/games/blocked")
                                        .async<create_game_http_handler_t::reply_type> ()
                                        .result ();
    const auto health_result = http_client.get ("/health").async<health_http_reply_t> ().result ();
    const auto liveness_result = http_client.get ("/live").async<health_http_reply_t> ().result ();
    const bool keep_alive_ok = http_keep_alive_round_trip (http_endpoint);
    trace.phase = "main app stop";
    app.stop ();
    app_thread.join ();

    trace.phase = "main app assertions";
    if (!post_result || post_result.value ().body.name != "post"
        || post_result.value ().body.correlationId != "corr-post"
        || post_result.value ().headers.at ("x-correlation-id") != "corr-post"
        || post_result.value ().headers.at ("x-middleware-before") != "seen"
        || post_result.value ().headers.at ("x-middleware-after") != "seen"
        || post_result.value ().headers.at ("x-middleware-state") != "preserved") {
        return 14;
    }
    if (!nested_result || nested_result.value ().body.name != "nested:app"
        || !post_after_nested_result
        || post_after_nested_result.value ().body.name != "after-nested") {
        return 62;
    }
    if (!get_result || get_result.value ().status != 200 || get_result.value ().body.id != "1"
        || get_result.value ().body.filter != "active"
        || get_result.value ().headers.at ("x-context-path") != "/games/1") {
        return 18;
    }
    if (!number_get_result || number_get_result.value ().body.id != 41
        || number_get_result.value ().body.page != 2) {
        return 46;
    }
    if (!response_object_result || response_object_result.value ().status != 202
        || response_object_result.value ().body.id != "9"
        || response_object_result.value ().body.name != "response:object"
        || response_object_result.value ().body.filter != "direct"
        || response_object_result.value ().headers.at ("x-http-target")
             != "/response-games/9?filter=direct&name=object") {
        return 57;
    }
    if (!response_context_result || response_context_result.value ().status != 207
        || response_context_result.value ().body.id != "10"
        || response_context_result.value ().body.name != "response-context:object"
        || response_context_result.value ().headers.at ("x-response-wins") != "true") {
        return 61;
    }
    if (!request_task_result || request_task_result.value ().status != 200
        || request_task_result.value ().body.id != "13"
        || request_task_result.value ().body.name != "request-task:task"
        || request_task_result.value ().body.filter != "request") {
        return 62;
    }
    if (!raw_result || raw_result.value ().status != 206
        || raw_result.value ().headers.at ("x-raw-path") != "/raw/42"
        || raw_result.value ().body.find ("42|echo|text/plain|") == std::string::npos) {
        return 58;
    }
    if (!raw_async_result || raw_async_result.value ().status != 208
        || raw_async_result.value ().body.find ("raw-async|43|echo") == std::string::npos) {
        return 63;
    }
    if (!secure_get_result || secure_get_result.value ().status != 200
        || secure_get_result.value ().body.id != "7") {
        return 49;
    }
    if (!unauthorized_get_result || unauthorized_get_result.value ().status != 401
        || unauthorized_get_result.value ().body.find ("unauthorized") == std::string::npos) {
        return 50;
    }
    if (!put_result || put_result.value ().body.name != "put" || put_result.value ().body.id != "1"
        || put_result.value ().body.filter != "body-filter") {
        return 19;
    }
    if (!query_overrides_body_result || query_overrides_body_result.value ().body.name != "put"
        || query_overrides_body_result.value ().body.id != "1"
        || query_overrides_body_result.value ().body.filter != "query-filter") {
        return 36;
    }
    if (!delete_result || delete_result.value ().status != 200) {
        return 20;
    }
    if (!method_mismatch_result || method_mismatch_result.value ().status != 405) {
        return 21;
    }
    if (!missing_route_result || missing_route_result.value ().status != 404) {
        return 26;
    }
    if (!invalid_json_shape_result || invalid_json_shape_result.value ().status != 400
        || invalid_json_shape_result.value ().body.find ("payload_decode_failed")
             == std::string::npos
        || invalid_json_shape_result.value ().body.find ("corr-invalid-json") == std::string::npos
        || invalid_json_shape_result.value ().headers.at ("x-middleware-after") != "seen") {
        return 22;
    }
    if (!route_parse_failure_result || route_parse_failure_result.value ().status != 400
        || route_parse_failure_result.value ().body.find ("payload_decode_failed")
             == std::string::npos
        || route_parse_failure_result.value ().body.find ("invalid route id")
             == std::string::npos) {
        return 47;
    }
    if (!query_parse_failure_result || query_parse_failure_result.value ().status != 400
        || query_parse_failure_result.value ().body.find ("payload_decode_failed")
             == std::string::npos
        || query_parse_failure_result.value ().body.find ("invalid query page")
             == std::string::npos) {
        return 48;
    }
    if (!missing_required_field_result || missing_required_field_result.value ().status != 400
        || missing_required_field_result.value ().body.find ("name is required")
             == std::string::npos) {
        return 43;
    }
    if (!dto_validation_result || dto_validation_result.value ().status != 400
        || dto_validation_result.value ().body.find ("name is invalid") == std::string::npos) {
        return 44;
    }
    if (!unsupported_content_type_result || unsupported_content_type_result.value ().status != 400
        || unsupported_content_type_result.value ().body.find ("unsupported content type")
             == std::string::npos) {
        return 45;
    }
    if (!system_method_mismatch_result || system_method_mismatch_result.value ().status != 405) {
        return 23;
    }
    if (!timeout_mapping_result || timeout_mapping_result.value ().status != 504
        || timeout_mapping_result.value ().body.find ("timeout") == std::string::npos
        || timeout_mapping_result.value ().body.find ("corr-timeout") == std::string::npos
        || timeout_mapping_result.value ().headers.at ("x-middleware-after") != "seen") {
        return 27;
    }
    if (!shutdown_mapping_result || shutdown_mapping_result.value ().status != 503
        || shutdown_mapping_result.value ().body.find ("shutdown") == std::string::npos) {
        return 28;
    }
    if (!protocol_mapping_result || protocol_mapping_result.value ().status != 400
        || protocol_mapping_result.value ().body.find ("request_protocol_error")
             == std::string::npos) {
        return 29;
    }
    if (!failed_mapping_result || failed_mapping_result.value ().status != 500
        || failed_mapping_result.value ().body.find ("request_failed") == std::string::npos) {
        return 30;
    }
    if (!async_post_result || async_post_result.value ().status != 200
        || async_post_result.value ().body.name != "async"
        || async_post_result.value ().body.correlationId != "corr-async") {
        return 33;
    }
    if (!async_timeout_mapping_result || async_timeout_mapping_result.value ().status != 504
        || async_timeout_mapping_result.value ().body.find ("timeout") == std::string::npos) {
        return 34;
    }
    if (!injected_post_result || injected_post_result.value ().status != 200
        || injected_post_result.value ().body.name != "di:handler:1" || !injected_second_post_result
        || injected_second_post_result.value ().status != 200
        || injected_second_post_result.value ().body.name != "di:handler:1") {
        return 35;
    }
    if (!short_circuit_result || short_circuit_result.value ().status != 202
        || short_circuit_result.value ().body.name != "short") {
        return 24;
    }
    if (!health_result || health_result.value ().body.status != "healthy" || !liveness_result
        || liveness_result.value ().body.liveness != "healthy") {
        return 25;
    }
    if (!keep_alive_ok) {
        return 59;
    }
    if (correlation_middleware_t::before_count < 4 || correlation_middleware_t::after_count < 4) {
        return 17;
    }

    if (exit_code != 0 || !zlink_configured) {
        return 1;
    }
    if (app.config ().model ().get ("config.json.path") != config_path.string ()) {
        return 2;
    }
    if (app.config ().model ().get ("config.env.prefix") != "ZLINK_") {
        return 3;
    }
    if (app.config ().environment () != "development"
        || !app.config ().is_environment ("Development")
        || app.config ().is_environment ("production")) {
        return 42;
    }
    if (app.config ().model ().get ("cli.node") != "alpha") {
        return 4;
    }
    if (app.config ().model ().get ("cli.dry-run") != "true") {
        return 5;
    }
    if (app.config ().model ().get ("node") != "alpha"
        || app.config ().model ().get ("server.endpoint") != "tcp://cli:7100"
        || app.config ().model ().get ("server.queue") != "64"
        || app.config ().model ().get ("server.enabled") != "true"
        || app.config ().model ().get ("env.REGION") != "local"
        || app.config ().model ().get ("env.server__queue") != "64"
        || app.config ().model ().get ("host.signal_handlers") != "installed") {
        return 10;
    }
    const auto server_options = app.config ().bind_required<app_host_config_options_t> ("server");
    if (server_options.endpoint != "tcp://cli:7100" || server_options.queue_limit != 64
        || !server_options.enabled) {
        return 38;
    }
    bool missing_required_value_rejected = false;
    app.config ().model ().set ("broken.endpoint", "tcp://broken:7100");
    try {
        (void) app.config ().bind_required<app_host_config_options_t> ("broken");
    }
    catch (const zlink::framework::framework_exception_t &ex) {
        missing_required_value_rejected =
          ex.kind () == zlink::framework::framework_error_kind_t::request_protocol_error;
    }
    if (!missing_required_value_rejected) {
        return 39;
    }
    if (!app.logging ().console_enabled () || app.logging ().level () != "debug") {
        return 6;
    }
    if (!app.logging ().async_enabled ()
        || app.logging ().backend () != zlink::framework::logging_backend_t::structured
        || app.logging ().file_paths ().empty () || observed_logs.size () != 1
        || observed_logs.front ().category != "app-host-test"
        || observed_logs.front ().fields.front ().key != "node"
        || app.logging ().captured_records ().size () != 1) {
        return 7;
    }
    {
        std::ifstream log_file (log_path);
        std::string line;
        std::getline (log_file, line);
        if (line.find ("startup") == std::string::npos
            || line.find ("node=alpha") == std::string::npos) {
            return 11;
        }
    }
    if (!std::filesystem::exists (rotating_log_path.string () + ".1")) {
        return 38;
    }
    {
        std::ifstream rotated_file (rotating_log_path.string () + ".1");
        std::string rotated_line;
        std::getline (rotated_file, rotated_line);
        if (rotated_line.find ("old") == std::string::npos) {
            return 39;
        }
    }
    {
        std::ifstream current_rotating_log (rotating_log_path);
        std::string line;
        std::getline (current_rotating_log, line);
        if (line.find ("startup") == std::string::npos) {
            return 40;
        }
    }

    struct singleton_service_t
    {
        int value = 7;
    };
    app.advanced ().services ().add_singleton<singleton_service_t> ();
    auto provider = app.advanced ().services ().build_provider ();
    if (provider.get_required<singleton_service_t> ().value != 7) {
        return 8;
    }
    provider.get_required<zlink::framework::logger_factory_t> ()
      .create ("app-default-logger")
      .info ("resolved");
    if (app.logging ().captured_records ().size () != 2
        || app.logging ().captured_records ().back ().category != "app-default-logger") {
        return 42;
    }

    auto restartable = zlink::framework::app_t::create ();
    restartable.stop ();
    if (restartable.run (1, argv) != 0) {
        return 9;
    }

    auto unhealthy_app = zlink::framework::app_t::create ();
    const auto unhealthy_endpoint =
      process_unique_endpoint (ZLINK_FRAMEWORK_HTTP_TEST_HTTP_ENDPOINT, 3);
    unhealthy_app.health ()
      .add_channel_check ("games.channel")
      .set_status ("games.channel", zlink::framework::health_status_t::unhealthy,
                   "channel unavailable");
    unhealthy_app.add_zlink_framework ([&] (zlink::framework::zlink_framework_options_t &options) {
        options.http ().listen (unhealthy_endpoint).map_readiness ("/ready").map_liveness ("/live");
    });
    int unhealthy_exit_code = -1;
    std::thread unhealthy_thread ([&] { unhealthy_exit_code = unhealthy_app.run (1, argv); });
    auto unhealthy_client = make_app_host_test_client (unhealthy_endpoint);
    trace.phase = "unhealthy app readiness";
    if (!wait_for_raw_status (unhealthy_client, "/ready", 503)) {
        unhealthy_app.stop ();
        unhealthy_thread.join ();
        return 51;
    }
    if (!wait_for_raw_status (unhealthy_client, "/live", 200)) {
        unhealthy_app.stop ();
        unhealthy_thread.join ();
        return 52;
    }
    unhealthy_app.stop ();
    unhealthy_thread.join ();
    if (unhealthy_exit_code != 0) {
        return 53;
    }

    auto not_live_app = zlink::framework::app_t::create ();
    const auto not_live_endpoint =
      process_unique_endpoint (ZLINK_FRAMEWORK_HTTP_TEST_HTTP_ENDPOINT, 4);
    not_live_app.health ()
      .add_hosted_service_check ("games.service")
      .set_status ("games.service", zlink::framework::health_status_t::unhealthy,
                   "hosted service unavailable");
    not_live_app.add_zlink_framework ([&] (zlink::framework::zlink_framework_options_t &options) {
        options.http ().listen (not_live_endpoint).map_readiness ("/ready").map_liveness ("/live");
    });
    int not_live_exit_code = -1;
    std::thread not_live_thread ([&] { not_live_exit_code = not_live_app.run (1, argv); });
    auto not_live_client = make_app_host_test_client (not_live_endpoint);
    trace.phase = "not-live app readiness";
    if (!wait_for_raw_status (not_live_client, "/ready", 503)) {
        not_live_app.stop ();
        not_live_thread.join ();
        return 54;
    }
    if (!wait_for_raw_status (not_live_client, "/live", 503)) {
        not_live_app.stop ();
        not_live_thread.join ();
        return 55;
    }
    not_live_app.stop ();
    not_live_thread.join ();
    if (not_live_exit_code != 0) {
        return 56;
    }

    auto limited_app = zlink::framework::app_t::create ();
    const auto limited_endpoint =
      process_unique_endpoint (ZLINK_FRAMEWORK_HTTP_TEST_HTTP_ENDPOINT, 5);
    limited_app.add_zlink_framework ([&] (zlink::framework::zlink_framework_options_t &options) {
        options.http ()
          .configure_server ([] (zlink::framework::http_server_options_builder_t &server) {
              server.set_max_request_body_size (4).set_max_header_size (256);
          })
          .listen (limited_endpoint)
          .map_readiness ("/ready")
          .map_post<raw_echo_http_handler_t> ("/raw/{id}");
    });
    int limited_exit_code = -1;
    std::thread limited_thread ([&] { limited_exit_code = limited_app.run (1, argv); });
    auto limited_client = make_app_host_test_client (limited_endpoint);
    trace.phase = "limited app raw rejection";
    const bool limited_ready = wait_for_ready (limited_client);
    const bool limited_rejected =
      limited_ready
      && http_raw_request_contains (limited_endpoint,
                                    "POST /raw/1?mode=echo HTTP/1.1\r\nHost: "
                                    "localhost\r\nContent-Type: text/plain\r\nContent-Length: "
                                    "9\r\nConnection: close\r\n\r\ntoo-large",
                                    "413 Payload Too Large");
    const std::string large_header_request =
      "POST /raw/1?mode=echo HTTP/1.1\r\nHost: localhost\r\nX-Large: " + std::string (2048, 'x')
      + "\r\nContent-Type: text/plain\r\nContent-Length: 2\r\nConnection: close\r\n\r\nok";
    const bool large_header_rejected =
      limited_ready
      && http_raw_request_contains (limited_endpoint, large_header_request,
                                    "431 Request Header Fields Too Large");
    limited_app.stop ();
    limited_thread.join ();
    if (!limited_rejected || !large_header_rejected || limited_exit_code != 0) {
        return 60;
    }

    auto expect_https_tls_validation_rejected = [] (auto configure_http) {
        bool rejected = false;
        try {
            auto invalid = zlink::framework::app_t::create ();
            invalid.add_zlink_framework (
              [&] (zlink::framework::zlink_framework_options_t &options) {
                  configure_http (options.http ());
              });
        }
        catch (const zlink::framework::framework_exception_t &error) {
            rejected =
              error.kind () == zlink::framework::framework_error_kind_t::request_protocol_error;
        }
        return rejected;
    };

    if (!expect_https_tls_validation_rejected (
          [&] (zlink::framework::http_options_builder_t &http) {
              http.listen (https_invalid_endpoint);
          })) {
        return 12;
    }

    if (!expect_https_tls_validation_rejected (
          [&] (zlink::framework::http_options_builder_t &http) {
              http.listen (https_invalid_endpoint)
                .configure_tls ([] (zlink::framework::http_tls_options_builder_t &tls) {
                    tls.private_key_file ("server.key");
                });
          })) {
        return 41;
    }

    if (!expect_https_tls_validation_rejected (
          [&] (zlink::framework::http_options_builder_t &http) {
              http.listen (https_invalid_endpoint)
                .configure_tls ([] (zlink::framework::http_tls_options_builder_t &tls) {
                    tls.certificate_file ("server.crt");
                });
          })) {
        return 42;
    }

    if (!expect_https_tls_validation_rejected (
          [&] (zlink::framework::http_options_builder_t &http) {
              http.listen (https_invalid_endpoint)
                .configure_tls ([] (zlink::framework::http_tls_options_builder_t &) {});
          })) {
        return 43;
    }

    bool http_with_tls_accepted = true;
    try {
        auto invalid = zlink::framework::app_t::create ();
        invalid.add_zlink_framework ([&] (zlink::framework::zlink_framework_options_t &options) {
            options.http ()
              .listen (http_endpoint)
              .configure_tls ([] (zlink::framework::http_tls_options_builder_t &) {});
        });
    }
    catch (const zlink::framework::framework_exception_t &) {
        http_with_tls_accepted = false;
    }
    if (!http_with_tls_accepted) {
        return 44;
    }

    auto secure = zlink::framework::app_t::create ();
    secure.add_zlink_framework ([&] (zlink::framework::zlink_framework_options_t &options) {
        options.http ()
          .listen (https_invalid_endpoint)
          .configure_tls ([] (zlink::framework::http_tls_options_builder_t &tls) {
              tls.certificate_file ("server.crt").private_key_file ("server.key");
          })
          .map_post<create_game_http_handler_t> ("/games");
    });

#ifdef ZLINK_FRAMEWORK_HTTP_TEST_WITH_OPENSSL
    auto secure_host = zlink::framework::app_t::create ();
    secure_host.add_zlink_framework ([&] (zlink::framework::zlink_framework_options_t &options) {
        options.http ()
          .listen (https_endpoint)
          .configure_tls ([] (zlink::framework::http_tls_options_builder_t &tls) {
              tls.certificate_file (ZLINK_FRAMEWORK_HTTP_TEST_CERT)
                .private_key_file (ZLINK_FRAMEWORK_HTTP_TEST_KEY);
          })
          .map_readiness ("/ready")
          .map_get<create_game_http_handler_t> ("/games/{id}")
          .map_post<create_game_http_handler_t> ("/games");
    });
    int secure_exit_code = -1;
    std::thread secure_thread ([&] { secure_exit_code = secure_host.run (3, argv); });
    auto secure_client = make_app_host_test_client (https_client_base_url,
                                                    std::string (ZLINK_FRAMEWORK_HTTP_TEST_CERT));
    trace.phase = "secure app readiness";
    if (!wait_for_ready (secure_client)) {
        secure_host.stop ();
        secure_thread.join ();
        return 15;
    }
    const auto secure_post_result =
      secure_client.post ("/games")
        .body (create_game_http_handler_t::request_type{.name = "secure"})
        .async<create_game_http_handler_t::reply_type> ()
        .result ();
    secure_host.stop ();
    secure_thread.join ();
    if (secure_exit_code != 0 || !secure_post_result
        || secure_post_result.value ().body.name != "secure") {
        return 16;
    }
#endif

    trace.success = true;
    return 0;
}
