/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/codec/json.hpp>
#include <zlink/framework/contracts/dispatch/task.hpp>
#include <zlink/framework/contracts/errors/result.hpp>

#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <utility>

namespace zlink::http_client
{

inline constexpr int version_major = 0;
inline constexpr int version_minor = 1;
inline constexpr int version_patch = 0;

enum class http_method_t
{
    get,
    post,
    put,
    delete_
};

struct http_response_metadata_t
{
    int status = 0;
    std::map<std::string, std::string> headers;
};

template <typename T> struct http_response_t
{
    int status = 0;
    std::map<std::string, std::string> headers;
    T body;
    std::string raw_body;
};

struct raw_http_response_t
{
    int status = 0;
    std::map<std::string, std::string> headers;
    std::string body;
};

class request_builder_t;
class client_builder_t;

namespace detail
{
class http_client_runtime_t;
}

class client_t
{
  public:
    client_t () = default;

    static client_builder_t create ();

    request_builder_t get (std::string path) const;
    request_builder_t post (std::string path) const;
    request_builder_t put (std::string path) const;
    request_builder_t delete_ (std::string path) const;

  private:
    explicit client_t (std::shared_ptr<detail::http_client_runtime_t> runtime);

    std::shared_ptr<detail::http_client_runtime_t> _runtime;

    friend class client_builder_t;
    friend class request_builder_t;
};

class client_builder_t
{
  public:
    client_builder_t &base_url (std::string value);
    client_builder_t &json ();
    client_builder_t &timeout (std::chrono::milliseconds value);
    client_builder_t &default_header (std::string name, std::string value);
    client_builder_t &trust_certificate_file (std::string path);

    client_t build () const;

  private:
    std::string _base_url;
    bool _json = false;
    std::chrono::milliseconds _timeout{3000};
    std::map<std::string, std::string> _headers;
    std::optional<std::string> _trust_certificate_file;
};

class request_builder_t
{
  public:
    request_builder_t (const client_t &client, http_method_t method, std::string path);

    request_builder_t &header (std::string name, std::string value);

    template <typename T> request_builder_t &body (const T &value)
    {
        _body = zlink::message_t::from_json (value).to_string ();
        _headers.try_emplace ("content-type", "application/json");
        return *this;
    }

    zlink::framework::task_t<raw_http_response_t> submit_raw () const;

    template <typename T> zlink::framework::task_t<http_response_t<T>> submit () const
    {
        auto raw_task = submit_raw ();
        raw_http_response_t raw;
        try {
            raw = co_await raw_task;
        }
        catch (const zlink::framework::framework_exception_t &error) {
            co_return zlink::framework::result_t<http_response_t<T>>::failure (error.kind (), error.what (),
                                                                               error.is_retriable ());
        }

        if (raw.status >= 400) {
            std::ostringstream message;
            message << "HTTP request failed with status " << raw.status;
            co_return zlink::framework::result_t<http_response_t<T>>::failure (
              zlink::framework::framework_error_kind_t::request_failed, message.str ());
        }

        try {
            http_response_t<T> response{.status = raw.status,
                                        .headers = raw.headers,
                                        .body = zlink::message_t::from (raw.body).template parse_json<T> (),
                                        .raw_body = raw.body};
            co_return response;
        }
        catch (const std::exception &ex) {
            co_return zlink::framework::result_t<http_response_t<T>>::failure (
              zlink::framework::framework_error_kind_t::payload_decode_failed, ex.what ());
        }
    }

    template <typename T, typename TCallback> void submit (TCallback &&callback) const
    {
        auto task = submit<T> ();
        zlink::framework::detail::observe_task_completion (task, std::forward<TCallback> (callback));
    }

  private:
    const client_t *_client;
    http_method_t _method;
    std::string _path;
    std::optional<std::string> _body;
    std::map<std::string, std::string> _headers;
};

} // namespace zlink::http_client
