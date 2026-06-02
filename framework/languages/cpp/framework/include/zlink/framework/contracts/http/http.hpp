/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/framework/contracts/configuration/services.hpp>
#include <zlink/framework/contracts/codecs/serializer.hpp>
#include <zlink/framework/contracts/dispatch/task.hpp>
#include <zlink/framework/contracts/errors/error.hpp>

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <typeindex>
#include <typeinfo>
#include <utility>
#include <set>
#include <vector>

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
  std::function<std::shared_ptr<void> ()> create_instance;
  std::function<void (service_provider_t &,
                      http_context_t &,
                      const std::shared_ptr<void> &)> before;
  std::function<void (service_provider_t &,
                      http_context_t &,
                      const std::shared_ptr<void> &)> after;
};

class http_route_t
{
public:
  http_method_t method;
  std::string path;
  std::string handler_name;

private:
  std::function<task_t<std::string> (service_provider_t &,
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
    middleware.create_instance = [] () -> std::shared_ptr<void> {
      if constexpr (std::is_default_constructible_v<TMiddleware>) {
        return std::make_shared<TMiddleware> ();
      } else {
        return {};
      }
    };
    middleware.before = [](service_provider_t &services,
                           http_context_t &context,
                           const std::shared_ptr<void> &instance) {
      invoke_middleware_before<TMiddleware> (services, context, instance);
    };
    middleware.after = [](service_provider_t &services,
                          http_context_t &context,
                          const std::shared_ptr<void> &instance) {
      invoke_middleware_after<TMiddleware> (services, context, instance);
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
    std::set<std::string> route_keys;
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
    for (const auto &route : _snapshot.routes) {
      const auto key = route_key (route.method, route.path);
      if (!route_keys.insert (key).second) {
        throw framework_exception_t (
          framework_error_kind_t::request_protocol_error,
          "duplicate HTTP route registration");
      }
      if (matches_system_path (route.path)) {
        throw framework_exception_t (
          framework_error_kind_t::request_protocol_error,
          "HTTP route conflicts with a system health route");
      }
    }
  }

private:
  friend class zlink_framework_options_t;

  void bind_services (service_collection_t &services,
                      serializer_registry_t &serializers) noexcept
  {
    _services = &services;
    _serializers = &serializers;
  }

  template<typename T>
  struct task_value_type_t
  {
  };

  template<typename T>
  struct task_value_type_t<task_t<T>>
  {
    using type = T;
  };

  template<typename T>
  static constexpr bool is_task_v =
    requires { typename task_value_type_t<T>::type; };

  static bool starts_with (const std::string &value, const char *prefix)
  {
    return value.rfind (prefix, 0) == 0;
  }

  static std::string route_key (http_method_t method, const std::string &path)
  {
    return std::to_string (static_cast<int> (method)) + ":" + path;
  }

  bool matches_system_path (const std::string &path) const
  {
    return (_snapshot.health_path && *_snapshot.health_path == path) ||
           (_snapshot.readiness_path && *_snapshot.readiness_path == path) ||
           (_snapshot.liveness_path && *_snapshot.liveness_path == path);
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
  static void invoke_middleware_before_instance (TMiddleware &middleware,
                                                service_provider_t &services,
                                                http_context_t &context)
  {
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
  static void invoke_middleware_after_instance (TMiddleware &middleware,
                                               service_provider_t &services,
                                               http_context_t &context)
  {
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

  template<typename TMiddleware>
  static TMiddleware &resolve_middleware (
    service_provider_t &services,
    const std::shared_ptr<void> &instance)
  {
    if constexpr (std::is_default_constructible_v<TMiddleware>) {
      return *std::static_pointer_cast<TMiddleware> (instance);
    } else {
      return services.get_required<TMiddleware> ();
    }
  }

  template<typename TMiddleware>
  static void invoke_middleware_before (
    service_provider_t &services,
    http_context_t &context,
    const std::shared_ptr<void> &instance)
  {
    auto &middleware = resolve_middleware<TMiddleware> (services, instance);
    invoke_middleware_before_instance<TMiddleware> (
      middleware,
      services,
      context);
  }

  template<typename TMiddleware>
  static void invoke_middleware_after (
    service_provider_t &services,
    http_context_t &context,
    const std::shared_ptr<void> &instance)
  {
    auto &middleware = resolve_middleware<TMiddleware> (services, instance);
    invoke_middleware_after_instance<TMiddleware> (
      middleware,
      services,
      context);
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

  template<typename TReply, typename TResult>
  static task_t<TReply> resolve_handler_reply (TResult &&result)
  {
    using result_type = std::remove_cvref_t<TResult>;
    if constexpr (is_task_v<result_type>) {
      using value_type = typename task_value_type_t<result_type>::type;
      static_assert (std::is_same_v<value_type, TReply>,
                     "HTTP handler task_t<T> must return reply_type");
      co_return co_await std::forward<TResult> (result);
    } else {
      co_return TReply (std::forward<TResult> (result));
    }
  }

  template<typename THandler>
  http_options_builder_t &add_route (http_method_t method, std::string path)
  {
    register_handler_service<THandler> ();
    register_route_serializers<THandler> ();
    path = validate_system_path (std::move (path));
    http_route_t route;
    route.method = method;
    route.path = std::move (path);
    route.handler_name = typeid (THandler).name ();
    auto *serializers = _serializers;
    route.invoke_json = [serializers](service_provider_t &services,
                                      http_context_t &context,
                                      const std::string &body)
      -> task_t<std::string> {
      using request_type = typename THandler::request_type;
      using reply_type = typename THandler::reply_type;
      if (serializers == nullptr) {
        throw framework_exception_t (
          framework_error_kind_t::request_protocol_error,
          "HTTP route serializer registry is not configured");
      }
      const auto request =
        serializers->template get<request_type> ().deserialize (
          zlink::message_t::from (body));
      try {
        auto &handler = services.get_required<THandler> ();
        auto handler_result = invoke_handler (handler, request, context);
        auto reply =
          co_await resolve_handler_reply<reply_type> (
            std::move (handler_result));
        co_return serializers->template get<reply_type> ()
          .serialize (reply)
          .to_string ();
      } catch (const framework_exception_t &ex) {
        co_return result_t<std::string>::failure (
          ex.kind (),
          ex.what (),
          ex.is_retriable ());
      } catch (const std::exception &ex) {
        co_return result_t<std::string>::failure (
          framework_error_kind_t::request_failed,
          ex.what ());
      }
    };
    _snapshot.routes.push_back (std::move (route));
    return *this;
  }

  template<typename THandler>
  void register_handler_service ()
  {
    if (_services == nullptr) {
      return;
    }
    if (!_handler_service_types.emplace (
          std::type_index (typeid (THandler))).second) {
      return;
    }
    if (_services->contains (std::type_index (typeid (THandler)))) {
      return;
    }
    detail::injected_handler_registrar_t<
      THandler,
      typename detail::handler_dependencies_t<THandler>::type>::add (
        *_services);
  }

  template<typename T>
  void register_json_serializer ()
  {
    if (_serializers == nullptr) {
      return;
    }
    const auto type = std::type_index (typeid (T));
    if (!_json_serializer_types.emplace (type).second ||
        _serializers->contains (type)) {
      return;
    }
    _serializers->template add_json<T> ();
  }

  template<typename THandler>
  void register_route_serializers ()
  {
    register_json_serializer<typename THandler::request_type> ();
    register_json_serializer<typename THandler::reply_type> ();
  }

  http_options_snapshot_t _snapshot;
  service_collection_t *_services = nullptr;
  serializer_registry_t *_serializers = nullptr;
  std::set<std::type_index> _handler_service_types;
  std::set<std::type_index> _json_serializer_types;
};

} // namespace zlink::framework
