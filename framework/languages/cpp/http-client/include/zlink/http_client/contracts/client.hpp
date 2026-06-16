/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/http_client/contracts/coroutines.hpp>
#include <zlink/http_client/contracts/types.hpp>
#include <zlink/codec/json.hpp>
#include <zlink/framework/contracts/dispatch/task.hpp>

#include <chrono>
#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace zlink::http_client
{

class request_builder_t;
class client_builder_t;

namespace detail
{
class http_client_runtime_t;
struct http_request_t;
}

class client_t
{
  public:
    client_t () = default;

    static client_builder_t create ();
    static client_builder_t create (std::string base_url);

    request_builder_t get (std::string path) const;
    request_builder_t post (std::string path) const;
    request_builder_t put (std::string path) const;
    request_builder_t delete_ (std::string path) const;
    request_builder_t patch (std::string path) const;
    request_builder_t head (std::string path) const;
    request_builder_t options (std::string path) const;

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
    client_builder_t &basic_auth (const std::string &user, const std::string &password);
    client_builder_t &bearer_token (const std::string &token);
    client_builder_t &max_response_body_size (std::size_t bytes);
    client_builder_t &trust_certificate_file (std::string path);
    client_builder_t &client_certificate_file (std::string certificate_path, std::string key_path);
    client_builder_t &follow_redirects (int max_redirects = 5);
    client_builder_t &retry (int attempts);
    client_builder_t &cookies ();
    client_builder_t &proxy (std::string url);
    client_builder_t &proxy_basic_auth (const std::string &user, const std::string &password);
    client_builder_t &compression ();
    client_builder_t &coroutines ();
    client_builder_t &coroutines (std::shared_ptr<coroutine_resume_scheduler_t> resume_scheduler);
    client_builder_t &coroutines (std::shared_ptr<coroutine_execute_scheduler_t> execute_scheduler,
                                  std::shared_ptr<coroutine_resume_scheduler_t> resume_scheduler);

    client_t build () const;

    // One-shot shortcuts: build the client on demand so `build()` can be
    // omitted for single requests. The returned request owns its client
    // (see request_builder_t::_client), so the runtime stays alive for the
    // duration of the request even when the builder is a temporary.
    request_builder_t get (std::string path) const;
    request_builder_t post (std::string path) const;
    request_builder_t put (std::string path) const;
    request_builder_t delete_ (std::string path) const;
    request_builder_t patch (std::string path) const;
    request_builder_t head (std::string path) const;
    request_builder_t options (std::string path) const;

  private:
    std::string _base_url;
    bool _json = false;
    std::chrono::milliseconds _timeout{3000};
    std::size_t _max_response_body_size = 16 * 1024 * 1024;
    std::map<std::string, std::string> _headers;
    std::optional<std::string> _trust_certificate_file;
    std::optional<std::pair<std::string, std::string>> _client_certificate;
    int _follow_redirects = 0;
    int _retry_attempts = 0;
    bool _cookies = false;
    std::optional<std::string> _proxy;
    std::optional<std::string> _proxy_authorization;
    bool _compression = false;
    bool _coroutines = false;
    std::shared_ptr<coroutine_execute_scheduler_t> _execute_scheduler;
    std::shared_ptr<coroutine_resume_scheduler_t> _resume_scheduler;
};

class request_builder_t
{
  public:
    using body_stream_provider_t = std::function<std::optional<std::string> ()>;

    request_builder_t (client_t client, http_method_t method, std::string path);

    request_builder_t &header (std::string name, std::string value);
    request_builder_t &query (std::string name, std::string value);
    request_builder_t &timeout (std::chrono::milliseconds value);

    template <typename T> request_builder_t &body (const T &value)
    {
        _body = zlink::message_t::from_json (value).to_string ();
        _headers.try_emplace ("content-type", "application/json");
        return *this;
    }

    request_builder_t &body (std::string content, std::string content_type);

    // Streams the request body chunk by chunk with chunked transfer-encoding;
    // the provider returns std::nullopt when the body is complete. Requests
    // with a streamed body are sent on a fresh connection and are excluded
    // from automatic retry (the provider cannot be rewound).
    request_builder_t &body_stream (body_stream_provider_t provider, std::string content_type);

    request_builder_t &form (std::string name, std::string value);
    request_builder_t &multipart (std::string name, std::string value);
    request_builder_t &multipart_file (std::string name,
                                       std::string filename,
                                       std::string content,
                                       std::string content_type);

    zlink::framework::task_t<raw_http_response_t> submit_raw () const;

    // Streams the response body to `sink` chunk by chunk instead of buffering
    // it; the returned response carries status and headers with an empty body.
    // Chunks are delivered as received (no content-encoding decompression).
    zlink::framework::task_t<raw_http_response_t>
    download (std::function<void (std::string_view)> sink) const;

    template <typename T> zlink::framework::task_t<http_response_t<T>> submit () const
    {
        auto raw_task = submit_raw ();
        raw_http_response_t raw;
        try {
            raw = co_await raw_task;
        }
        catch (const zlink::framework::framework_exception_t &error) {
            co_return zlink::framework::result_t<http_response_t<T>>::failure (
              error.kind (), error.what (), error.is_retriable ());
        }

        if (raw.status >= 400) {
            std::ostringstream message;
            message << "HTTP request failed with status " << raw.status;
            co_return zlink::framework::result_t<http_response_t<T>>::failure (
              zlink::framework::framework_error_kind_t::request_failed, message.str ());
        }

        try {
            http_response_t<T> response{
              .status = raw.status,
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
        zlink::framework::detail::observe_task_completion (task,
                                                           std::forward<TCallback> (callback));
    }

    // Blocking convenience that unwraps the result and returns the typed body
    // directly, throwing on failure. Intended for tests and client scenarios
    // where blocking is acceptable; runtime/handler code should use submit<T>()
    // and co_await the task instead of blocking on the result.
    template <typename T> T fetch () const
    {
        auto result = submit<T> ().result ();
        if (!result) {
            const auto *error = result.error ();
            throw zlink::framework::framework_exception_t (error->kind (), error->what (),
                                                           error->is_retriable ());
        }
        return std::move (result.value ().body);
    }

  private:
    struct multipart_part_t
    {
        std::string name;
        std::string filename;
        std::string content;
        std::string content_type;
    };

    std::string resolve_target () const;
    std::pair<std::optional<std::string>, std::map<std::string, std::string>>
    resolve_body_and_headers () const;
    detail::http_request_t make_request (std::function<void (std::string_view)> sink) const;
    zlink::framework::task_t<raw_http_response_t> dispatch_request (detail::http_request_t request) const;

    client_t _client;
    http_method_t _method;
    std::string _path;
    std::optional<std::string> _body;
    std::function<std::optional<std::string> ()> _body_provider;
    std::map<std::string, std::string> _headers;
    std::optional<std::chrono::milliseconds> _timeout;
    std::vector<std::pair<std::string, std::string>> _query;
    std::vector<std::pair<std::string, std::string>> _form;
    std::vector<multipart_part_t> _multipart;
};

} // namespace zlink::http_client
