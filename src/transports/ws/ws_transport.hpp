/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZMQ_WS_TRANSPORT_HPP_INCLUDED__
#define __ZMQ_WS_TRANSPORT_HPP_INCLUDED__

#include "core/poller.hpp"
#if defined ZMQ_IOTHREAD_POLLER_USE_ASIO && defined ZMQ_HAVE_ASIO_WS

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <array>
#include <memory>
#include <string>

#include "engine/asio/i_asio_transport.hpp"

namespace zmq
{

//  WebSocket transport implementation using Boost.Beast
//
//  This transport wraps a TCP socket with WebSocket framing.
//  It implements the i_asio_transport interface for use with asio_engine_t.
//
//  WebSocket specifics:
//  - Requires HTTP upgrade handshake before data transfer
//  - Uses binary frames for ZMQ messages (not text frames)
//  - Frame-based protocol (read returns complete frames, not streams)
//
//  Usage:
//    1. Create ws_transport_t with optional path/protocol
//    2. Call open() to wrap an existing TCP socket
//    3. Call async_handshake() to complete WebSocket handshake
//    4. Use async_read_some/async_write_some for WebSocket I/O

class ws_transport_t : public i_asio_transport
{
  public:
    //  Handshake types
    enum handshake_type
    {
        client = 0,
        server = 1
    };

    //  Create WebSocket transport
    //  path: URL path for WebSocket endpoint (e.g., "/zmq")
    //  host: Host name for client handshake (e.g., "127.0.0.1:9000")
    ws_transport_t (const std::string &path = "/",
                    const std::string &host = "localhost");
    ~ws_transport_t () ZMQ_OVERRIDE;

    //  i_asio_transport interface
    bool open (boost::asio::io_context &io_context, fd_t fd) ZMQ_OVERRIDE;
    bool is_open () const ZMQ_OVERRIDE;
    void close () ZMQ_OVERRIDE;

    void async_read_some (unsigned char *buffer,
                          std::size_t buffer_size,
                          completion_handler_t handler) ZMQ_OVERRIDE;

    std::size_t read_some (std::uint8_t *buffer,
                           std::size_t len) ZMQ_OVERRIDE;

    void async_write_some (const unsigned char *buffer,
                           std::size_t buffer_size,
                           completion_handler_t handler) ZMQ_OVERRIDE;

    std::size_t write_some (const std::uint8_t *data,
                            std::size_t len) ZMQ_OVERRIDE;

    //  WebSocket-specific overrides
    bool requires_handshake () const ZMQ_OVERRIDE { return true; }
    void async_handshake (int handshake_type,
                          completion_handler_t handler) ZMQ_OVERRIDE;
    bool supports_speculative_write () const ZMQ_OVERRIDE { return false; }
    bool supports_gather_write () const ZMQ_OVERRIDE { return true; }
    void async_writev (const unsigned char *header,
                       std::size_t header_size,
                       const unsigned char *body,
                       std::size_t body_size,
                       completion_handler_t handler) ZMQ_OVERRIDE;
    bool is_encrypted () const ZMQ_OVERRIDE { return false; }
    const char *name () const ZMQ_OVERRIDE { return "ws"; }

    //  Set the host for client handshake
    void set_host (const std::string &host) { _host = host; }

    //  Set the path for WebSocket endpoint
    void set_path (const std::string &path) { _path = path; }

  private:
    //  WebSocket stream type (over TCP socket, no compression for simplicity)
    typedef boost::beast::websocket::stream<boost::asio::ip::tcp::socket>
      ws_stream_t;

    //  Dynamic buffer for reading WebSocket frames
    typedef boost::beast::flat_buffer buffer_t;

    std::string _path;
    std::string _host;
    std::unique_ptr<ws_stream_t> _ws_stream;
    buffer_t _read_buffer;
    std::size_t _read_offset;
    bool _handshake_complete;

    ZMQ_NON_COPYABLE_NOR_MOVABLE (ws_transport_t)
};

}  // namespace zmq

#endif  // ZMQ_IOTHREAD_POLLER_USE_ASIO && ZMQ_HAVE_ASIO_WS

#endif  // __ZMQ_WS_TRANSPORT_HPP_INCLUDED__
