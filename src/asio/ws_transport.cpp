/* SPDX-License-Identifier: MPL-2.0 */

#include "../precompiled.hpp"
#include "ws_transport.hpp"

#if defined ZMQ_IOTHREAD_POLLER_USE_ASIO && defined ZMQ_HAVE_ASIO_WS

#include "asio_debug.hpp"
#include "../address.hpp"

//  Debug logging for WebSocket transport
#define ASIO_DBG_WS(fmt, ...) ASIO_DBG_THIS ("WS", fmt, ##__VA_ARGS__)

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

ws_transport_t::ws_transport_t (const std::string &path,
                                const std::string &host) :
    _path (path),
    _host (host),
    _handshake_complete (false),
    _frame_offset (0)
{
}

ws_transport_t::~ws_transport_t ()
{
    close ();
}

bool ws_transport_t::open (boost::asio::io_context &io_context, fd_t fd)
{
    //  Close any existing stream
    close ();

    //  Create the underlying TCP socket
    boost::asio::ip::tcp::socket socket (io_context);
    boost::system::error_code ec;

    //  Assign the file descriptor to the socket
    socket.assign (protocol_for_fd (fd), fd, ec);
    if (ec) {
        ASIO_GLOBAL_ERROR ("ws_transport assign failed: %s",
                          ec.message ().c_str ());
        return false;
    }

    //  Create WebSocket stream wrapping the socket
    try {
        _ws_stream =
          std::unique_ptr<ws_stream_t> (new ws_stream_t (std::move (socket)));
    } catch (const std::bad_alloc &) {
        ASIO_GLOBAL_ERROR ("ws_transport stream allocation failed");
        return false;
    }

    //  Configure WebSocket options
    //  Use binary mode for ZMQ messages
    _ws_stream->binary (true);

    //  Create read buffer for frame assembly
    try {
        _read_buffer = std::unique_ptr<buffer_t> (new buffer_t ());
    } catch (const std::bad_alloc &) {
        ASIO_GLOBAL_ERROR ("ws_transport buffer allocation failed");
        _ws_stream.reset ();
        return false;
    }

    _handshake_complete = false;
    _frame_buffer.clear ();
    _frame_offset = 0;

    ASIO_DBG_WS ("opened with path=%s, host=%s", _path.c_str (), _host.c_str ());
    return true;
}

bool ws_transport_t::is_open () const
{
    return _ws_stream && _ws_stream->next_layer ().is_open ();
}

void ws_transport_t::close ()
{
    if (_ws_stream) {
        boost::system::error_code ec;

        //  Close the underlying socket first - this cancels all pending async ops
        //  The socket close will cause pending async_read/async_write to complete
        //  with operation_aborted error
        if (_ws_stream->next_layer ().is_open ()) {
            _ws_stream->next_layer ().shutdown (
              boost::asio::ip::tcp::socket::shutdown_both, ec);
            _ws_stream->next_layer ().close (ec);
        }

        //  Note: We don't reset the stream here - let the caller drain pending
        //  handlers first. The stream will be cleaned up in destructor.
        _handshake_complete = false;
    }

    _frame_buffer.clear ();
    _frame_offset = 0;
}

void ws_transport_t::async_read_some (unsigned char *buffer,
                                      std::size_t buffer_size,
                                      completion_handler_t handler)
{
    if (!_ws_stream || !_handshake_complete) {
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

        ASIO_DBG_WS ("read_some: returned %zu bytes from frame buffer", to_copy);

        if (handler) {
            //  Post the handler to avoid stack overflow with synchronous completion
            boost::asio::post (
              _ws_stream->get_executor (),
              [handler, to_copy] () {
                  handler (boost::system::error_code (), to_copy);
              });
        }
        return;
    }

    //  Need to read a new frame from WebSocket
    _ws_stream->async_read (
      *_read_buffer,
      [this, buffer, buffer_size, handler] (
        const boost::system::error_code &ec, std::size_t bytes_transferred) {
          if (ec) {
              ASIO_DBG_WS ("read frame failed: %s", ec.message ().c_str ());
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

          ASIO_DBG_WS ("read frame: %zu bytes", frame_size);

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

void ws_transport_t::async_write_some (const unsigned char *buffer,
                                       std::size_t buffer_size,
                                       completion_handler_t handler)
{
    if (!_ws_stream || !_handshake_complete) {
        if (handler) {
            handler (boost::asio::error::not_connected, 0);
        }
        return;
    }

    //  WebSocket writes are frame-based, so we write the entire buffer
    //  as a single binary frame
    _ws_stream->async_write (
      boost::asio::buffer (buffer, buffer_size),
      [handler] (const boost::system::error_code &ec,
                 std::size_t bytes_transferred) {
          ASIO_DBG ("WS", "write complete: ec=%s, bytes=%zu",
                    ec.message ().c_str (), bytes_transferred);
          if (handler) {
              handler (ec, bytes_transferred);
          }
      });
}

std::size_t ws_transport_t::write_some (const std::uint8_t *data,
                                        std::size_t len)
{
    if (len == 0) {
        return 0;
    }

    //  Check transport state
    if (!_ws_stream || !_handshake_complete) {
        errno = ENOTCONN;
        return 0;
    }

    if (!_ws_stream->next_layer ().is_open ()) {
        errno = EBADF;
        return 0;
    }

    //  WebSocket is frame-based protocol.
    //  We must write the complete frame atomically to maintain protocol integrity.
    //  Unlike TCP/TLS, partial writes are not meaningful for WebSocket.
    //
    //  Beast's write() sends a complete frame synchronously.
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
      _ws_stream->write (boost::asio::buffer (data, len), ec);

    if (ec) {
        //  Handle would_block case
        if (ec == boost::asio::error::would_block
            || ec == boost::asio::error::try_again) {
            errno = EAGAIN;
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

        ASIO_DBG_WS ("write_some error: %s", ec.message ().c_str ());
        return 0;
    }

    ASIO_DBG_WS ("write_some: wrote %zu bytes as frame", bytes_written);
    errno = 0;
    return bytes_written;
}

void ws_transport_t::async_handshake (int handshake_type,
                                      completion_handler_t handler)
{
    if (!_ws_stream) {
        if (handler) {
            handler (boost::asio::error::not_connected, 0);
        }
        return;
    }

    ASIO_DBG_WS ("starting handshake, type=%s",
                 handshake_type == 0 ? "client" : "server");

    if (handshake_type == client) {
        //  Client-side WebSocket handshake
        _ws_stream->async_handshake (
          _host, _path,
          [this, handler] (const boost::system::error_code &ec) {
              if (!ec) {
                  _handshake_complete = true;
                  ASIO_DBG_WS ("client handshake complete");
              } else {
                  ASIO_DBG_WS ("client handshake failed: %s",
                               ec.message ().c_str ());
              }
              if (handler) {
                  handler (ec, 0);
              }
          });
    } else {
        //  Server-side WebSocket handshake
        _ws_stream->async_accept (
          [this, handler] (const boost::system::error_code &ec) {
              if (!ec) {
                  _handshake_complete = true;
                  ASIO_DBG_WS ("server handshake complete");
              } else {
                  ASIO_DBG_WS ("server handshake failed: %s",
                               ec.message ().c_str ());
              }
              if (handler) {
                  handler (ec, 0);
              }
          });
    }
}

}  // namespace zmq

#endif  // ZMQ_IOTHREAD_POLLER_USE_ASIO && ZMQ_HAVE_ASIO_WS
