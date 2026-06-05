/* SPDX-License-Identifier: MPL-2.0 */

#include "runtime/http_client_runtime.hpp"

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>

#ifdef ZLINK_HTTP_CLIENT_WITH_OPENSSL
#include <boost/asio/ssl/host_name_verification.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/beast/ssl.hpp>
#endif

#include <algorithm>
#include <cctype>
#include <chrono>
#include <sstream>
#include <stdexcept>

namespace zlink::http_client::detail
{
namespace
{
namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
using tcp = asio::ip::tcp;

struct parsed_url_t
{
    std::string scheme;
    std::string host;
    std::string port;
    std::string target_prefix;
};

bool starts_with (const std::string &value, const char *prefix)
{
    return value.rfind (prefix, 0) == 0;
}

parsed_url_t parse_base_url (const std::string &url)
{
    std::string scheme;
    std::string rest;
    if (starts_with (url, "http://")) {
        scheme = "http";
        rest = url.substr (7);
    } else if (starts_with (url, "https://")) {
        scheme = "https";
        rest = url.substr (8);
    } else {
        throw std::invalid_argument ("HTTP client base_url must start with http:// or https://");
    }

    const auto slash = rest.find ('/');
    auto authority = slash == std::string::npos ? rest : rest.substr (0, slash);
    auto target_prefix = slash == std::string::npos ? std::string () : rest.substr (slash);
    if (authority.empty ()) {
        throw std::invalid_argument ("HTTP client base_url requires a host");
    }

    std::string host;
    std::string port = scheme == "https" ? "443" : "80";
    if (authority.front () == '[') {
        const auto close = authority.find (']');
        if (close == std::string::npos) {
            throw std::invalid_argument ("HTTP client IPv6 host is missing ]");
        }
        host = authority.substr (1, close - 1);
        if (close + 1 < authority.size ()) {
            if (authority[close + 1] != ':') {
                throw std::invalid_argument ("HTTP client base_url has invalid IPv6 authority");
            }
            port = authority.substr (close + 2);
        }
    } else {
        const auto colon = authority.rfind (':');
        if (colon == std::string::npos) {
            host = authority;
        } else {
            host = authority.substr (0, colon);
            port = authority.substr (colon + 1);
        }
    }

    if (host.empty () || port.empty ()) {
        throw std::invalid_argument ("HTTP client base_url requires host and port");
    }

    return {.scheme = std::move (scheme),
            .host = std::move (host),
            .port = std::move (port),
            .target_prefix = std::move (target_prefix)};
}

std::string make_target (const parsed_url_t &url, const std::string &path)
{
    if (path.empty () || path.front () != '/') {
        throw std::invalid_argument ("HTTP request path must start with /");
    }
    if (url.target_prefix.empty () || url.target_prefix == "/") {
        return path;
    }
    if (url.target_prefix.back () == '/') {
        return url.target_prefix.substr (0, url.target_prefix.size () - 1) + path;
    }
    return url.target_prefix + path;
}

http::verb to_beast_method (http_method_t method)
{
    switch (method) {
        case http_method_t::get:
            return http::verb::get;
        case http_method_t::post:
            return http::verb::post;
        case http_method_t::put:
            return http::verb::put;
        case http_method_t::delete_:
            return http::verb::delete_;
    }
    return http::verb::get;
}

raw_http_response_t to_raw_response (const http::response<http::string_body> &response)
{
    raw_http_response_t raw;
    raw.status = static_cast<int> (response.result_int ());
    raw.body = response.body ();
    for (const auto &field : response) {
        raw.headers.emplace (std::string (field.name_string ()), std::string (field.value ()));
    }
    return raw;
}

template <typename TStream>
raw_http_response_t
exchange_request (TStream &stream, const http::request<http::string_body> &request, beast::flat_buffer &buffer)
{
    http::write (stream, request);
    http::response<http::string_body> response;
    http::read (stream, buffer, response);
    return to_raw_response (response);
}

zlink::framework::result_t<raw_http_response_t> finish_response (raw_http_response_t response,
                                                                 std::chrono::steady_clock::time_point started_at,
                                                                 std::chrono::milliseconds timeout)
{
    if (std::chrono::steady_clock::now () - started_at > timeout) {
        return zlink::framework::result_t<raw_http_response_t>::failure (
          zlink::framework::framework_error_kind_t::timeout, "HTTP request exceeded timeout", true);
    }
    return zlink::framework::result_t<raw_http_response_t>::success (std::move (response));
}

zlink::framework::result_t<raw_http_response_t> map_exception (const std::exception &ex)
{
    return zlink::framework::result_t<raw_http_response_t>::failure (
      zlink::framework::framework_error_kind_t::request_failed, ex.what (), true);
}

} // namespace

http_client_runtime_t::http_client_runtime_t (http_client_options_t options) : _options (std::move (options))
{
    (void) parse_base_url (_options.base_url);
}

zlink::framework::result_t<raw_http_response_t> http_client_runtime_t::execute (const http_request_t &request) const
{
    try {
        const auto started_at = std::chrono::steady_clock::now ();
        const auto url = parse_base_url (_options.base_url);
        const auto target = make_target (url, request.path);

        asio::io_context io;
        tcp::resolver resolver (io);
        beast::flat_buffer buffer;

        http::request<http::string_body> req{to_beast_method (request.method), target, 11};
        req.set (http::field::host, url.host);
        req.set (http::field::user_agent, "zlink-http-client/0.1");
        if (_options.json) {
            req.set (http::field::accept, "application/json");
        }
        for (const auto &[name, value] : _options.headers) {
            req.set (name, value);
        }
        for (const auto &[name, value] : request.headers) {
            req.set (name, value);
        }
        if (request.body) {
            req.body () = *request.body;
            req.prepare_payload ();
        }

        const auto results = resolver.resolve (url.host, url.port);

        if (url.scheme == "http") {
            beast::tcp_stream stream (io);
            stream.expires_after (_options.timeout);
            stream.connect (results);
            auto raw = exchange_request (stream, req, buffer);
            beast::error_code ignored;
            stream.socket ().shutdown (tcp::socket::shutdown_both, ignored);
            return finish_response (std::move (raw), started_at, _options.timeout);
        }

#ifdef ZLINK_HTTP_CLIENT_WITH_OPENSSL
        asio::ssl::context context (asio::ssl::context::tls_client);
        context.set_default_verify_paths ();
        if (_options.trust_certificate_file) {
            context.load_verify_file (*_options.trust_certificate_file);
        }
        asio::ssl::stream<beast::tcp_stream> stream (io, context);
        SSL_set_tlsext_host_name (stream.native_handle (), url.host.c_str ());
        stream.set_verify_mode (asio::ssl::verify_peer);
        stream.set_verify_callback (asio::ssl::host_name_verification (url.host));
        beast::get_lowest_layer (stream).expires_after (_options.timeout);
        beast::get_lowest_layer (stream).connect (results);
        stream.handshake (asio::ssl::stream_base::client);
        auto raw = exchange_request (stream, req, buffer);
        beast::error_code ignored;
        stream.shutdown (ignored);
        return finish_response (std::move (raw), started_at, _options.timeout);
#else
        return zlink::framework::result_t<raw_http_response_t>::failure (
          zlink::framework::framework_error_kind_t::request_protocol_error, "HTTPS support requires OpenSSL");
#endif
    }
    catch (const boost::system::system_error &ex) {
        if (ex.code () == boost::beast::error::timeout || ex.code () == boost::asio::error::timed_out) {
            return zlink::framework::result_t<raw_http_response_t>::failure (
              zlink::framework::framework_error_kind_t::timeout, ex.what (), true);
        }
        return map_exception (ex);
    }
    catch (const std::exception &ex) {
        return map_exception (ex);
    }
}

} // namespace zlink::http_client::detail
