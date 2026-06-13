/* SPDX-License-Identifier: MPL-2.0 */

#include "runtime/transport/stream_connection.hpp"

#include <boost/asio/connect.hpp>
#include <boost/asio/write.hpp>
#ifdef ZLINK_STREAM_CONNECTOR_WITH_OPENSSL
#include <boost/asio/ssl/host_name_verification.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <openssl/ssl.h>
#endif

#ifndef _WIN32
#include <poll.h>
#endif

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <memory>

namespace zlink::stream_connector::detail
{

namespace
{

#ifndef _WIN32
bool wait_native_socket_readable (int native_handle,
                                  std::chrono::steady_clock::time_point deadline,
                                  boost::system::error_code &error)
{
    for (;;) {
        const auto now = std::chrono::steady_clock::now ();
        if (now >= deadline) {
            error.clear ();
            return false;
        }
        const auto timeout =
          static_cast<int> (std::chrono::duration_cast<std::chrono::milliseconds> (deadline - now)
                              .count ());
        pollfd descriptor{};
        descriptor.fd = native_handle;
        descriptor.events = POLLIN;
        const auto ready = ::poll (&descriptor, 1, std::max (0, timeout));
        if (ready > 0) {
            error.clear ();
            return true;
        }
        if (ready == 0) {
            error.clear ();
            return false;
        }
        if (errno == EINTR) {
            continue;
        }
        error.assign (errno, boost::system::generic_category ());
        return false;
    }
}
#endif

class tcp_stream_connection_t final : public stream_connection_t
{
  public:
    explicit tcp_stream_connection_t (boost::asio::ip::tcp::socket socket) :
        _socket (std::move (socket))
    {
    }

    bool is_open () const override { return _socket.is_open (); }

    std::size_t available (boost::system::error_code &error) override
    {
        return _socket.available (error);
    }

    std::size_t
    read_some (std::uint8_t *buffer, std::size_t size, boost::system::error_code &error) override
    {
        return _socket.read_some (boost::asio::buffer (buffer, size), error);
    }

    void async_read_some (
      std::size_t max_size,
      std::function<void (boost::system::error_code, std::vector<std::uint8_t>)> completion) override
    {
        auto buffer = std::make_shared<std::vector<std::uint8_t>> (max_size);
        _socket.async_read_some (
          boost::asio::buffer (*buffer),
          [buffer, completion = std::move (completion)] (boost::system::error_code error,
                                                         std::size_t bytes_read) mutable {
              buffer->resize (bytes_read);
              if (completion) {
                  completion (error, std::move (*buffer));
              }
          });
    }

    bool wait_readable_until (std::chrono::steady_clock::time_point deadline,
                              boost::system::error_code &error) override
    {
#ifdef _WIN32
        (void) deadline;
        error.clear ();
        return true;
#else
        return wait_native_socket_readable (_socket.native_handle (), deadline, error);
#endif
    }

    void write (const std::vector<std::uint8_t> &bytes) override
    {
        boost::asio::write (_socket, boost::asio::buffer (bytes));
    }

    void async_write (std::vector<std::uint8_t> bytes,
                      std::function<void (boost::system::error_code)> completion) override
    {
        auto buffer = std::make_shared<std::vector<std::uint8_t>> (std::move (bytes));
        boost::asio::async_write (
          _socket, boost::asio::buffer (*buffer),
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
        _socket.shutdown (boost::asio::ip::tcp::socket::shutdown_both, ignored);
        _socket.close (ignored);
    }

    void close (boost::system::error_code &error) override { _socket.close (error); }

  private:
    boost::asio::ip::tcp::socket _socket;
};

#ifdef ZLINK_STREAM_CONNECTOR_WITH_OPENSSL
namespace ssl = boost::asio::ssl;

class tls_stream_connection_t final : public stream_connection_t
{
  public:
    explicit tls_stream_connection_t (ssl::stream<boost::asio::ip::tcp::socket> stream) :
        _stream (std::move (stream))
    {
    }

    bool is_open () const override { return _stream.next_layer ().is_open (); }

    std::size_t available (boost::system::error_code &error) override
    {
        return _stream.next_layer ().available (error);
    }

    std::size_t
    read_some (std::uint8_t *buffer, std::size_t size, boost::system::error_code &error) override
    {
        return _stream.read_some (boost::asio::buffer (buffer, size), error);
    }

    void async_read_some (
      std::size_t max_size,
      std::function<void (boost::system::error_code, std::vector<std::uint8_t>)> completion) override
    {
        auto buffer = std::make_shared<std::vector<std::uint8_t>> (max_size);
        _stream.async_read_some (
          boost::asio::buffer (*buffer),
          [buffer, completion = std::move (completion)] (boost::system::error_code error,
                                                         std::size_t bytes_read) mutable {
              buffer->resize (bytes_read);
              if (completion) {
                  completion (error, std::move (*buffer));
              }
          });
    }

    bool wait_readable_until (std::chrono::steady_clock::time_point deadline,
                              boost::system::error_code &error) override
    {
#ifdef _WIN32
        (void) deadline;
        error.clear ();
        return true;
#else
        return wait_native_socket_readable (_stream.next_layer ().native_handle (), deadline, error);
#endif
    }

    void write (const std::vector<std::uint8_t> &bytes) override
    {
        boost::asio::write (_stream, boost::asio::buffer (bytes));
    }

    void async_write (std::vector<std::uint8_t> bytes,
                      std::function<void (boost::system::error_code)> completion) override
    {
        auto buffer = std::make_shared<std::vector<std::uint8_t>> (std::move (bytes));
        boost::asio::async_write (
          _stream, boost::asio::buffer (*buffer),
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
        _stream.shutdown (ignored);
        _stream.next_layer ().close (ignored);
    }

    void close (boost::system::error_code &error) override { _stream.next_layer ().close (error); }

  private:
    ssl::stream<boost::asio::ip::tcp::socket> _stream;
};
#endif

} // namespace

std::unique_ptr<stream_connection_t> make_tcp_connection (boost::asio::ip::tcp::socket socket)
{
    return std::make_unique<tcp_stream_connection_t> (std::move (socket));
}

namespace
{

std::optional<endpoint_parts_t> parse_host_port_endpoint (const std::string &endpoint,
                                                          std::string_view prefix)
{
    if (endpoint.rfind (std::string (prefix), 0) != 0) {
        return std::nullopt;
    }
    const auto host_start = prefix.size ();
    const auto colon = endpoint.rfind (':');
    if (colon == std::string::npos || colon <= host_start || colon + 1 >= endpoint.size ()) {
        return std::nullopt;
    }
    return endpoint_parts_t{endpoint.substr (host_start, colon - host_start),
                            endpoint.substr (colon + 1)};
}

} // namespace

std::optional<endpoint_parts_t> parse_tcp_endpoint (const std::string &endpoint)
{
    return parse_host_port_endpoint (endpoint, "tcp://");
}

std::optional<endpoint_parts_t> parse_tls_endpoint (const std::string &endpoint)
{
    return parse_host_port_endpoint (endpoint, "tls://");
}

#ifdef ZLINK_STREAM_CONNECTOR_WITH_OPENSSL
std::unique_ptr<stream_connection_t> connect_tls (boost::asio::io_context &io_context,
                                                  const endpoint_parts_t &endpoint,
                                                  bool skip_server_certificate_validation)
{
    ssl::context context (ssl::context::tls_client);
    context.set_default_verify_paths ();
    ssl::stream<boost::asio::ip::tcp::socket> stream (io_context, context);
    SSL_set_tlsext_host_name (stream.native_handle (), endpoint.host.c_str ());
    if (skip_server_certificate_validation) {
        stream.set_verify_mode (ssl::verify_none);
    } else {
        stream.set_verify_mode (ssl::verify_peer);
        stream.set_verify_callback (ssl::host_name_verification (endpoint.host));
    }

    boost::asio::ip::tcp::resolver resolver (io_context);
    auto endpoints = resolver.resolve (endpoint.host, endpoint.port);
    boost::asio::connect (stream.next_layer (), endpoints);
    stream.handshake (ssl::stream_base::client);
    return std::make_unique<tls_stream_connection_t> (std::move (stream));
}

void connect_tls_async (
  boost::asio::io_context &io_context,
  endpoint_parts_t endpoint,
  bool skip_server_certificate_validation,
  std::function<void (boost::system::error_code, std::unique_ptr<stream_connection_t>)> callback)
{
    auto context = std::make_shared<ssl::context> (ssl::context::tls_client);
    context->set_default_verify_paths ();
    auto stream =
      std::make_shared<ssl::stream<boost::asio::ip::tcp::socket>> (io_context, *context);
    SSL_set_tlsext_host_name (stream->native_handle (), endpoint.host.c_str ());
    if (skip_server_certificate_validation) {
        stream->set_verify_mode (ssl::verify_none);
    } else {
        stream->set_verify_mode (ssl::verify_peer);
        stream->set_verify_callback (ssl::host_name_verification (endpoint.host));
    }

    const auto host = endpoint.host;
    const auto port = endpoint.port;
    auto resolver = std::make_shared<boost::asio::ip::tcp::resolver> (io_context);
    resolver->async_resolve (
      host, port,
      [resolver, context, stream, callback = std::move (callback)] (
        boost::system::error_code error,
        boost::asio::ip::tcp::resolver::results_type endpoints) mutable {
          if (error) {
              callback (error, nullptr);
              return;
          }
          boost::asio::async_connect (
            stream->next_layer (), endpoints,
            [context, stream, callback = std::move (callback)] (
              boost::system::error_code connect_error,
              const boost::asio::ip::tcp::endpoint &) mutable {
                if (connect_error) {
                    callback (connect_error, nullptr);
                    return;
                }
                stream->async_handshake (
                  ssl::stream_base::client,
                  [context, stream, callback = std::move (callback)] (
                    boost::system::error_code handshake_error) mutable {
                      if (handshake_error) {
                          callback (handshake_error, nullptr);
                          return;
                      }
                      callback ({}, std::make_unique<tls_stream_connection_t> (std::move (*stream)));
                  });
            });
      });
}
#endif

bool is_transport_connected (const connector_state_t &state)
{
    return state.state == connection_state_t::connected && state.connection
           && state.connection->is_open ();
}

std::vector<std::uint8_t> read_exact (connector_state_t &state, std::size_t size)
{
    std::vector<std::uint8_t> bytes (size);
    std::size_t offset = 0;
    while (offset < size) {
        boost::system::error_code error;
        const auto read =
          state.connection->read_some (bytes.data () + offset, bytes.size () - offset, error);
        if (error) {
            throw boost::system::system_error (error);
        }
        offset += read;
    }
    return bytes;
}

void write_bytes (connector_state_t &state, const std::vector<std::uint8_t> &bytes)
{
    state.connection->write (bytes);
}

} // namespace zlink::stream_connector::detail
