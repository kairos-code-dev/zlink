/* SPDX-License-Identifier: MPL-2.0 */

#include "runtime/http_client_runtime.hpp"

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/beast/zlib/error.hpp>
#include <boost/beast/zlib/inflate_stream.hpp>

#ifdef ZLINK_HTTP_CLIENT_WITH_OPENSSL
#include <boost/asio/ssl/host_name_verification.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/beast/ssl.hpp>
#endif

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <thread>

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

struct hop_target_t
{
    std::string scheme;
    std::string host;
    std::string port;
    std::string target;
};

bool starts_with (const std::string &value, const char *prefix)
{
    return value.rfind (prefix, 0) == 0;
}

bool iequals (std::string_view left, std::string_view right)
{
    return left.size () == right.size ()
           && std::equal (left.begin (), left.end (), right.begin (), [] (char a, char b) {
                  return std::tolower (static_cast<unsigned char> (a))
                         == std::tolower (static_cast<unsigned char> (b));
              });
}

std::string trim (std::string_view value)
{
    const auto begin = value.find_first_not_of (" \t");
    if (begin == std::string_view::npos) {
        return {};
    }
    const auto end = value.find_last_not_of (" \t");
    return std::string (value.substr (begin, end - begin + 1));
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
        case http_method_t::patch:
            return http::verb::patch;
        case http_method_t::head:
            return http::verb::head;
        case http_method_t::options:
            return http::verb::options;
    }
    return http::verb::get;
}

bool is_redirect_status (int status)
{
    return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
}

zlink::framework::framework_exception_t request_error (const std::string &message)
{
    return zlink::framework::framework_exception_t (
      zlink::framework::framework_error_kind_t::request_failed, message);
}

hop_target_t resolve_location (const hop_target_t &current, const std::string &location)
{
    if (starts_with (location, "http://") || starts_with (location, "https://")) {
        const auto url = parse_base_url (location);
        return {.scheme = url.scheme,
                .host = url.host,
                .port = url.port,
                .target = url.target_prefix.empty () ? "/" : url.target_prefix};
    }
    if (!location.empty () && location.front () == '/') {
        return {.scheme = current.scheme,
                .host = current.host,
                .port = current.port,
                .target = location};
    }
    throw request_error ("HTTP redirect location is not supported: " + location);
}

[[noreturn]] void fail_decode ()
{
    throw zlink::framework::framework_exception_t (
      zlink::framework::framework_error_kind_t::payload_decode_failed,
      "HTTP response compressed body is malformed");
}

std::string inflate_raw (const unsigned char *data, std::size_t size)
{
    if (size == 0) {
        fail_decode ();
    }

    beast::zlib::inflate_stream inflater;
    beast::zlib::z_params zs;
    zs.next_in = data;
    zs.avail_in = size;

    std::string decoded;
    char chunk[16384];
    for (;;) {
        zs.next_out = chunk;
        zs.avail_out = sizeof chunk;
        beast::error_code ec;
        inflater.write (zs, beast::zlib::Flush::sync, ec);
        decoded.append (chunk, sizeof chunk - zs.avail_out);
        if (ec == beast::zlib::error::end_of_stream) {
            return decoded;
        }
        if (ec || (zs.avail_in == 0 && zs.avail_out != 0)) {
            fail_decode ();
        }
    }
}

// The gzip wrapper is parsed by hand because beast::zlib only inflates raw
// DEFLATE streams. The 8-byte trailer (CRC32 + size) is not validated.
std::string gunzip (const std::string &compressed)
{
    const auto *data = reinterpret_cast<const unsigned char *> (compressed.data ());
    const auto size = compressed.size ();
    if (size < 18 || data[0] != 0x1f || data[1] != 0x8b || data[2] != 0x08) {
        fail_decode ();
    }

    const unsigned char flags = data[3];
    std::size_t offset = 10;
    if (flags & 0x04) {
        if (offset + 2 > size) {
            fail_decode ();
        }
        const std::size_t extra = data[offset] | (data[offset + 1] << 8);
        offset += 2 + extra;
    }
    for (const unsigned char flag :
         {static_cast<unsigned char> (0x08), static_cast<unsigned char> (0x10)}) {
        if (flags & flag) {
            while (offset < size && data[offset] != 0) {
                ++offset;
            }
            ++offset;
        }
    }
    if (flags & 0x02) {
        offset += 2;
    }
    if (offset >= size) {
        fail_decode ();
    }
    return inflate_raw (data + offset, size - offset);
}

std::string inflate_deflate (const std::string &compressed)
{
    const auto *data = reinterpret_cast<const unsigned char *> (compressed.data ());
    const auto size = compressed.size ();
    // Servers send either a zlib-wrapped stream (sniffed via the CMF/FLG
    // checksum) or a raw DEFLATE stream.
    if (size >= 2 && (data[0] & 0x0f) == 8 && ((data[0] << 8 | data[1]) % 31) == 0) {
        return inflate_raw (data + 2, size - 2);
    }
    return inflate_raw (data, size);
}

std::optional<std::string> find_header (const std::map<std::string, std::string> &headers,
                                        std::string_view name)
{
    for (const auto &[key, value] : headers) {
        if (iequals (key, name)) {
            return value;
        }
    }
    return std::nullopt;
}

void erase_header (std::map<std::string, std::string> &headers, std::string_view name)
{
    for (auto it = headers.begin (); it != headers.end ();) {
        it = iequals (it->first, name) ? headers.erase (it) : std::next (it);
    }
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

zlink::framework::result_t<raw_http_response_t>
finish_response (raw_http_response_t response,
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

struct pooled_connection_t
{
    asio::io_context io;
    std::optional<beast::tcp_stream> plain;
#ifdef ZLINK_HTTP_CLIENT_WITH_OPENSSL
    std::optional<asio::ssl::context> tls_context;
    std::optional<asio::ssl::stream<beast::tcp_stream>> secure;
#endif
    beast::flat_buffer buffer;
};

} // namespace

class connection_pool_t
{
  public:
    std::unique_ptr<pooled_connection_t> acquire (const std::string &key)
    {
        const std::lock_guard<std::mutex> lock (_mutex);
        auto found = _idle.find (key);
        if (found == _idle.end () || found->second.empty ()) {
            return nullptr;
        }
        auto connection = std::move (found->second.back ());
        found->second.pop_back ();
        return connection;
    }

    void release (const std::string &key, std::unique_ptr<pooled_connection_t> connection)
    {
        static constexpr std::size_t max_idle_per_key = 4;
        const std::lock_guard<std::mutex> lock (_mutex);
        auto &idle = _idle[key];
        if (idle.size () < max_idle_per_key) {
            idle.push_back (std::move (connection));
        }
    }

  private:
    std::mutex _mutex;
    std::map<std::string, std::vector<std::unique_ptr<pooled_connection_t>>> _idle;
};

void cookie_jar_t::store (const std::string &host, const std::string &set_cookie_header)
{
    std::string_view remaining (set_cookie_header);
    const auto take_segment = [&remaining] () -> std::optional<std::string> {
        if (remaining.empty ()) {
            return std::nullopt;
        }
        const auto semicolon = remaining.find (';');
        auto segment = trim (remaining.substr (0, semicolon));
        remaining = semicolon == std::string_view::npos ? std::string_view ()
                                                        : remaining.substr (semicolon + 1);
        return segment;
    };

    const auto pair = take_segment ();
    if (!pair) {
        return;
    }
    const auto equals = pair->find ('=');
    if (equals == std::string::npos) {
        return;
    }
    cookie_t cookie{.host = host,
                    .name = trim (std::string_view (*pair).substr (0, equals)),
                    .value = trim (std::string_view (*pair).substr (equals + 1)),
                    .path = "/",
                    .secure = false};
    if (cookie.name.empty ()) {
        return;
    }

    bool expired = false;
    while (auto attribute = take_segment ()) {
        const auto attr_equals = attribute->find ('=');
        const auto name = attr_equals == std::string::npos
                            ? *attribute
                            : trim (std::string_view (*attribute).substr (0, attr_equals));
        const auto value = attr_equals == std::string::npos
                             ? std::string ()
                             : trim (std::string_view (*attribute).substr (attr_equals + 1));
        if (iequals (name, "path") && !value.empty ()) {
            cookie.path = value;
        } else if (iequals (name, "secure")) {
            cookie.secure = true;
        } else if (iequals (name, "max-age")) {
            try {
                expired = std::stol (value) <= 0;
            }
            catch (const std::exception &) {
            }
        }
    }

    const std::lock_guard<std::mutex> lock (_mutex);
    _cookies.erase (std::remove_if (_cookies.begin (), _cookies.end (),
                                    [&] (const cookie_t &existing) {
                                        return existing.host == host
                                               && existing.name == cookie.name;
                                    }),
                    _cookies.end ());
    if (!expired) {
        _cookies.push_back (std::move (cookie));
    }
}

std::string cookie_jar_t::header_for (const std::string &host,
                                      const std::string &path,
                                      bool secure) const
{
    const std::lock_guard<std::mutex> lock (_mutex);
    std::string header;
    for (const auto &cookie : _cookies) {
        if (cookie.host != host || (cookie.secure && !secure)
            || path.rfind (cookie.path, 0) != 0) {
            continue;
        }
        if (!header.empty ()) {
            header += "; ";
        }
        header += cookie.name + "=" + cookie.value;
    }
    return header;
}

http_client_runtime_t::http_client_runtime_t (http_client_options_t options) :
    _options (std::move (options)), _connection_pool (std::make_unique<connection_pool_t> ())
{
    (void) parse_base_url (_options.base_url);
    if (_options.proxy) {
        (void) parse_base_url (*_options.proxy);
    }
}

http_client_runtime_t::~http_client_runtime_t () = default;

namespace
{

struct exchange_outcome_t
{
    http::response<http::string_body> response;
    bool reusable = false;
};

class request_performer_t
{
  public:
    request_performer_t (const http_client_options_t &options,
                         cookie_jar_t &cookie_jar,
                         connection_pool_t &pool,
                         const http_request_t &request) :
        _options (options), _cookie_jar (cookie_jar), _pool (pool), _request (request)
    {
    }

    zlink::framework::result_t<raw_http_response_t> perform ()
    {
        const auto started_at = std::chrono::steady_clock::now ();
        const auto url = parse_base_url (_options.base_url);

        hop_target_t hop{.scheme = url.scheme,
                         .host = url.host,
                         .port = url.port,
                         .target = make_target (url, _request.path)};
        auto method = _request.method;
        auto body = _request.body;
        int redirects_left = _options.follow_redirects;

        for (;;) {
            auto response = perform_hop (hop, method, body);
            if (_options.cookies) {
                for (const auto &field : response) {
                    if (field.name () == http::field::set_cookie) {
                        _cookie_jar.store (hop.host, std::string (field.value ()));
                    }
                }
            }

            const auto status = static_cast<int> (response.result_int ());
            const auto location = std::string (response[http::field::location]);
            if (_options.follow_redirects > 0 && is_redirect_status (status)
                && !location.empty ()) {
                if (redirects_left == 0) {
                    throw request_error ("HTTP request exceeded the redirect limit");
                }
                --redirects_left;
                if (status == 303
                    || ((status == 301 || status == 302) && method == http_method_t::post)) {
                    method = http_method_t::get;
                    body.reset ();
                }
                hop = resolve_location (hop, location);
                continue;
            }

            auto raw = to_raw_response (response);
            if (_options.compression && !_request.sink) {
                const auto encoding = find_header (raw.headers, "content-encoding");
                if (encoding && iequals (*encoding, "gzip")) {
                    raw.body = gunzip (raw.body);
                    erase_header (raw.headers, "content-encoding");
                } else if (encoding && iequals (*encoding, "deflate")) {
                    raw.body = inflate_deflate (raw.body);
                    erase_header (raw.headers, "content-encoding");
                }
            }
            return finish_response (std::move (raw), started_at, effective_timeout ());
        }
    }

  private:
    std::chrono::milliseconds effective_timeout () const
    {
        return _request.timeout.value_or (_options.timeout);
    }

    template <typename TBody>
    http::request<TBody> build_wire_request (const hop_target_t &hop,
                                             http_method_t method,
                                             bool absolute_form) const
    {
        const auto wire_target = absolute_form
                                   ? "http://" + hop.host + ":" + hop.port + hop.target
                                   : hop.target;
        http::request<TBody> wire{to_beast_method (method), wire_target, 11};

        const bool default_port = (hop.scheme == "http" && hop.port == "80")
                                  || (hop.scheme == "https" && hop.port == "443");
        wire.set (http::field::host, default_port ? hop.host : hop.host + ":" + hop.port);
        wire.set (http::field::user_agent, "zlink-http-client/0.2");
        if (_options.json) {
            wire.set (http::field::accept, "application/json");
        }
        if (_options.compression) {
            wire.set (http::field::accept_encoding, "gzip, deflate");
        }
        if (absolute_form && _options.proxy_authorization) {
            wire.set (http::field::proxy_authorization, *_options.proxy_authorization);
        }
        for (const auto &[name, value] : _options.headers) {
            wire.set (name, value);
        }
        for (const auto &[name, value] : _request.headers) {
            wire.set (name, value);
        }
        if (_options.cookies) {
            const auto path = hop.target.substr (0, hop.target.find ('?'));
            const auto cookie_header =
              _cookie_jar.header_for (hop.host, path, hop.scheme == "https");
            if (!cookie_header.empty ()) {
                wire.set (http::field::cookie, cookie_header);
            }
        }
        return wire;
    }

    std::string pool_key (const hop_target_t &hop) const
    {
        auto key = hop.scheme + "|" + hop.host + ":" + hop.port;
        if (_options.proxy) {
            key += "|proxy=" + *_options.proxy;
        }
        return key;
    }

    http::response<http::string_body> perform_hop (const hop_target_t &hop,
                                                   http_method_t method,
                                                   const std::optional<std::string> &body)
    {
        const auto key = pool_key (hop);
        // A consumed body stream cannot be replayed on a stale-connection
        // retry, so streamed uploads always use a fresh connection.
        const bool can_reuse = !_request.body_provider;

        for (int attempt = 0; attempt < 2; ++attempt) {
            std::unique_ptr<pooled_connection_t> connection;
            bool reused = false;
            if (can_reuse && attempt == 0) {
                connection = _pool.acquire (key);
                reused = connection != nullptr;
            }
            if (!connection) {
                connection = open_connection (hop);
            }

            try {
                auto outcome = run_exchange (*connection, hop, method, body);
                if (outcome.reusable && can_reuse) {
                    _pool.release (key, std::move (connection));
                }
                return std::move (outcome.response);
            }
            catch (...) {
                if (!reused) {
                    throw;
                }
                // The pooled connection went stale; retry once on a fresh one.
            }
        }
        throw request_error ("HTTP connection retry exhausted");
    }

    exchange_outcome_t run_exchange (pooled_connection_t &connection,
                                     const hop_target_t &hop,
                                     http_method_t method,
                                     const std::optional<std::string> &body)
    {
        if (connection.plain) {
            connection.plain->expires_after (effective_timeout ());
            return run_exchange_on (*connection.plain, connection.buffer, hop, method, body);
        }
#ifdef ZLINK_HTTP_CLIENT_WITH_OPENSSL
        if (connection.secure) {
            beast::get_lowest_layer (*connection.secure).expires_after (effective_timeout ());
            return run_exchange_on (*connection.secure, connection.buffer, hop, method, body);
        }
#endif
        throw request_error ("HTTP connection is not open");
    }

    template <typename TStream>
    exchange_outcome_t run_exchange_on (TStream &stream,
                                        beast::flat_buffer &buffer,
                                        const hop_target_t &hop,
                                        http_method_t method,
                                        const std::optional<std::string> &body)
    {
        const bool absolute_form = _options.proxy.has_value () && hop.scheme == "http";

        if (_request.body_provider) {
            auto wire = build_wire_request<http::buffer_body> (hop, method, absolute_form);
            wire.chunked (true);
            send_streamed (stream, wire, _request.body_provider);
        } else {
            auto wire = build_wire_request<http::string_body> (hop, method, absolute_form);
            if (body) {
                wire.body () = *body;
                wire.prepare_payload ();
            }
            http::write (stream, wire);
        }
        return receive (stream, buffer);
    }

    template <typename TStream>
    static void send_streamed (TStream &stream,
                               http::request<http::buffer_body> &request,
                               const std::function<std::optional<std::string> ()> &provider)
    {
        http::request_serializer<http::buffer_body> serializer{request};
        beast::error_code ec;
        http::write_header (stream, serializer, ec);
        if (ec) {
            throw boost::system::system_error (ec);
        }

        while (!serializer.is_done ()) {
            auto chunk = provider ();
            if (chunk && chunk->empty ()) {
                continue;
            }
            if (chunk) {
                request.body ().data = chunk->data ();
                request.body ().size = chunk->size ();
                request.body ().more = true;
            } else {
                request.body ().data = nullptr;
                request.body ().size = 0;
                request.body ().more = false;
            }
            http::write (stream, serializer, ec);
            if (ec == http::error::need_buffer) {
                ec = {};
            }
            if (ec) {
                throw boost::system::system_error (ec);
            }
        }
    }

    template <typename TStream>
    exchange_outcome_t receive (TStream &stream, beast::flat_buffer &buffer)
    {
        const bool skip_body = _request.method == http_method_t::head;

        if (!_request.sink) {
            http::response_parser<http::string_body> parser;
            parser.body_limit ((std::numeric_limits<std::uint64_t>::max) ());
            parser.skip (skip_body);
            http::read (stream, buffer, parser);
            const bool reusable = parser.get ().keep_alive ();
            return {parser.release (), reusable};
        }

        http::response_parser<http::buffer_body> parser;
        parser.body_limit ((std::numeric_limits<std::uint64_t>::max) ());
        parser.skip (skip_body);
        http::read_header (stream, buffer, parser);

        // While following redirects, intermediate bodies are drained instead
        // of being delivered to the caller's sink.
        const bool draining =
          _options.follow_redirects > 0
          && is_redirect_status (static_cast<int> (parser.get ().result_int ()))
          && parser.get ().count (http::field::location) > 0;

        char chunk[16384];
        while (!parser.is_done ()) {
            parser.get ().body ().data = chunk;
            parser.get ().body ().size = sizeof chunk;
            beast::error_code ec;
            http::read (stream, buffer, parser, ec);
            if (ec && ec != http::error::need_buffer) {
                throw boost::system::system_error (ec);
            }
            const auto produced = sizeof chunk - parser.get ().body ().size;
            if (!draining && produced > 0) {
                _request.sink (std::string_view (chunk, produced));
            }
        }

        const bool reusable = parser.get ().keep_alive ();
        http::response<http::string_body> response;
        response.base () = parser.get ().base ();
        return {std::move (response), reusable};
    }

    std::unique_ptr<pooled_connection_t> open_connection (const hop_target_t &hop)
    {
        auto connection = std::make_unique<pooled_connection_t> ();
        tcp::resolver resolver (connection->io);

        const bool via_proxy = _options.proxy.has_value ();
        std::string connect_host = hop.host;
        std::string connect_port = hop.port;
        if (via_proxy) {
            const auto proxy = parse_base_url (*_options.proxy);
            connect_host = proxy.host;
            connect_port = proxy.port;
        }
        const auto endpoints = resolver.resolve (connect_host, connect_port);

        if (hop.scheme == "http") {
            connection->plain.emplace (connection->io);
            connection->plain->expires_after (effective_timeout ());
            connection->plain->connect (endpoints);
            return connection;
        }

#ifdef ZLINK_HTTP_CLIENT_WITH_OPENSSL
        connection->tls_context.emplace (asio::ssl::context::tls_client);
        auto &context = *connection->tls_context;
        context.set_default_verify_paths ();
        if (_options.trust_certificate_file) {
            context.load_verify_file (*_options.trust_certificate_file);
        }
        if (_options.client_certificate) {
            context.use_certificate_chain_file (_options.client_certificate->first);
            context.use_private_key_file (_options.client_certificate->second,
                                          asio::ssl::context::pem);
        }

        connection->secure.emplace (connection->io, context);
        auto &stream = *connection->secure;
        SSL_set_tlsext_host_name (stream.native_handle (), hop.host.c_str ());
        stream.set_verify_mode (asio::ssl::verify_peer);
        stream.set_verify_callback (asio::ssl::host_name_verification (hop.host));
        beast::get_lowest_layer (stream).expires_after (effective_timeout ());
        beast::get_lowest_layer (stream).connect (endpoints);
        if (via_proxy) {
            open_proxy_tunnel (beast::get_lowest_layer (stream), hop);
        }
        stream.handshake (asio::ssl::stream_base::client);
        return connection;
#else
        throw zlink::framework::framework_exception_t (
          zlink::framework::framework_error_kind_t::request_protocol_error,
          "HTTPS support requires OpenSSL");
#endif
    }

    void open_proxy_tunnel (beast::tcp_stream &stream, const hop_target_t &hop) const
    {
        const auto authority = hop.host + ":" + hop.port;
        http::request<http::empty_body> connect_request{http::verb::connect, authority, 11};
        connect_request.set (http::field::host, authority);
        if (_options.proxy_authorization) {
            connect_request.set (http::field::proxy_authorization,
                                 *_options.proxy_authorization);
        }
        http::write (stream, connect_request);

        beast::flat_buffer buffer;
        http::response_parser<http::empty_body> parser;
        parser.skip (true);
        http::read (stream, buffer, parser);
        if (parser.get ().result () != http::status::ok) {
            throw request_error ("HTTP proxy CONNECT failed with status "
                                 + std::to_string (parser.get ().result_int ()));
        }
    }

    const http_client_options_t &_options;
    cookie_jar_t &_cookie_jar;
    connection_pool_t &_pool;
    const http_request_t &_request;
};

zlink::framework::result_t<raw_http_response_t>
perform_once (const http_client_options_t &options,
              cookie_jar_t &cookie_jar,
              connection_pool_t &pool,
              const http_request_t &request)
{
    try {
        return request_performer_t (options, cookie_jar, pool, request).perform ();
    }
    catch (const zlink::framework::framework_exception_t &ex) {
        return zlink::framework::result_t<raw_http_response_t>::failure (ex.kind (), ex.what (),
                                                                         ex.is_retriable ());
    }
    catch (const boost::system::system_error &ex) {
        if (ex.code () == boost::beast::error::timeout
            || ex.code () == boost::asio::error::timed_out) {
            return zlink::framework::result_t<raw_http_response_t>::failure (
              zlink::framework::framework_error_kind_t::timeout, ex.what (), true);
        }
        return map_exception (ex);
    }
    catch (const std::exception &ex) {
        return map_exception (ex);
    }
}

} // namespace

zlink::framework::result_t<raw_http_response_t>
http_client_runtime_t::execute (const http_request_t &request) const
{
    // Streamed uploads and downloads cannot be replayed, so they are excluded
    // from automatic retry.
    const int max_retries =
      (request.sink || request.body_provider) ? 0 : _options.retry_attempts;

    for (int attempt = 0;; ++attempt) {
        auto result = perform_once (_options, _cookie_jar, *_connection_pool, request);
        if (result.has_value () || attempt >= max_retries
            || !result.error ()->is_retriable ()) {
            return result;
        }
        std::this_thread::sleep_for (std::chrono::milliseconds (50));
    }
}

} // namespace zlink::http_client::detail
