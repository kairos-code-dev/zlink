/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#include "runtime/transport/websocket_connection.hpp"

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#ifdef ZLINK_STREAM_CONNECTOR_WITH_OPENSSL
#include <boost/asio/ssl/host_name_verification.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <openssl/ssl.h>
#endif
#include <boost/beast/core/buffers_to_string.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace zlink::stream_connector::detail
{

namespace
{

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace websocket = beast::websocket;
using tcp = asio::ip::tcp;
#ifdef ZLINK_STREAM_CONNECTOR_WITH_OPENSSL
namespace ssl = asio::ssl;
#endif

template <typename TStream> class websocket_stream_connection_t final : public stream_connection_t
{
  public:
    explicit websocket_stream_connection_t (websocket::stream<TStream> stream) :
        _stream (std::move (stream))
    {
        _stream.binary (true);
    }

    bool is_open () const override { return _stream.is_open (); }

    std::size_t available (boost::system::error_code &error) override
    {
        if (!_read_buffer.empty ()) {
            return _read_buffer.size () - _read_offset;
        }

        const auto tcp_available = beast::get_lowest_layer (_stream).available (error);
        if (error || tcp_available == 0) {
            return 0;
        }

        beast::flat_buffer buffer;
        _stream.read (buffer, error);
        if (error) {
            return 0;
        }

        auto text = beast::buffers_to_string (buffer.data ());
        _read_buffer.assign (text.begin (), text.end ());
        _read_offset = 0;
        return _read_buffer.size ();
    }

    std::size_t
    read_some (std::uint8_t *buffer, std::size_t size, boost::system::error_code &error) override
    {
        if (available (error) == 0 || error) {
            return 0;
        }

        const auto remaining = _read_buffer.size () - _read_offset;
        const auto copied = std::min (remaining, size);
        std::copy_n (_read_buffer.data () + _read_offset, copied, buffer);
        _read_offset += copied;
        if (_read_offset == _read_buffer.size ()) {
            _read_buffer.clear ();
            _read_offset = 0;
        }
        return copied;
    }

    void async_read_some (
      std::size_t max_size,
      std::function<void (boost::system::error_code, std::vector<std::uint8_t>)> completion) override
    {
        (void) max_size;
        auto buffer = std::make_shared<beast::flat_buffer> ();
        _stream.async_read (
          *buffer,
          [buffer, completion = std::move (completion)] (boost::system::error_code error,
                                                         std::size_t) mutable {
              auto text = beast::buffers_to_string (buffer->data ());
              std::vector<std::uint8_t> bytes (text.begin (), text.end ());
              if (completion) {
                  completion (error, std::move (bytes));
              }
          });
    }

    void write (const std::vector<std::uint8_t> &bytes) override
    {
        _stream.binary (true);
        _stream.write (asio::buffer (bytes));
    }

    void async_write (std::vector<std::uint8_t> bytes,
                      std::function<void (boost::system::error_code)> completion) override
    {
        auto buffer = std::make_shared<std::vector<std::uint8_t>> (std::move (bytes));
        _stream.binary (true);
        _stream.async_write (
          asio::buffer (*buffer),
          [buffer, completion = std::move (completion)] (boost::system::error_code error,
                                                         std::size_t) mutable {
              if (completion) {
                  completion (error);
              }
          });
    }

    void shutdown_and_close () override
    {
        boost::system::error_code ignored;
        _stream.close (websocket::close_code::normal, ignored);
    }

    void close (boost::system::error_code &error) override
    {
        beast::get_lowest_layer (_stream).close (error);
    }

  private:
    websocket::stream<TStream> _stream;
    std::vector<std::uint8_t> _read_buffer;
    std::size_t _read_offset = 0;
};

std::optional<websocket_endpoint_parts_t>
parse_websocket_endpoint_with_prefix (const std::string &endpoint, std::string_view prefix)
{
    if (endpoint.rfind (std::string (prefix), 0) != 0) {
        return std::nullopt;
    }

    const auto host_start = prefix.size ();
    const auto path_start = endpoint.find ('/', host_start);
    const auto authority = endpoint.substr (
      host_start, path_start == std::string::npos ? std::string::npos : path_start - host_start);
    const auto colon = authority.rfind (':');
    if (colon == std::string::npos || colon == 0 || colon + 1 >= authority.size ()) {
        return std::nullopt;
    }

    return websocket_endpoint_parts_t{
      authority.substr (0, colon), authority.substr (colon + 1),
      path_start == std::string::npos ? std::string ("/") : endpoint.substr (path_start)};
}

} // namespace

std::optional<websocket_endpoint_parts_t> parse_websocket_endpoint (const std::string &endpoint)
{
    return parse_websocket_endpoint_with_prefix (endpoint, "ws://");
}

std::optional<websocket_endpoint_parts_t>
parse_websocket_secure_endpoint (const std::string &endpoint)
{
    return parse_websocket_endpoint_with_prefix (endpoint, "wss://");
}

std::unique_ptr<stream_connection_t> connect_websocket (boost::asio::io_context &io_context,
                                                        const websocket_endpoint_parts_t &endpoint)
{
    tcp::resolver resolver (io_context);
    auto endpoints = resolver.resolve (endpoint.host, endpoint.port);
    websocket::stream<tcp::socket> stream (io_context);
    asio::connect (stream.next_layer (), endpoints);
    stream.binary (true);
    stream.handshake (endpoint.host, endpoint.target);
    return std::make_unique<websocket_stream_connection_t<tcp::socket>> (std::move (stream));
}

void connect_websocket_async (
  boost::asio::io_context &io_context,
  websocket_endpoint_parts_t endpoint,
  std::function<void (boost::system::error_code, std::unique_ptr<stream_connection_t>)> callback)
{
    auto resolver = std::make_shared<tcp::resolver> (io_context);
    auto stream = std::make_shared<websocket::stream<tcp::socket>> (io_context);
    const auto host = endpoint.host;
    const auto port = endpoint.port;
    resolver->async_resolve (
      host, port,
      [resolver, stream, endpoint = std::move (endpoint), callback = std::move (callback)] (
        boost::system::error_code error, tcp::resolver::results_type endpoints) mutable {
          if (error) {
              callback (error, nullptr);
              return;
          }
          asio::async_connect (
            stream->next_layer (), endpoints,
            [stream, endpoint = std::move (endpoint), callback = std::move (callback)] (
              boost::system::error_code connect_error, const tcp::endpoint &) mutable {
                if (connect_error) {
                    callback (connect_error, nullptr);
                    return;
                }
                stream->binary (true);
                stream->async_handshake (
                  endpoint.host, endpoint.target,
                  [stream, callback = std::move (callback)] (
                    boost::system::error_code handshake_error) mutable {
                      if (handshake_error) {
                          callback (handshake_error, nullptr);
                          return;
                      }
                      callback (
                        {}, std::make_unique<websocket_stream_connection_t<tcp::socket>> (
                              std::move (*stream)));
                  });
            });
      });
}

#ifdef ZLINK_STREAM_CONNECTOR_WITH_OPENSSL
std::unique_ptr<stream_connection_t>
connect_websocket_secure (boost::asio::io_context &io_context,
                          const websocket_endpoint_parts_t &endpoint,
                          bool skip_server_certificate_validation)
{
    ssl::context context (ssl::context::tls_client);
    context.set_default_verify_paths ();
    websocket::stream<ssl::stream<tcp::socket>> stream (io_context, context);
    SSL_set_tlsext_host_name (stream.next_layer ().native_handle (), endpoint.host.c_str ());
    if (skip_server_certificate_validation) {
        stream.next_layer ().set_verify_mode (ssl::verify_none);
    } else {
        stream.next_layer ().set_verify_mode (ssl::verify_peer);
        stream.next_layer ().set_verify_callback (ssl::host_name_verification (endpoint.host));
    }

    tcp::resolver resolver (io_context);
    auto endpoints = resolver.resolve (endpoint.host, endpoint.port);
    asio::connect (beast::get_lowest_layer (stream), endpoints);
    stream.next_layer ().handshake (ssl::stream_base::client);
    stream.binary (true);
    stream.handshake (endpoint.host, endpoint.target);
    return std::make_unique<websocket_stream_connection_t<ssl::stream<tcp::socket>>> (
      std::move (stream));
}

void connect_websocket_secure_async (
  boost::asio::io_context &io_context,
  websocket_endpoint_parts_t endpoint,
  bool skip_server_certificate_validation,
  std::function<void (boost::system::error_code, std::unique_ptr<stream_connection_t>)> callback)
{
    auto context = std::make_shared<ssl::context> (ssl::context::tls_client);
    context->set_default_verify_paths ();
    auto stream =
      std::make_shared<websocket::stream<ssl::stream<tcp::socket>>> (io_context, *context);
    SSL_set_tlsext_host_name (stream->next_layer ().native_handle (), endpoint.host.c_str ());
    if (skip_server_certificate_validation) {
        stream->next_layer ().set_verify_mode (ssl::verify_none);
    } else {
        stream->next_layer ().set_verify_mode (ssl::verify_peer);
        stream->next_layer ().set_verify_callback (ssl::host_name_verification (endpoint.host));
    }

    const auto host = endpoint.host;
    const auto port = endpoint.port;
    auto resolver = std::make_shared<tcp::resolver> (io_context);
    resolver->async_resolve (
      host, port,
      [resolver, context, stream, endpoint = std::move (endpoint),
       callback = std::move (callback)] (boost::system::error_code error,
                                         tcp::resolver::results_type endpoints) mutable {
          if (error) {
              callback (error, nullptr);
              return;
          }
          asio::async_connect (
            beast::get_lowest_layer (*stream), endpoints,
            [context, stream, endpoint = std::move (endpoint), callback = std::move (callback)] (
              boost::system::error_code connect_error, const tcp::endpoint &) mutable {
                if (connect_error) {
                    callback (connect_error, nullptr);
                    return;
                }
                stream->next_layer ().async_handshake (
                  ssl::stream_base::client,
                  [context, stream, endpoint = std::move (endpoint),
                   callback = std::move (callback)] (
                    boost::system::error_code tls_error) mutable {
                      if (tls_error) {
                          callback (tls_error, nullptr);
                          return;
                      }
                      stream->binary (true);
                      stream->async_handshake (
                        endpoint.host, endpoint.target,
                        [context, stream, callback = std::move (callback)] (
                          boost::system::error_code websocket_error) mutable {
                            if (websocket_error) {
                                callback (websocket_error, nullptr);
                                return;
                            }
                            callback (
                              {}, std::make_unique<
                                    websocket_stream_connection_t<ssl::stream<tcp::socket>>> (
                                    std::move (*stream)));
                        });
                  });
            });
      });
}
#endif

} // namespace zlink::stream_connector::detail
