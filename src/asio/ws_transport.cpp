/* SPDX-License-Identifier: MPL-2.0 */

#include "../precompiled.hpp"
#include "ws_transport.hpp"

#if defined ZMQ_IOTHREAD_POLLER_USE_ASIO && defined ZMQ_HAVE_ASIO_WS

#include "asio_debug.hpp"

//  Debug logging for WebSocket transport
#define ASIO_DBG_WS(fmt, ...) ASIO_DBG_THIS ("WS", fmt, ##__VA_ARGS__)

namespace zmq
{

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
    socket.assign (boost::asio::ip::tcp::v4 (), fd, ec);
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

        //  Try graceful WebSocket close if handshake was complete
        if (_handshake_complete) {
            //  Note: We use sync close here as this is called during cleanup
            //  A production implementation might want async_close
            _ws_stream->close (boost::beast::websocket::close_code::normal, ec);
            //  Ignore close errors - connection may already be closed
        }

        //  Close the underlying socket
        _ws_stream->next_layer ().close (ec);

        _ws_stream.reset ();
    }

    _read_buffer.reset ();

    _handshake_complete = false;
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
