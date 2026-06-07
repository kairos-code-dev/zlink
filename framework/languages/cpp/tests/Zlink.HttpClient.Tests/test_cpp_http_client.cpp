/* SPDX-License-Identifier: MPL-2.0 */

#include <zlink/http_client.hpp>

#include <boost/asio/ip/tcp.hpp>
#ifdef ZLINK_HTTP_CLIENT_TEST_WITH_OPENSSL
#include <boost/asio/ssl/stream.hpp>
#endif
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#ifdef ZLINK_HTTP_CLIENT_TEST_WITH_OPENSSL
#include <boost/beast/ssl.hpp>
#endif

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <thread>
#include <utility>

namespace
{
namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
using tcp = asio::ip::tcp;

struct create_game_request_t
{
    std::string name;
};

struct create_game_reply_t
{
    std::string method;
    std::string name;
};

struct header_echo_reply_t
{
    std::string defaultHeader;
    std::string overrideHeader;
};

void to_json (nlohmann::json &json, const create_game_request_t &value)
{
    json = nlohmann::json{{"name", value.name}};
}

void from_json (const nlohmann::json &json, create_game_reply_t &value)
{
    value.method = json.at ("method").get<std::string> ();
    value.name = json.at ("name").get<std::string> ();
}

void from_json (const nlohmann::json &json, header_echo_reply_t &value)
{
    value.defaultHeader = json.at ("defaultHeader").get<std::string> ();
    value.overrideHeader = json.at ("overrideHeader").get<std::string> ();
}

http::response<http::string_body> make_response (const http::request<http::string_body> &request)
{
    const auto target = std::string (request.target ());
    http::response<http::string_body> response{http::status::ok, request.version ()};
    response.set (http::field::content_type, "application/json");

    if (target == "/missing") {
        response.result (http::status::not_found);
        response.body () = R"({"error":"missing"})";
    } else if (target == "/bad-request") {
        response.result (http::status::bad_request);
        response.body () = R"({"error":"bad request"})";
    } else if (target == "/server-error") {
        response.result (http::status::internal_server_error);
        response.body () = R"({"error":"server error"})";
    } else if (target == "/invalid-json") {
        response.body () = "{";
    } else if (target == "/slow") {
        std::this_thread::sleep_for (std::chrono::milliseconds (250));
        response.body () = R"({"method":"GET","name":"slow"})";
    } else if (target == "/headers") {
        response.body () =
          nlohmann::json{{"defaultHeader", std::string (request[http::field::from])},
                         {"overrideHeader", std::string (request["X-ZLink-Override"])}}
            .dump ();
    } else {
        const auto method = std::string (request.method_string ());
        std::string name = method;
        if (!request.body ().empty ()) {
            name = nlohmann::json::parse (request.body ()).at ("name").get<std::string> ();
        }
        response.body () = nlohmann::json{{"method", method}, {"name", name}}.dump ();
    }

    response.prepare_payload ();
    return response;
}

class loopback_http_server_t
{
  public:
    loopback_http_server_t () :
        _acceptor (_io, tcp::endpoint (asio::ip::make_address ("127.0.0.1"), 0))
    {
        _port = _acceptor.local_endpoint ().port ();
        _thread = std::thread ([this] { run (); });
    }

    ~loopback_http_server_t ()
    {
        _stop = true;
        beast::error_code ignored;
        _acceptor.close (ignored);
        try {
            tcp::socket wakeup (_io);
            wakeup.connect (tcp::endpoint (asio::ip::make_address ("127.0.0.1"), _port), ignored);
        }
        catch (...) {
        }
        if (_thread.joinable ()) {
            _thread.join ();
        }
    }

    std::string base_url () const { return "http://127.0.0.1:" + std::to_string (_port); }

  private:
    void run ()
    {
        while (!_stop) {
            beast::error_code ec;
            tcp::socket socket (_io);
            _acceptor.accept (socket, ec);
            if (ec) {
                continue;
            }
            handle (std::move (socket));
        }
    }

    void handle (tcp::socket socket)
    {
        beast::flat_buffer buffer;
        http::request<http::string_body> request;
        beast::error_code ec;
        http::read (socket, buffer, request, ec);
        if (ec) {
            return;
        }

        auto response = make_response (request);
        http::write (socket, response, ec);
        socket.shutdown (tcp::socket::shutdown_send, ec);
    }

    asio::io_context _io;
    tcp::acceptor _acceptor;
    std::uint16_t _port = 0;
    std::atomic_bool _stop{false};
    std::thread _thread;
};

#ifdef ZLINK_HTTP_CLIENT_TEST_WITH_OPENSSL
class loopback_https_server_t
{
  public:
    loopback_https_server_t () :
        _context (asio::ssl::context::tls_server),
        _acceptor (_io, tcp::endpoint (asio::ip::make_address ("127.0.0.1"), 0))
    {
        _context.use_certificate_chain_file (ZLINK_HTTP_CLIENT_TEST_CERT);
        _context.use_private_key_file (ZLINK_HTTP_CLIENT_TEST_KEY, asio::ssl::context::pem);
        _port = _acceptor.local_endpoint ().port ();
        _thread = std::thread ([this] { run (); });
    }

    ~loopback_https_server_t ()
    {
        _stop = true;
        beast::error_code ignored;
        _acceptor.close (ignored);
        try {
            tcp::socket wakeup (_io);
            wakeup.connect (tcp::endpoint (asio::ip::make_address ("127.0.0.1"), _port), ignored);
        }
        catch (...) {
        }
        if (_thread.joinable ()) {
            _thread.join ();
        }
    }

    std::string base_url () const { return "https://localhost:" + std::to_string (_port); }

    std::string mismatched_base_url () const
    {
        return "https://127.0.0.1:" + std::to_string (_port);
    }

  private:
    void run ()
    {
        while (!_stop) {
            beast::error_code ec;
            tcp::socket socket (_io);
            _acceptor.accept (socket, ec);
            if (ec) {
                continue;
            }
            handle (std::move (socket));
        }
    }

    void handle (tcp::socket socket)
    {
        asio::ssl::stream<tcp::socket> stream (std::move (socket), _context);
        beast::error_code ec;
        stream.handshake (asio::ssl::stream_base::server, ec);
        if (ec) {
            return;
        }

        beast::flat_buffer buffer;
        http::request<http::string_body> request;
        http::read (stream, buffer, request, ec);
        if (ec) {
            return;
        }

        auto response = make_response (request);
        http::write (stream, response, ec);
        stream.shutdown (ec);
    }

    asio::io_context _io;
    asio::ssl::context _context;
    tcp::acceptor _acceptor;
    std::uint16_t _port = 0;
    std::atomic_bool _stop{false};
    std::thread _thread;
};
#endif

zlink::http_client::client_t
make_json_client (std::string base_url,
                  std::chrono::milliseconds timeout = std::chrono::milliseconds (500),
                  std::optional<std::string> trust_certificate_file = std::nullopt)
{
    auto builder = zlink::http_client::client_t::create ()
                     .base_url (std::move (base_url))
                     .json ()
                     .timeout (timeout);
    if (trust_certificate_file) {
        builder.trust_certificate_file (std::move (*trust_certificate_file));
    }
    return builder.build ();
}

zlink::framework::task_t<zlink::http_client::http_response_t<create_game_reply_t>>
create_game_with_coroutine_submit (const zlink::http_client::client_t &client)
{
    auto response = co_await client.post ("/games")
                      .body (create_game_request_t{.name = "coawait"})
                      .submit<create_game_reply_t> ();
    co_return response;
}

template <typename TAction>
bool throws_protocol_error (TAction &&action, const std::string &message_part)
{
    try {
        action ();
    }
    catch (const zlink::framework::framework_exception_t &error) {
        return error.kind () == zlink::framework::framework_error_kind_t::request_protocol_error
               && std::string (error.what ()).find (message_part) != std::string::npos;
    }
    return false;
}

} // namespace

TEST (ZLinkHttpClient, ValidatesFluentInputAsProtocolErrors)
{
    EXPECT_TRUE (throws_protocol_error (
      [] { (void) zlink::http_client::client_t::create ().build (); }, "base_url"));

    EXPECT_TRUE (throws_protocol_error (
      [] { (void) zlink::http_client::client_t::create ().base_url ("ftp://host").build (); },
      "http:// or https://"));

    EXPECT_TRUE (throws_protocol_error (
      [] {
          (void) zlink::http_client::client_t::create ()
            .base_url ("http://127.0.0.1")
            .timeout (std::chrono::milliseconds (0));
      },
      "timeout"));

    EXPECT_TRUE (throws_protocol_error (
      [] {
          (void) zlink::http_client::client_t::create ()
            .base_url ("http://127.0.0.1")
            .default_header (" ", "value");
      },
      "header name"));

    EXPECT_TRUE (throws_protocol_error (
      [] {
          (void) zlink::http_client::client_t::create ()
            .base_url ("http://127.0.0.1")
            .trust_certificate_file (" ");
      },
      "trust certificate"));

    auto client =
      zlink::http_client::client_t::create ().base_url ("http://127.0.0.1").json ().build ();
    EXPECT_TRUE (
      throws_protocol_error ([&client] { (void) client.get ("missing-leading-slash"); }, "path"));

    EXPECT_TRUE (throws_protocol_error (
      [&client] { (void) client.get ("/games").header (" ", "value"); }, "header name"));
}

TEST (ZLinkHttpClient, ContractBuilderSubmitsTypedJsonRequests)
{
    loopback_http_server_t server;
    auto client = make_json_client (server.base_url ());

    auto result = client.post ("/games")
                    .body (create_game_request_t{.name = "match-1"})
                    .submit<create_game_reply_t> ()
                    .result ();

    ASSERT_TRUE (result) << result.error ()->what ();
    EXPECT_EQ (result.value ().status, 200);
    EXPECT_EQ (result.value ().body.method, "POST");
    EXPECT_EQ (result.value ().body.name, "match-1");
}

TEST (ZLinkHttpClient, SupportsCoroutineSubmit)
{
    loopback_http_server_t server;
    auto client = make_json_client (server.base_url ());

    auto result = create_game_with_coroutine_submit (client).result ();

    ASSERT_TRUE (result) << result.error ()->what ();
    EXPECT_EQ (result.value ().status, 200);
    EXPECT_EQ (result.value ().body.method, "POST");
    EXPECT_EQ (result.value ().body.name, "coawait");
}

TEST (ZLinkHttpClient, SupportsCommonMethodsAndCallbackSubmit)
{
    loopback_http_server_t server;
    auto client = make_json_client (server.base_url ());

    EXPECT_EQ (client.get ("/games").submit<create_game_reply_t> ().result ().value ().body.method,
               "GET");
    EXPECT_EQ (client.put ("/games")
                 .body (create_game_request_t{.name = "put"})
                 .submit<create_game_reply_t> ()
                 .result ()
                 .value ()
                 .body.method,
               "PUT");
    EXPECT_EQ (
      client.delete_ ("/games").submit<create_game_reply_t> ().result ().value ().body.method,
      "DELETE");

    bool callback_called = false;
    client.post ("/games")
      .body (create_game_request_t{.name = "callback"})
      .submit<create_game_reply_t> ([&callback_called] (const auto &result) {
          callback_called = true;
          ASSERT_TRUE (result);
          EXPECT_EQ (result.value ().body.name, "callback");
      });
    EXPECT_TRUE (callback_called);

    bool failure_callback_called = false;
    client.get ("/invalid-json")
      .submit<create_game_reply_t> ([&failure_callback_called] (const auto &result) {
          failure_callback_called = true;
          ASSERT_FALSE (result);
          EXPECT_EQ (result.error_kind (),
                     zlink::framework::framework_error_kind_t::payload_decode_failed);
      });
    EXPECT_TRUE (failure_callback_called);
}

TEST (ZLinkHttpClient, SendsDefaultHeadersAndRequestOverride)
{
    loopback_http_server_t server;
    auto client = zlink::http_client::client_t::create ()
                    .base_url (server.base_url ())
                    .json ()
                    .default_header ("From", "default@example.test")
                    .default_header ("X-ZLink-Override", "default")
                    .build ();

    auto result = client.get ("/headers")
                    .header ("X-ZLink-Override", "request")
                    .submit<header_echo_reply_t> ()
                    .result ();

    ASSERT_TRUE (result) << result.error ()->what ();
    EXPECT_EQ (result.value ().body.defaultHeader, "default@example.test");
    EXPECT_EQ (result.value ().body.overrideHeader, "request");
}

TEST (ZLinkHttpClient, MapsStatusDecodeAndTimeoutFailures)
{
    loopback_http_server_t server;
    auto client = make_json_client (server.base_url (), std::chrono::milliseconds (50));

    const auto missing = client.get ("/missing").submit<create_game_reply_t> ().result ();
    ASSERT_FALSE (missing);
    EXPECT_EQ (missing.error_kind (), zlink::framework::framework_error_kind_t::request_failed);

    const auto bad_request = client.get ("/bad-request").submit<create_game_reply_t> ().result ();
    ASSERT_FALSE (bad_request);
    EXPECT_EQ (bad_request.error_kind (), zlink::framework::framework_error_kind_t::request_failed);

    const auto server_error = client.get ("/server-error").submit<create_game_reply_t> ().result ();
    ASSERT_FALSE (server_error);
    EXPECT_EQ (server_error.error_kind (),
               zlink::framework::framework_error_kind_t::request_failed);

    const auto invalid_json = client.get ("/invalid-json").submit<create_game_reply_t> ().result ();
    ASSERT_FALSE (invalid_json);
    EXPECT_EQ (invalid_json.error_kind (),
               zlink::framework::framework_error_kind_t::payload_decode_failed);

    const auto timeout = client.get ("/slow").submit_raw ().result ();
    ASSERT_FALSE (timeout);
    EXPECT_EQ (timeout.error_kind (), zlink::framework::framework_error_kind_t::timeout);
}

#ifdef ZLINK_HTTP_CLIENT_TEST_WITH_OPENSSL
TEST (ZLinkHttpClient, SupportsHttpsWithExplicitTrust)
{
    loopback_https_server_t server;
    auto client = make_json_client (server.base_url (), std::chrono::milliseconds (500),
                                    std::string (ZLINK_HTTP_CLIENT_TEST_CERT));

    auto result = client.post ("/games")
                    .body (create_game_request_t{.name = "secure"})
                    .submit<create_game_reply_t> ()
                    .result ();

    ASSERT_TRUE (result) << result.error ()->what ();
    EXPECT_EQ (result.value ().body.method, "POST");
    EXPECT_EQ (result.value ().body.name, "secure");
}

TEST (ZLinkHttpClient, RejectsUntrustedHttpsCertificate)
{
    loopback_https_server_t server;
    auto client = make_json_client (server.base_url ());

    auto result = client.get ("/games").submit<create_game_reply_t> ().result ();

    ASSERT_FALSE (result);
    EXPECT_EQ (result.error_kind (), zlink::framework::framework_error_kind_t::request_failed);
}

TEST (ZLinkHttpClient, RejectsHttpsHostnameMismatch)
{
    loopback_https_server_t server;
    auto client = make_json_client (server.mismatched_base_url (), std::chrono::milliseconds (500),
                                    std::string (ZLINK_HTTP_CLIENT_TEST_CERT));

    auto result = client.get ("/games").submit<create_game_reply_t> ().result ();

    ASSERT_FALSE (result);
    EXPECT_EQ (result.error_kind (), zlink::framework::framework_error_kind_t::request_failed);
}
#endif

int main (int argc, char **argv)
{
    testing::InitGoogleTest (&argc, argv);
    return RUN_ALL_TESTS ();
}
