/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/framework/contracts/errors/error.hpp>

#include <functional>
#include <optional>
#include <string>
#include <typeinfo>
#include <utility>
#include <vector>

namespace zlink::framework
{

enum class http_method_t
{
  get,
  post,
  put,
  delete_
};

struct http_tls_options_t
{
  std::string certificate_file;
  std::string private_key_file;
};

struct http_route_t
{
  http_method_t method;
  std::string path;
  std::string handler_name;
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
  std::vector<std::string> middleware;
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
    _snapshot.middleware.push_back (typeid (TMiddleware).name ());
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

  template<typename THandler>
  http_options_builder_t &add_route (http_method_t method, std::string path)
  {
    if (path.empty () || path.front () != '/') {
      throw framework_exception_t (
        framework_error_kind_t::request_protocol_error,
        "HTTP route path must start with /");
    }
    _snapshot.routes.push_back (
      { .method = method,
        .path = std::move (path),
        .handler_name = typeid (THandler).name () });
    return *this;
  }

  http_options_snapshot_t _snapshot;
};

} // namespace zlink::framework
