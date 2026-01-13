/* SPDX-License-Identifier: MPL-2.0 */

#include "../precompiled.hpp"
#include "ssl_transport.hpp"

#if defined ZMQ_IOTHREAD_POLLER_USE_ASIO && defined ZMQ_HAVE_ASIO_SSL

#include "asio_debug.hpp"
#include "asio_error_handler.hpp"
#include "../address.hpp"

#include <openssl/ssl.h>

namespace zmq
{
namespace
{
boost::asio::ip::tcp protocol_for_fd (fd_t fd_)
{
    sockaddr_storage ss;
    const zmq_socklen_t sl = get_socket_address (fd_, socket_end_local, &ss);
    if (sl != 0 && ss.ss_family == AF_INET6)
        return boost::asio::ip::tcp::v6 ();
    return boost::asio::ip::tcp::v4 ();
}
}

ssl_transport_t::ssl_transport_t (boost::asio::ssl::context &ssl_ctx) :
    _ssl_ctx (ssl_ctx),
    _handshake_complete (false)
{
}

ssl_transport_t::~ssl_transport_t ()
{
    close ();
}

bool ssl_transport_t::open (boost::asio::io_context &io_context, fd_t fd)
{
    //  Close any existing stream
    close ();

    //  Create the underlying TCP socket
    boost::asio::ip::tcp::socket socket (io_context);
    boost::system::error_code ec;

    //  Assign the file descriptor to the socket
    socket.assign (protocol_for_fd (fd), fd, ec);
    if (ec) {
        ASIO_GLOBAL_ERROR ("ssl_transport assign failed: %s",
                          ec.message ().c_str ());
        return false;
    }

    //  Create SSL stream wrapping the socket
    try {
        _ssl_stream = std::unique_ptr<ssl_stream_t> (
          new ssl_stream_t (std::move (socket), _ssl_ctx));
    } catch (const std::bad_alloc &) {
        ASIO_GLOBAL_ERROR ("ssl_transport stream allocation failed");
        return false;
    }

    _handshake_complete = false;
    return true;
}

bool ssl_transport_t::is_open () const
{
    return _ssl_stream && _ssl_stream->lowest_layer ().is_open ();
}

void ssl_transport_t::close ()
{
    if (_ssl_stream) {
        boost::system::error_code ec;

        //  Avoid blocking SSL shutdown on close; just close the TCP layer.
        _ssl_stream->lowest_layer ().shutdown (
          boost::asio::ip::tcp::socket::shutdown_both, ec);
        _ssl_stream->lowest_layer ().close (ec);

        _ssl_stream.reset ();
        _handshake_complete = false;
    }
}

void ssl_transport_t::async_read_some (unsigned char *buffer,
                                       std::size_t buffer_size,
                                       completion_handler_t handler)
{
    if (!_ssl_stream || !_handshake_complete) {
        if (handler) {
            handler (boost::asio::error::not_connected, 0);
        }
        return;
    }

    _ssl_stream->async_read_some (boost::asio::buffer (buffer, buffer_size),
                                  handler);
}

void ssl_transport_t::async_write_some (const unsigned char *buffer,
                                        std::size_t buffer_size,
                                        completion_handler_t handler)
{
    if (!_ssl_stream || !_handshake_complete) {
        if (handler) {
            handler (boost::asio::error::not_connected, 0);
        }
        return;
    }

    boost::asio::async_write (*_ssl_stream,
                              boost::asio::buffer (buffer, buffer_size),
                              handler);
}

void ssl_transport_t::async_handshake (int handshake_type,
                                       completion_handler_t handler)
{
    if (!_ssl_stream) {
        if (handler) {
            handler (boost::asio::error::not_connected, 0);
        }
        return;
    }

    auto hs_type =
      (handshake_type == 0)
        ? boost::asio::ssl::stream_base::client
        : boost::asio::ssl::stream_base::server;

    if (handshake_type == client && !_hostname.empty ()) {
        if (!SSL_set_tlsext_host_name (_ssl_stream->native_handle (),
                                       _hostname.c_str ())) {
            if (handler) {
                handler (boost::asio::error::invalid_argument, 0);
            }
            return;
        }
    }

    _ssl_stream->async_handshake (
      hs_type,
      [this, handler] (const boost::system::error_code &ec) {
          if (!ec) {
              _handshake_complete = true;
          }
          if (handler) {
              handler (ec, 0);
          }
      });
}

}  // namespace zmq

#endif  // ZMQ_IOTHREAD_POLLER_USE_ASIO && ZMQ_HAVE_ASIO_SSL
