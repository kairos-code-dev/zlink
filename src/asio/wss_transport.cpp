/* SPDX-License-Identifier: MPL-2.0 */

#include "../precompiled.hpp"
#include "wss_transport.hpp"

#if defined ZMQ_IOTHREAD_POLLER_USE_ASIO && defined ZMQ_HAVE_ASIO_WS \
  && defined ZMQ_HAVE_ASIO_SSL

#include "asio_debug.hpp"
#include "../address.hpp"

#include <openssl/ssl.h>

//  Debug logging for WSS transport
#define ASIO_DBG_WSS(fmt, ...) ASIO_DBG_THIS ("WSS", fmt, ##__VA_ARGS__)

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

wss_transport_t::wss_transport_t (boost::asio::ssl::context &ssl_ctx,
                                  const std::string &path,
                                  const std::string &host) :
    _ssl_ctx (ssl_ctx),
    _path (path),
    _host (host),
    _ssl_handshake_complete (false),
    _ws_handshake_complete (false),
    _handshake_type (client),
    _frame_offset (0)
{
}

wss_transport_t::~wss_transport_t ()
{
    close ();
}

bool wss_transport_t::open (boost::asio::io_context &io_context, fd_t fd)
{
    //  Close any existing stream
    close ();

    //  Create the underlying TCP socket
    boost::asio::ip::tcp::socket socket (io_context);
    boost::system::error_code ec;

    //  Assign the file descriptor to the socket
    socket.assign (protocol_for_fd (fd), fd, ec);
    if (ec) {
        ASIO_GLOBAL_ERROR ("wss_transport assign failed: %s",
                          ec.message ().c_str ());
        return false;
    }

    //  Create SSL stream wrapping the socket
    ssl_stream_t ssl_stream (std::move (socket), _ssl_ctx);

    //  Create WebSocket stream wrapping the SSL stream
    try {
        _wss_stream =
          std::unique_ptr<wss_stream_t> (new wss_stream_t (std::move (ssl_stream)));
    } catch (const std::bad_alloc &) {
        ASIO_GLOBAL_ERROR ("wss_transport stream allocation failed");
        return false;
    }

    //  Configure WebSocket options
    //  Use binary mode for ZMQ messages
    _wss_stream->binary (true);

    //  Create read buffer for frame assembly
    try {
        _read_buffer = std::unique_ptr<buffer_t> (new buffer_t ());
    } catch (const std::bad_alloc &) {
        ASIO_GLOBAL_ERROR ("wss_transport buffer allocation failed");
        _wss_stream.reset ();
        return false;
    }

    _ssl_handshake_complete = false;
    _ws_handshake_complete = false;
    _frame_buffer.clear ();
    _frame_offset = 0;

    ASIO_DBG_WSS ("opened with path=%s, host=%s", _path.c_str (), _host.c_str ());
    return true;
}

bool wss_transport_t::is_open () const
{
    return _wss_stream && _wss_stream->next_layer ().next_layer ().is_open ();
}

void wss_transport_t::close ()
{
    if (_wss_stream) {
        boost::system::error_code ec;

        //  Avoid blocking WebSocket/SSL shutdown; just close the TCP layer.
        _wss_stream->next_layer ().next_layer ().shutdown (
          boost::asio::ip::tcp::socket::shutdown_both, ec);
        _wss_stream->next_layer ().next_layer ().close (ec);

        _wss_stream.reset ();
    }

    _read_buffer.reset ();

    _ssl_handshake_complete = false;
    _ws_handshake_complete = false;
    _frame_buffer.clear ();
    _frame_offset = 0;
}

void wss_transport_t::async_read_some (unsigned char *buffer,
                                       std::size_t buffer_size,
                                       completion_handler_t handler)
{
    if (!_wss_stream || !_ws_handshake_complete) {
        if (handler) {
            handler (boost::asio::error::not_connected, 0);
        }
        return;
    }

    //  If we have leftover data from a previous frame, return it first
    if (_frame_offset < _frame_buffer.size ()) {
        std::size_t available = _frame_buffer.size () - _frame_offset;
        std::size_t to_copy = std::min (available, buffer_size);
        std::memcpy (buffer, _frame_buffer.data () + _frame_offset, to_copy);
        _frame_offset += to_copy;

        //  If we consumed all frame data, clear the buffer
        if (_frame_offset >= _frame_buffer.size ()) {
            _frame_buffer.clear ();
            _frame_offset = 0;
        }

        ASIO_DBG_WSS ("read_some: returned %zu bytes from frame buffer", to_copy);

        if (handler) {
            boost::asio::post (
              _wss_stream->get_executor (),
              [handler, to_copy] () {
                  handler (boost::system::error_code (), to_copy);
              });
        }
        return;
    }

    //  Need to read a new frame from WebSocket
    _wss_stream->async_read (
      *_read_buffer,
      [this, buffer, buffer_size, handler] (
        const boost::system::error_code &ec, std::size_t bytes_transferred) {
          if (ec) {
              ASIO_DBG_WSS ("read frame failed: %s", ec.message ().c_str ());
              if (handler) {
                  handler (ec, 0);
              }
              return;
          }

          //  Copy frame data to our internal buffer
          const auto &data = _read_buffer->data ();
          const unsigned char *frame_data =
            static_cast<const unsigned char *> (data.data ());
          std::size_t frame_size = data.size ();

          ASIO_DBG_WSS ("read frame: %zu bytes", frame_size);

          //  Copy as much as possible to the caller's buffer
          std::size_t to_copy = std::min (frame_size, buffer_size);
          std::memcpy (buffer, frame_data, to_copy);

          //  If there's leftover data, store it for subsequent reads
          if (to_copy < frame_size) {
              _frame_buffer.assign (frame_data + to_copy,
                                    frame_data + frame_size);
              _frame_offset = 0;
          }

          //  Consume the data from the beast buffer
          _read_buffer->consume (_read_buffer->size ());

          if (handler) {
              handler (boost::system::error_code (), to_copy);
          }
      });
}

void wss_transport_t::async_write_some (const unsigned char *buffer,
                                        std::size_t buffer_size,
                                        completion_handler_t handler)
{
    if (!_wss_stream || !_ws_handshake_complete) {
        if (handler) {
            handler (boost::asio::error::not_connected, 0);
        }
        return;
    }

    //  WebSocket writes are frame-based
    _wss_stream->async_write (
      boost::asio::buffer (buffer, buffer_size),
      [handler] (const boost::system::error_code &ec,
                 std::size_t bytes_transferred) {
          ASIO_DBG ("WSS", "write complete: ec=%s, bytes=%zu",
                    ec.message ().c_str (), bytes_transferred);
          if (handler) {
              handler (ec, bytes_transferred);
          }
      });
}

std::size_t wss_transport_t::write_some (const std::uint8_t *data,
                                         std::size_t len)
{
    if (len == 0) {
        return 0;
    }

    //  Check transport state - both SSL and WebSocket handshakes must be complete
    if (!_wss_stream || !_ws_handshake_complete) {
        errno = ENOTCONN;
        return 0;
    }

    if (!_wss_stream->next_layer ().next_layer ().is_open ()) {
        errno = EBADF;
        return 0;
    }

    //  WebSocket over SSL is frame-based protocol.
    //  We must write the complete frame atomically to maintain protocol integrity.
    //  Unlike TCP/TLS, partial writes are not meaningful for WebSocket.
    //
    //  Beast's write() sends a complete frame synchronously over the SSL layer.
    //  On would_block, we return 0 with errno = EAGAIN, and the caller
    //  should retry via async path.
    //
    //  Important: This blocks until the entire frame is sent or an error occurs.
    //  For speculative write optimization, this is acceptable because:
    //  1. Small messages (typical ZMQ use case) fit in socket buffer
    //  2. If buffer is full, we get would_block immediately
    //  3. Large messages should use async path anyway

    boost::system::error_code ec;
    std::size_t bytes_written = 0;

    //  Use write() to send complete frame (not write_some which is not
    //  available for WebSocket in Beast)
    bytes_written =
      _wss_stream->write (boost::asio::buffer (data, len), ec);

    if (ec) {
        //  Handle would_block case
        if (ec == boost::asio::error::would_block
            || ec == boost::asio::error::try_again) {
            errno = EAGAIN;
            return 0;
        }

        //  Handle SSL-specific errors
        if (ec.category () == boost::asio::error::get_ssl_category ()) {
            errno = EIO;
            ASIO_DBG_WSS ("write_some SSL error: %s", ec.message ().c_str ());
            return 0;
        }

        //  Map boost error codes to errno
        if (ec == boost::asio::error::broken_pipe
            || ec == boost::asio::error::connection_reset) {
            errno = EPIPE;
        } else if (ec == boost::asio::error::not_connected) {
            errno = ENOTCONN;
        } else if (ec == boost::asio::error::bad_descriptor) {
            errno = EBADF;
        } else if (ec == boost::asio::error::eof
                   || ec == boost::beast::websocket::error::closed) {
            errno = ECONNRESET;
        } else {
            errno = EIO;
        }

        ASIO_DBG_WSS ("write_some error: %s", ec.message ().c_str ());
        return 0;
    }

    ASIO_DBG_WSS ("write_some: wrote %zu bytes as frame", bytes_written);
    errno = 0;
    return bytes_written;
}

void wss_transport_t::async_handshake (int handshake_type,
                                       completion_handler_t handler)
{
    if (!_wss_stream) {
        if (handler) {
            handler (boost::asio::error::not_connected, 0);
        }
        return;
    }

    _handshake_type = handshake_type;

    ASIO_DBG_WSS ("starting SSL handshake, type=%s",
                  handshake_type == 0 ? "client" : "server");

    //  First do SSL handshake
    auto ssl_hs_type = (handshake_type == client)
                         ? boost::asio::ssl::stream_base::client
                         : boost::asio::ssl::stream_base::server;

    if (handshake_type == client && !_tls_hostname.empty ()) {
        if (!SSL_set_tlsext_host_name (_wss_stream->next_layer ().native_handle (),
                                       _tls_hostname.c_str ())) {
            if (handler) {
                handler (boost::asio::error::invalid_argument, 0);
            }
            return;
        }
    }

    _wss_stream->next_layer ().async_handshake (
      ssl_hs_type, [this, handler] (const boost::system::error_code &ec) {
          if (ec) {
              ASIO_DBG_WSS ("SSL handshake failed: %s", ec.message ().c_str ());
              if (handler) {
                  handler (ec, 0);
              }
              return;
          }

          _ssl_handshake_complete = true;
          ASIO_DBG_WSS ("SSL handshake complete, continuing with WebSocket");

          //  Now do WebSocket handshake
          continue_ws_handshake (handler);
      });
}

void wss_transport_t::continue_ws_handshake (completion_handler_t handler)
{
    if (_handshake_type == client) {
        //  Client-side WebSocket handshake
        _wss_stream->async_handshake (
          _host, _path,
          [this, handler] (const boost::system::error_code &ec) {
              if (!ec) {
                  _ws_handshake_complete = true;
                  ASIO_DBG_WSS ("WebSocket client handshake complete");
              } else {
                  ASIO_DBG_WSS ("WebSocket client handshake failed: %s",
                                ec.message ().c_str ());
              }
              if (handler) {
                  handler (ec, 0);
              }
          });
    } else {
        //  Server-side WebSocket handshake
        _wss_stream->async_accept (
          [this, handler] (const boost::system::error_code &ec) {
              if (!ec) {
                  _ws_handshake_complete = true;
                  ASIO_DBG_WSS ("WebSocket server handshake complete");
              } else {
                  ASIO_DBG_WSS ("WebSocket server handshake failed: %s",
                                ec.message ().c_str ());
              }
              if (handler) {
                  handler (ec, 0);
              }
          });
    }
}

}  // namespace zmq

#endif  // ZMQ_IOTHREAD_POLLER_USE_ASIO && ZMQ_HAVE_ASIO_WS && ZMQ_HAVE_ASIO_SSL
