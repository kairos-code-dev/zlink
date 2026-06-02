/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/framework/contracts/configuration/services.hpp>
#include <zlink/framework/contracts/errors/error.hpp>

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <type_traits>
#include <typeinfo>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace zlink::framework
{

namespace runtime
{
class http_route_invoker_access_t;
} // namespace runtime

enum class http_method_t
{
  get,
  post,
  put,
  delete_
};

struct http_context_t
{
  http_method_t method = http_method_t::get;
  std::string path;
  std::string correlation_id;
  std::map<std::string, std::string> request_headers;
  std::map<std::string, std::string> response_headers;
  std::optional<std::string> response_body;
  int response_status = 200;

  http_context_t &response_header (std::string name, std::string value)
  {
    response_headers[std::move (name)] = std::move (value);
    return *this;
  }

  http_context_t &json_response (int status, std::string body)
  {
    response_status = status;
    response_body = std::move (body);
    return *this;
  }
};

struct http_tls_options_t
{
  std::string certificate_file;
  std::string private_key_file;
};

struct http_middleware_t
{
  std::string name;
  std::function<void (service_provider_t &, http_context_t &)> before;
  std::function<void (service_provider_t &, http_context_t &)> after;
};

class http_route_t
{
public:
  http_method_t method;
  std::string path;
  std::string handler_name;

private:
  std::function<std::string (service_provider_t &,
                             http_context_t &,
                             const std::string &)> invoke_json;

  friend class http_options_builder_t;
  friend class runtime::http_route_invoker_access_t;
};

struct http_endpoint_t
{
  std::string uri;
  std::optional<http_tls_options_t> tls;
};

struct http_options_snapshot_t
{
  std::vector<http_endpoint_t> endpoints;
  std::vector<http_route_t> routes;
  std::vector<http_middleware_t> middleware;
  std::optional<std::string> health_path;
  std::optional<std::string> readiness_path;
  std::optional<std::string> liveness_path;
};

class http_tls_options_builder_t
{
public:
  explicit http_tls_options_builder_t (http_tls_options_t &options) noexcept
    : _options (&options)
  {
  }

  http_tls_options_builder_t &certificate_file (std::string path)
  {
    _options->certificate_file = std::move (path);
    return *this;
  }

  http_tls_options_builder_t &private_key_file (std::string path)
  {
    _options->private_key_file = std::move (path);
    return *this;
  }

private:
  http_tls_options_t *_options;
};

class http_options_builder_t
{
public:
  http_options_builder_t &listen (std::string endpoint)
  {
    if (!starts_with (endpoint, "http://") &&
        !starts_with (endpoint, "https://")) {
      throw framework_exception_t (
        framework_error_kind_t::request_protocol_error,
        "HTTP endpoint must start with http:// or https://");
    }
    _snapshot.endpoints.push_back ({ .uri = std::move (endpoint) });
    return *this;
  }

  http_options_builder_t &tls (
    std::function<void (http_tls_options_builder_t &)> configure)
  {
    if (_snapshot.endpoints.empty ()) {
      throw framework_exception_t (
        framework_error_kind_t::request_protocol_error,
        "HTTP TLS options require a listen endpoint");
    }
    auto &endpoint = _snapshot.endpoints.back ();
    endpoint.tls.emplace ();
    http_tls_options_builder_t builder (*endpoint.tls);
    if (configure) {
      configure (builder);
    }
    return *this;
  }

  template<typename THandler>
  http_options_builder_t &map_get (std::string path)
  {
    return add_route<THandler> (http_method_t::get, std::move (path));
  }

  template<typename THandler>
  http_options_builder_t &map_post (std::string path)
  {
    return add_route<THandler> (http_method_t::post, std::move (path));
  }

  template<typename THandler>
  http_options_builder_t &map_put (std::string path)
  {
    return add_route<THandler> (http_method_t::put, std::move (path));
  }

  template<typename THandler>
  http_options_builder_t &map_delete (std::string path)
  {
    return add_route<THandler> (http_method_t::delete_, std::move (path));
  }

  template<typename TMiddleware>
  http_options_builder_t &use ()
  {
    http_middleware_t middleware;
    middleware.name = typeid (TMiddleware).name ();
    middleware.before = [](service_provider_t &services,
                           http_context_t &context) {
      invoke_middleware_before<TMiddleware> (services, context);
    };
    middleware.after = [](service_provider_t &services,
                          http_context_t &context) {
      invoke_middleware_after<TMiddleware> (services, context);
    };
    _snapshot.middleware.push_back (std::move (middleware));
    return *this;
  }

  http_options_builder_t &map_health (std::string path)
  {
    _snapshot.health_path = validate_system_path (std::move (path));
    return *this;
  }

  http_options_builder_t &map_readiness (std::string path)
  {
    _snapshot.readiness_path = validate_system_path (std::move (path));
    return *this;
  }

  http_options_builder_t &map_liveness (std::string path)
  {
    _snapshot.liveness_path = validate_system_path (std::move (path));
    return *this;
  }

  const http_options_snapshot_t &snapshot () const noexcept
  {
    return _snapshot;
  }

  void validate () const
  {
    for (const auto &endpoint : _snapshot.endpoints) {
      if (!starts_with (endpoint.uri, "https://")) {
        continue;
      }
      if (!endpoint.tls ||
          endpoint.tls->certificate_file.empty () ||
          endpoint.tls->private_key_file.empty ()) {
        throw framework_exception_t (
          framework_error_kind_t::request_protocol_error,
          "HTTPS endpoint requires TLS certificate and private key");
      }
    }
  }

private:
  static bool starts_with (const std::string &value, const char *prefix)
  {
    return value.rfind (prefix, 0) == 0;
  }

  static std::string validate_system_path (std::string path)
  {
    if (path.empty () || path.front () != '/') {
      throw framework_exception_t (
        framework_error_kind_t::request_protocol_error,
        "HTTP route path must start with /");
    }
    return path;
  }

  template<typename TMiddleware>
  static TMiddleware &resolve_middleware (service_provider_t &services)
  {
    if constexpr (std::is_default_constructible_v<TMiddleware>) {
      static thread_local TMiddleware middleware;
      return middleware;
    } else {
      return services.get_required<TMiddleware> ();
    }
  }

  template<typename TMiddleware>
  static void invoke_middleware_before (service_provider_t &services,
                                        http_context_t &context)
  {
    auto &middleware = resolve_middleware<TMiddleware> (services);
    if constexpr (requires (TMiddleware value, http_context_t &ctx) {
                    value.before (ctx);
                  }) {
      middleware.before (context);
    } else if constexpr (requires (TMiddleware value,
                                   service_provider_t &provider,
                                   http_context_t &ctx) {
                           value.before (provider, ctx);
                         }) {
      middleware.before (services, context);
    }
  }

  template<typename TMiddleware>
  static void invoke_middleware_after (service_provider_t &services,
                                       http_context_t &context)
  {
    auto &middleware = resolve_middleware<TMiddleware> (services);
    if constexpr (requires (TMiddleware value, http_context_t &ctx) {
                    value.after (ctx);
                  }) {
      middleware.after (context);
    } else if constexpr (requires (TMiddleware value,
                                   service_provider_t &provider,
                                   http_context_t &ctx) {
                           value.after (provider, ctx);
                         }) {
      middleware.after (services, context);
    }
  }

  template<typename THandler, typename TRequest>
  static auto invoke_handler (THandler &handler,
                              const TRequest &request,
                              http_context_t &context)
  {
    if constexpr (requires {
                    handler.handle (request, context);
                  }) {
      return handler.handle (request, context);
    } else {
      return handler.handle (request);
    }
  }

  template<typename THandler>
  http_options_builder_t &add_route (http_method_t method, std::string path)
  {
    path = validate_system_path (std::move (path));
    http_route_t route;
    route.method = method;
    route.path = std::move (path);
    route.handler_name = typeid (THandler).name ();
    route.invoke_json = [](service_provider_t &services,
                           http_context_t &context,
                           const std::string &body) {
      using request_type = typename THandler::request_type;
      using reply_type = typename THandler::reply_type;
      request_type request {};
      if (!body.empty ()) {
        request = nlohmann::json::parse (body).template get<request_type> ();
      }
      if constexpr (std::is_default_constructible_v<THandler>) {
        THandler handler;
        reply_type reply = invoke_handler (handler, request, context);
        return nlohmann::json (reply).dump ();
      } else {
        auto &handler = services.get_required<THandler> ();
        reply_type reply = invoke_handler (handler, request, context);
        return nlohmann::json (reply).dump ();
      }
    };
    _snapshot.routes.push_back (std::move (route));
    return *this;
  }

  http_options_snapshot_t _snapshot;
};

} // namespace zlink::framework
