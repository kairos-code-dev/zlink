/* SPDX-License-Identifier: MPL-2.0 */

#include "../precompiled.hpp"
#include "wss_transport.hpp"

#if defined ZMQ_IOTHREAD_POLLER_USE_ASIO && defined ZMQ_HAVE_ASIO_WS \
  && defined ZMQ_HAVE_ASIO_SSL

#include "asio_debug.hpp"

//  Debug logging for WSS transport
#define ASIO_DBG_WSS(fmt, ...) ASIO_DBG_THIS ("WSS", fmt, ##__VA_ARGS__)

namespace zmq
{

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
    socket.assign (boost::asio::ip::tcp::v4 (), fd, ec);
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

        //  Try graceful WebSocket close if handshake was complete
        if (_ws_handshake_complete) {
            _wss_stream->close (boost::beast::websocket::close_code::normal, ec);
            //  Ignore close errors - connection may already be closed
        }

        //  Try graceful SSL shutdown
        if (_ssl_handshake_complete) {
            _wss_stream->next_layer ().shutdown (ec);
            //  Ignore shutdown errors
        }

        //  Close the underlying socket
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
