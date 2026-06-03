/* SPDX-License-Identifier: MPL-2.0 */

#include "runtime/http/http_host_service.hpp"

#include "runtime/dispatch/coroutine_executor.hpp"

#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

#ifdef ZLINK_FRAMEWORK_HTTP_WITH_OPENSSL
#include <boost/asio/ssl/stream.hpp>
#include <boost/beast/ssl.hpp>
#endif

#include <algorithm>
#include <atomic>
#include <cctype>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace zlink::framework::runtime
{
class http_route_invoker_access_t
{
public:
  static task_t<std::string> invoke (const http_route_t &route,
                                     service_provider_t &services,
                                     http_context_t &context,
                                     const std::string &body)
  {
    auto owned_body = body;
    return handler_coroutine_executor ().submit<std::string> (
      [&route,
       &services,
       &context,
       owned_body = std::move (owned_body)]()
        -> boost::asio::awaitable<result_t<std::string>> {
        try {
          auto route_task = route.invoke_json (
            services,
            context,
            owned_body);
          co_return result_t<std::string>::success (
            (co_await await_task_result (std::move (route_task))).value ());
        } catch (const framework_exception_t &error) {
          co_return result_t<std::string>::failure (
            error.kind (), error.what (), error.is_retriable ());
        } catch (...) {
          co_return result_t<std::string>::failure (
            framework_error_kind_t::request_failed,
            "HTTP route handler threw an exception");
        }
      });
  }
};

namespace
{
namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
using tcp = asio::ip::tcp;

std::atomic_uint64_t g_correlation_sequence { 1 };

struct parsed_endpoint_t
{
  std::string scheme;
  std::string host;
  std::string port;
};

struct matched_route_t
{
  const http_route_t *route = nullptr;
  std::map<std::string, std::string> route_values;
  std::map<std::string, std::string> query_values;
  bool method_not_allowed = false;
};

struct middleware_invocation_t
{
  const http_middleware_t *middleware = nullptr;
  std::shared_ptr<void> instance;
};

enum class health_route_kind_t
{
  health,
  readiness,
  liveness
};

bool
starts_with (const std::string &value, const char *prefix)
{
  return value.rfind (prefix, 0) == 0;
}

parsed_endpoint_t
parse_endpoint (const std::string &uri)
{
  parsed_endpoint_t parsed;
  std::string rest;
  if (starts_with (uri, "http://")) {
    parsed.scheme = "http";
    rest = uri.substr (7);
  } else if (starts_with (uri, "https://")) {
    parsed.scheme = "https";
    rest = uri.substr (8);
  } else {
    throw framework_exception_t (
      framework_error_kind_t::request_protocol_error,
      "HTTP endpoint must start with http:// or https://");
  }

  const auto slash = rest.find ('/');
  const auto authority = slash == std::string::npos ? rest : rest.substr (0, slash);
  if (!authority.empty () && authority.front () == '[') {
    const auto close = authority.find (']');
    if (close == std::string::npos) {
      throw framework_exception_t (
        framework_error_kind_t::request_protocol_error,
        "HTTP endpoint IPv6 host is missing ]");
    }
    parsed.host = authority.substr (1, close - 1);
    if (close + 1 < authority.size ()) {
      if (authority[close + 1] != ':') {
        throw framework_exception_t (
          framework_error_kind_t::request_protocol_error,
          "HTTP endpoint has invalid IPv6 authority");
      }
      parsed.port = authority.substr (close + 2);
    } else {
      parsed.port = parsed.scheme == "https" ? "443" : "80";
    }
  } else {
    const auto colon = authority.rfind (':');
    if (colon == std::string::npos) {
      parsed.host = authority;
      parsed.port = parsed.scheme == "https" ? "443" : "80";
    } else {
      parsed.host = authority.substr (0, colon);
      parsed.port = authority.substr (colon + 1);
    }
  }
  if (parsed.host.empty () || parsed.port.empty ()) {
    throw framework_exception_t (
      framework_error_kind_t::request_protocol_error,
      "HTTP endpoint requires host and port");
  }
  return parsed;
}

std::optional<http_method_t>
from_beast_method (http::verb method)
{
  switch (method) {
  case http::verb::get:
    return http_method_t::get;
  case http::verb::post:
    return http_method_t::post;
  case http::verb::put:
    return http_method_t::put;
  case http::verb::delete_:
    return http_method_t::delete_;
  default:
    return std::nullopt;
  }
}

bool
is_hex (char value) noexcept
{
  return std::isxdigit (static_cast<unsigned char> (value)) != 0;
}

int
hex_value (char value) noexcept
{
  if (value >= '0' && value <= '9') {
    return value - '0';
  }
  if (value >= 'a' && value <= 'f') {
    return 10 + value - 'a';
  }
  if (value >= 'A' && value <= 'F') {
    return 10 + value - 'A';
  }
  return 0;
}

std::string
url_decode (std::string_view value)
{
  std::string decoded;
  decoded.reserve (value.size ());
  for (std::size_t index = 0; index < value.size (); ++index) {
    if (value[index] == '+') {
      decoded.push_back (' ');
      continue;
    }
    if (value[index] == '%' && index + 2 < value.size () &&
        is_hex (value[index + 1]) && is_hex (value[index + 2])) {
      decoded.push_back (
        static_cast<char> ((hex_value (value[index + 1]) << 4) |
                           hex_value (value[index + 2])));
      index += 2;
      continue;
    }
    decoded.push_back (value[index]);
  }
  return decoded;
}

std::vector<std::string>
split_path (const std::string &path)
{
  std::vector<std::string> parts;
  std::istringstream stream (path);
  std::string part;
  while (std::getline (stream, part, '/')) {
    if (!part.empty ()) {
      parts.push_back (part);
    }
  }
  return parts;
}

std::map<std::string, std::string>
parse_query (const std::string &target)
{
  std::map<std::string, std::string> values;
  const auto query_start = target.find ('?');
  if (query_start == std::string::npos) {
    return values;
  }

  std::string query = target.substr (query_start + 1);
  std::istringstream stream (query);
  std::string pair;
  while (std::getline (stream, pair, '&')) {
    if (pair.empty ()) {
      continue;
    }
    const auto separator = pair.find ('=');
    if (separator == std::string::npos) {
      values[url_decode (pair)] = "true";
      continue;
    }
    values[url_decode (std::string_view (pair).substr (0, separator))] =
      url_decode (std::string_view (pair).substr (separator + 1));
  }
  return values;
}

bool
header_name_equals (std::string_view left, std::string_view right)
{
  if (left.size () != right.size ()) {
    return false;
  }
  for (std::size_t index = 0; index < left.size (); ++index) {
    if (std::tolower (static_cast<unsigned char> (left[index])) !=
        std::tolower (static_cast<unsigned char> (right[index]))) {
      return false;
    }
  }
  return true;
}

std::optional<std::string>
find_header (const std::map<std::string, std::string> &headers,
             std::string_view name)
{
  for (const auto &[key, value] : headers) {
    if (header_name_equals (key, name)) {
      return value;
    }
  }
  return std::nullopt;
}

bool
route_matches (const std::string &pattern,
               const std::string &target,
               std::map<std::string, std::string> &route_values)
{
  if (pattern == target) {
    return true;
  }
  const auto pattern_parts = split_path (pattern);
  const auto target_parts = split_path (target);
  if (pattern_parts.size () != target_parts.size ()) {
    return false;
  }
  for (std::size_t index = 0; index < pattern_parts.size (); ++index) {
    const auto &pattern_part = pattern_parts[index];
    const auto &target_part = target_parts[index];
    const bool parameter =
      pattern_part.size () >= 3 &&
      pattern_part.front () == '{' &&
      pattern_part.back () == '}';
    if (parameter) {
      route_values[pattern_part.substr (1, pattern_part.size () - 2)] =
        url_decode (target_part);
      continue;
    }
    if (pattern_part != target_part) {
      return false;
    }
  }
  return true;
}

matched_route_t
find_route (const http_options_snapshot_t &options,
            std::optional<http_method_t> method,
            const std::string &target)
{
  const auto query = target.find ('?');
  const auto path = query == std::string::npos ? target : target.substr (0, query);
  bool path_matches = false;
  for (const auto &route : options.routes) {
    std::map<std::string, std::string> route_values;
    if (!route_matches (route.path, path, route_values)) {
      continue;
    }
    path_matches = true;
    if (method && route.method == *method) {
      return matched_route_t {
        &route,
        std::move (route_values),
        parse_query (target) };
    }
  }
  matched_route_t match;
  match.method_not_allowed = path_matches;
  return match;
}

const char *
status_name (health_status_t status) noexcept
{
  switch (status) {
  case health_status_t::healthy:
    return "healthy";
  case health_status_t::degraded:
    return "degraded";
  case health_status_t::unhealthy:
    return "unhealthy";
  }
  return "unhealthy";
}

nlohmann::json
to_json (const health_check_result_t &check)
{
  return nlohmann::json {
    { "name", check.name },
    { "component", check.component },
    { "status", status_name (check.status) },
    { "message", check.message }
  };
}

std::optional<health_route_kind_t>
match_health_route (const http_options_snapshot_t &options,
                    std::optional<http_method_t> method,
                    const std::string &target)
{
  if (!method || *method != http_method_t::get) {
    return std::nullopt;
  }
  const auto query = target.find ('?');
  const auto path = query == std::string::npos ? target : target.substr (0, query);
  if (options.health_path && *options.health_path == path) {
    return health_route_kind_t::health;
  }
  if (options.readiness_path && *options.readiness_path == path) {
    return health_route_kind_t::readiness;
  }
  if (options.liveness_path && *options.liveness_path == path) {
    return health_route_kind_t::liveness;
  }
  return std::nullopt;
}

bool
system_route_path_exists (const http_options_snapshot_t &options,
                          const std::string &target)
{
  const auto query = target.find ('?');
  const auto path = query == std::string::npos ? target : target.substr (0, query);
  return (options.health_path && *options.health_path == path) ||
         (options.readiness_path && *options.readiness_path == path) ||
         (options.liveness_path && *options.liveness_path == path);
}

std::string
bind_http_request_body (
  const std::string &body,
  const std::map<std::string, std::string> &route_values,
  const std::map<std::string, std::string> &query_values)
{
  nlohmann::json json = nlohmann::json::object ();
  if (!body.empty ()) {
    try {
      json = nlohmann::json::parse (body);
      if (!json.is_object ()) {
        throw framework_exception_t (
          framework_error_kind_t::payload_decode_failed,
          "HTTP JSON body must be an object");
      }
    } catch (const framework_exception_t &) {
      throw;
    } catch (const std::exception &ex) {
      throw framework_exception_t (
        framework_error_kind_t::payload_decode_failed,
        ex.what ());
    }
  }
  for (const auto &[key, value] : query_values) {
    json[key] = value;
  }
  for (const auto &[key, value] : route_values) {
    json[key] = value;
  }
  return json.dump ();
}

std::map<std::string, std::string>
copy_headers (const http::request<http::string_body> &request)
{
  std::map<std::string, std::string> headers;
  for (const auto &field : request) {
    headers.emplace (std::string (field.name_string ()),
                     std::string (field.value ()));
  }
  return headers;
}

http_context_t
make_context (std::optional<http_method_t> method,
              const http::request<http::string_body> &request)
{
  http_context_t context;
  context.method = method.value_or (http_method_t::get);
  const auto target = std::string (request.target ());
  const auto query = target.find ('?');
  context.path = query == std::string::npos ? target : target.substr (0, query);
  context.request_headers = copy_headers (request);
  if (auto correlation_id =
        find_header (context.request_headers, "x-correlation-id")) {
    context.correlation_id = *correlation_id;
  } else if (auto request_id =
               find_header (context.request_headers, "x-request-id")) {
    context.correlation_id = *request_id;
  } else {
    context.correlation_id =
      "http-" + std::to_string (
        g_correlation_sequence.fetch_add (1, std::memory_order_relaxed));
  }
  context.response_header ("X-Correlation-Id", context.correlation_id);
  return context;
}

void
apply_context_response (http::response<http::string_body> &response,
                        const http_context_t &context,
                        bool allow_status_override = true)
{
  if (allow_status_override &&
      context.response_status >= 100 && context.response_status <= 599) {
    response.result (static_cast<unsigned> (context.response_status));
  }
  for (const auto &[name, value] : context.response_headers) {
    response.set (name, value);
  }
}

const char *
error_kind_name (framework_error_kind_t kind) noexcept
{
  switch (kind) {
  case framework_error_kind_t::payload_decode_failed:
    return "payload_decode_failed";
  case framework_error_kind_t::request_target_not_found:
    return "request_target_not_found";
  case framework_error_kind_t::request_protocol_error:
    return "request_protocol_error";
  case framework_error_kind_t::timeout:
    return "timeout";
  case framework_error_kind_t::shutdown:
    return "shutdown";
  case framework_error_kind_t::request_failed:
    return "request_failed";
  default:
    return "request_failed";
  }
}

http::status
status_for_error (framework_error_kind_t kind) noexcept
{
  switch (kind) {
  case framework_error_kind_t::payload_decode_failed:
  case framework_error_kind_t::request_protocol_error:
    return http::status::bad_request;
  case framework_error_kind_t::request_target_not_found:
    return http::status::not_found;
  case framework_error_kind_t::timeout:
    return http::status::gateway_timeout;
  case framework_error_kind_t::shutdown:
    return http::status::service_unavailable;
  default:
    return http::status::internal_server_error;
  }
}

void
apply_framework_error (http::response<http::string_body> &response,
                       const framework_exception_t &error,
                       const http_context_t &context)
{
  response.result (status_for_error (error.kind ()));
  response.body () =
    nlohmann::json {
      { "error", error_kind_name (error.kind ()) },
      { "message", error.what () },
      { "correlationId", context.correlation_id }
    }.dump ();
  apply_context_response (response, context, false);
}

http::response<http::string_body>
make_health_response (health_builder_t &health,
                      health_route_kind_t kind,
                      const http::request<http::string_body> &request,
                      http_context_t &context)
{
  const auto report = health.report ();
  health_status_t status = report.status;
  if (kind == health_route_kind_t::readiness) {
    status = report.readiness;
  } else if (kind == health_route_kind_t::liveness) {
    status = report.liveness;
  }

  nlohmann::json checks = nlohmann::json::array ();
  for (const auto &check : report.checks) {
    checks.push_back (to_json (check));
  }
  nlohmann::json body {
    { "status", status_name (report.status) },
    { "readiness", status_name (report.readiness) },
    { "liveness", status_name (report.liveness) },
    { "checks", checks }
  };

  http::response<http::string_body> response {
    status == health_status_t::unhealthy
      ? http::status::service_unavailable
      : http::status::ok,
    request.version ()
  };
  response.set (http::field::content_type, "application/json");
  response.body () = body.dump ();
  apply_context_response (response, context, false);
  response.prepare_payload ();
  return response;
}

http::response<http::string_body>
handle_request (const http_options_snapshot_t &options,
                service_provider_t &services,
                health_builder_t &health,
                const http::request<http::string_body> &request)
{
  http::response<http::string_body> response { http::status::ok,
                                               request.version () };
  response.set (http::field::content_type, "application/json");
  const auto method = from_beast_method (request.method ());
  auto context = make_context (method, request);
  if ((!method || *method != http_method_t::get) &&
      system_route_path_exists (options, std::string (request.target ()))) {
    response.result (http::status::method_not_allowed);
    response.body () = R"({"error":"method not allowed"})";
    apply_context_response (response, context, false);
    response.prepare_payload ();
    return response;
  }
  if (const auto health_route =
        match_health_route (options,
                            method,
                            std::string (request.target ()))) {
    return make_health_response (health, *health_route, request, context);
  }
  const auto match = find_route (options,
                                 method,
                                 std::string (request.target ()));
  if (match.route == nullptr) {
    if (match.method_not_allowed) {
      response.result (http::status::method_not_allowed);
      response.body () = R"({"error":"method not allowed"})";
    } else {
      response.result (http::status::not_found);
      response.body () = R"({"error":"route not found"})";
    }
    apply_context_response (response, context, false);
  } else {
    auto request_scope =
      services.create_scope (service_scope_kind_t::handler_invocation);
    auto &request_services = request_scope.provider ();
    std::vector<middleware_invocation_t> middleware_invocations;
    middleware_invocations.reserve (options.middleware.size ());
    bool after_middleware_ran = false;
    auto run_after_middleware = [&]() {
      if (after_middleware_ran) {
        return;
      }
      after_middleware_ran = true;
      for (auto it = middleware_invocations.rbegin ();
           it != middleware_invocations.rend ();
           ++it) {
        if (it->middleware != nullptr && it->middleware->after) {
          it->middleware->after (request_services, context, it->instance);
        }
      }
    };
    try {
      for (const auto &middleware : options.middleware) {
        auto &invocation = middleware_invocations.emplace_back ();
        invocation.middleware = &middleware;
        if (middleware.create_instance) {
          invocation.instance = middleware.create_instance ();
        }
        if (middleware.before) {
          middleware.before (request_services, context, invocation.instance);
        }
      }
      if (context.response_body) {
        response.body () = *context.response_body;
      } else {
        const auto bound_body = bind_http_request_body (
          request.body (),
          match.route_values,
          match.query_values);
        auto route_result =
          http_route_invoker_access_t::invoke (
            *match.route,
            request_services,
            context,
            bound_body).result ();
        if (!route_result) {
          throw *route_result.error ();
        }
        response.body () = route_result.value ();
      }
      run_after_middleware ();
      apply_context_response (response, context);
    } catch (const framework_exception_t &ex) {
      run_after_middleware ();
      apply_framework_error (response, ex, context);
    } catch (const std::exception &ex) {
      run_after_middleware ();
      response.result (http::status::internal_server_error);
      response.body () =
        nlohmann::json {
          { "error", "request_failed" },
          { "message", ex.what () },
          { "correlationId", context.correlation_id }
        }.dump ();
      apply_context_response (response, context, false);
    }
  }
  response.prepare_payload ();
  return response;
}

} // namespace

class http_host_service_t::listener_t
{
public:
  listener_t (const http_endpoint_t &endpoint,
              const http_options_snapshot_t &options,
              health_builder_t &health,
              service_provider_t &services,
              std::atomic_bool &stop)
    : _endpoint (&endpoint),
      _options (&options),
      _health (&health),
      _services (&services),
      _stop (&stop),
      _acceptor (_io)
  {
  }

  void run ()
  {
    _parsed = parse_endpoint (_endpoint->uri);
    tcp::resolver resolver (_io);
    const auto endpoints = resolver.resolve (_parsed.host, _parsed.port);
    _acceptor.open (endpoints.begin ()->endpoint ().protocol ());
    _acceptor.set_option (tcp::acceptor::reuse_address (true));
    _acceptor.bind (endpoints.begin ()->endpoint ());
    _acceptor.listen ();

    while (!_stop->load (std::memory_order_acquire)) {
      beast::error_code ec;
      tcp::socket socket (_io);
      _acceptor.accept (socket, ec);
      if (ec) {
        continue;
      }
      if (_parsed.scheme == "https") {
        handle_https (std::move (socket));
      } else {
        handle_http (std::move (socket));
      }
    }
  }

  void stop () noexcept
  {
    try {
      beast::error_code ignored;
      tcp::socket wakeup (_io);
      wakeup.connect (tcp::endpoint (asio::ip::make_address (_parsed.host),
                                     static_cast<unsigned short> (
                                       std::stoi (_parsed.port))),
                      ignored);
    } catch (...) {
    }
    beast::error_code ignored;
    _acceptor.close (ignored);
  }

private:
  template<typename TStream>
  void serve_request (TStream &stream)
  {
    beast::flat_buffer buffer;
    http::request<http::string_body> request;
    beast::error_code ec;
    http::read (stream, buffer, request, ec);
    if (ec) {
      return;
    }
    auto response = handle_request (*_options, *_services, *_health, request);
    http::write (stream, response, ec);
  }

  void handle_http (tcp::socket socket)
  {
    beast::error_code ec;
    serve_request (socket);
    socket.shutdown (tcp::socket::shutdown_send, ec);
  }

  void handle_https (tcp::socket socket)
  {
#ifdef ZLINK_FRAMEWORK_HTTP_WITH_OPENSSL
    asio::ssl::context context (asio::ssl::context::tls_server);
    context.use_certificate_chain_file (_endpoint->tls->certificate_file);
    context.use_private_key_file (_endpoint->tls->private_key_file,
                                  asio::ssl::context::pem);
    asio::ssl::stream<tcp::socket> stream (std::move (socket), context);
    beast::error_code ec;
    stream.handshake (asio::ssl::stream_base::server, ec);
    if (ec) {
      return;
    }
    serve_request (stream);
    stream.shutdown (ec);
#else
    (void) socket;
#endif
  }

  const http_endpoint_t *_endpoint;
  const http_options_snapshot_t *_options;
  health_builder_t *_health;
  service_provider_t *_services;
  std::atomic_bool *_stop;
  parsed_endpoint_t _parsed;
  asio::io_context _io;
  tcp::acceptor _acceptor;
};

http_host_service_t::http_host_service_t (http_options_snapshot_t options,
                                          health_builder_t &health)
  : _options (std::move (options)),
    _health (&health)
{
}

http_host_service_t::~http_host_service_t () = default;

void
http_host_service_t::start (service_provider_t &services)
{
  _stop.store (false, std::memory_order_release);
  for (const auto &endpoint : _options.endpoints) {
    auto listener =
      std::make_unique<listener_t> (
        endpoint,
        _options,
        *_health,
        services,
        _stop);
    auto *raw = listener.get ();
    _listeners.push_back (std::move (listener));
    _threads.emplace_back ([raw] {
      raw->run ();
    });
  }
}

void
http_host_service_t::stop () noexcept
{
  _stop.store (true, std::memory_order_release);
  for (auto &listener : _listeners) {
    listener->stop ();
  }
  for (auto &thread : _threads) {
    if (thread.joinable ()) {
      thread.join ();
    }
  }
  _threads.clear ();
  _listeners.clear ();
}

} // namespace zlink::framework::runtime
