#ifndef PERF_STREAM_CLIENT_STREAM_CLIENT_HPP
#define PERF_STREAM_CLIENT_STREAM_CLIENT_HPP

// Header-only synchronous transport client with len32be framing.
// Supports four transport modes: tcp, tls, ws, wss.
//
// Wire format on all transports:
//   send_payload() → [4-byte BE length][payload bytes]
//   recv_payload() ← [4-byte BE length][payload bytes]
//
// For ws/wss, len32be frames are packed inside WebSocket binary messages.
// Multiple len32be frames may arrive in a single WS message, so the
// receiver buffers partial data in ws_pending_frame and reassembles.
//
// Uses Boost.Asio for TCP/TLS and Boost.Beast for WebSocket.

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>

#include "perf_stream_common.hpp"
#include "perf_stream_frame_reassembly.hpp"

#include <cstddef>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

// Synchronous len32be transport client.
// One active socket at a time, selected by the transport mode.
class stream_client_t
{
  public:
    stream_client_t (const std::string &transport,
                     const std::string &host,
                     int port)
        : mode (parse_transport_mode (transport)),
          host_ (host),
          port_ (port),
          io (),
          tls_ctx (boost::asio::ssl::context::tls_client),
          tcp_socket (),
          tls_socket (),
          ws_socket (),
          wss_socket (),
          ws_pending_frame ()
    {
        tls_ctx.set_verify_mode (boost::asio::ssl::verify_none);
    }

    ~stream_client_t ()
    {
        close ();
    }

    stream_client_t (const stream_client_t &) = delete;
    stream_client_t &operator= (const stream_client_t &) = delete;

    // Resolve host:port and establish a connection (+ TLS/WS handshake if needed).
    bool connect ()
    {
        using boost::asio::ip::tcp;

        boost::system::error_code ec;
        tcp::resolver resolver (io);
        const tcp::resolver::results_type endpoints =
          resolver.resolve (host_, std::to_string (port_), ec);
        if (ec)
            return false;

        perf_stream_frame::reset (&ws_pending_frame);

        if (mode == raw_transport_tcp) {
            tcp_socket.reset (new tcp::socket (io));
            boost::asio::connect (*tcp_socket, endpoints, ec);
            return !ec;
        }

        if (mode == raw_transport_tls) {
            tls_socket.reset (
              new boost::asio::ssl::stream<tcp::socket> (io, tls_ctx));
            boost::asio::connect (tls_socket->next_layer (), endpoints, ec);
            if (ec)
                return false;
            tls_socket->handshake (boost::asio::ssl::stream_base::client, ec);
            return !ec;
        }

        if (mode == raw_transport_ws) {
            ws_socket.reset (
              new boost::beast::websocket::stream<tcp::socket> (io));
            boost::asio::connect (ws_socket->next_layer (), endpoints, ec);
            if (ec)
                return false;
            ws_socket->binary (true);
            ws_socket->set_option (
              boost::beast::websocket::stream_base::timeout::suggested (
                boost::beast::role_type::client));
            ws_socket->handshake (host_ + ":" + std::to_string (port_), "/", ec);
            return !ec;
        }

        wss_socket.reset (new boost::beast::websocket::stream<
                          boost::asio::ssl::stream<tcp::socket> > (io,
                                                                     tls_ctx));
        boost::asio::connect (wss_socket->next_layer ().next_layer (), endpoints,
                              ec);
        if (ec)
            return false;
        wss_socket->next_layer ().handshake (
          boost::asio::ssl::stream_base::client, ec);
        if (ec)
            return false;
        wss_socket->binary (true);
        wss_socket->set_option (
          boost::beast::websocket::stream_base::timeout::suggested (
            boost::beast::role_type::client));
        wss_socket->handshake (host_ + ":" + std::to_string (port_), "/", ec);
        return !ec;
    }

    // Send a len32be-framed payload: [4B BE length][payload].
    bool send_payload (const std::vector<unsigned char> &payload)
    {
        std::vector<unsigned char> frame (4 + payload.size ());
        perf_stream_common::perf_stream_store_u32_be (
          &frame[0], static_cast<uint32_t> (payload.size ()));
        if (!payload.empty ())
            std::memcpy (&frame[4], &payload[0], payload.size ());
        return write_frame_bytes (&frame[0], frame.size ());
    }

    // Receive a len32be-framed payload. Validates declared size against max
    // and optional expected_size.
    bool recv_payload (std::vector<unsigned char> *payload_out,
                       size_t expected_size = 0)
    {
        if (!payload_out)
            return false;
        payload_out->clear ();

        std::vector<unsigned char> frame;
        if (!read_frame_bytes (&frame))
            return false;
        if (frame.size () < 4)
            return false;

        const uint32_t declared = perf_stream_common::perf_stream_load_u32_be (
          &frame[0]);
        if (declared
            > static_cast<uint32_t> (perf_stream_common::k_stream_max_chunk_size)) {
            return false;
        }
        if (frame.size () != static_cast<size_t> (4 + declared))
            return false;
        if (expected_size > 0 && declared != expected_size)
            return false;

        payload_out->assign (frame.begin () + 4, frame.end ());
        return true;
    }

    // Graceful shutdown: close WS/TLS/TCP layers and clear pending buffers.
    void close ()
    {
        using boost::asio::ip::tcp;

        boost::system::error_code ec;

        if (ws_socket) {
            ws_socket->close (boost::beast::websocket::close_code::normal, ec);
            ws_socket.reset ();
        }
        if (wss_socket) {
            wss_socket->close (boost::beast::websocket::close_code::normal, ec);
            wss_socket.reset ();
        }
        if (tls_socket) {
            tls_socket->shutdown (ec);
            tls_socket->next_layer ().close (ec);
            tls_socket.reset ();
        }
        if (tcp_socket) {
            tcp_socket->shutdown (tcp::socket::shutdown_both, ec);
            tcp_socket->close (ec);
            tcp_socket.reset ();
        }

        perf_stream_frame::reset (&ws_pending_frame);
    }

  private:
    enum raw_transport_mode_t
    {
        raw_transport_tcp = 0,
        raw_transport_tls = 1,
        raw_transport_ws = 2,
        raw_transport_wss = 3,
    };

    static raw_transport_mode_t parse_transport_mode (
      const std::string &transport)
    {
        const std::string t = perf_stream_common::lower_copy (transport);
        if (t == "tls")
            return raw_transport_tls;
        if (t == "ws")
            return raw_transport_ws;
        if (t == "wss")
            return raw_transport_wss;
        return raw_transport_tcp;
    }

    // --- Low-level I/O ---

    // Write raw bytes to the active socket.
    bool write_frame_bytes (const unsigned char *data, size_t size)
    {
        boost::system::error_code ec;
        if (mode == raw_transport_tcp && tcp_socket) {
            const size_t n =
              boost::asio::write (*tcp_socket, boost::asio::buffer (data, size), ec);
            return !ec && n == size;
        }
        if (mode == raw_transport_tls && tls_socket) {
            const size_t n =
              boost::asio::write (*tls_socket, boost::asio::buffer (data, size), ec);
            return !ec && n == size;
        }
        if (mode == raw_transport_ws && ws_socket) {
            const size_t n = ws_socket->write (boost::asio::buffer (data, size), ec);
            return !ec && n == size;
        }
        if (mode == raw_transport_wss && wss_socket) {
            const size_t n =
              wss_socket->write (boost::asio::buffer (data, size), ec);
            return !ec && n == size;
        }
        return false;
    }

    // Read exactly `size` bytes from TCP or TLS socket (blocking).
    bool read_exact_tcp_like (unsigned char *data, size_t size)
    {
        boost::system::error_code ec;
        if (mode == raw_transport_tcp && tcp_socket) {
            const size_t n =
              boost::asio::read (*tcp_socket, boost::asio::buffer (data, size), ec);
            return !ec && n == size;
        }
        if (mode == raw_transport_tls && tls_socket) {
            const size_t n =
              boost::asio::read (*tls_socket, boost::asio::buffer (data, size), ec);
            return !ec && n == size;
        }
        return false;
    }

    // Read one complete WebSocket binary message.
    bool read_ws_message_bytes (std::vector<unsigned char> *message_out)
    {
        if (!message_out)
            return false;
        message_out->clear ();

        boost::system::error_code ec;
        boost::beast::flat_buffer recv_buf;
        if (mode == raw_transport_ws && ws_socket) {
            ws_socket->read (recv_buf, ec);
        } else if (mode == raw_transport_wss && wss_socket) {
            wss_socket->read (recv_buf, ec);
        } else {
            return false;
        }
        if (ec)
            return false;

        const size_t n = recv_buf.size ();
        message_out->resize (n);
        if (n > 0) {
            boost::asio::buffer_copy (
              boost::asio::buffer (&(*message_out)[0], n), recv_buf.cdata ());
        }
        return true;
    }

    // Read one complete len32be frame [4B header + payload].
    // For TCP/TLS: reads header then payload directly.
    // For WS/WSS: reassembles from ws_pending_frame buffer, fetching
    //             new WS messages as needed.
    bool read_frame_bytes (std::vector<unsigned char> *frame_out)
    {
        if (!frame_out)
            return false;
        frame_out->clear ();

        if (mode == raw_transport_tcp || mode == raw_transport_tls) {
            unsigned char hdr[4];
            if (!read_exact_tcp_like (hdr, sizeof (hdr)))
                return false;
            const uint32_t declared =
              perf_stream_common::perf_stream_load_u32_be (hdr);
            if (declared
                > static_cast<uint32_t> (perf_stream_common::k_stream_max_chunk_size)) {
                return false;
            }

            frame_out->resize (4 + declared);
            std::memcpy (&(*frame_out)[0], hdr, sizeof (hdr));
            if (declared > 0
                && !read_exact_tcp_like (&(*frame_out)[4],
                                         static_cast<size_t> (declared))) {
                return false;
            }
            return true;
        }

        if (mode != raw_transport_ws && mode != raw_transport_wss)
            return false;

        while (true) {
            if (perf_stream_frame::has_invalid_declared_size (&ws_pending_frame))
                return false;

            perf_stream_frame::frame_view_t frame;
            if (perf_stream_frame::try_peek (&ws_pending_frame, &frame)) {
                frame_out->assign (frame.data, frame.data + frame.size);
                perf_stream_frame::consume (&ws_pending_frame, frame);
                perf_stream_frame::compact (&ws_pending_frame);
                return true;
            }

            std::vector<unsigned char> message;
            if (!read_ws_message_bytes (&message))
                return false;
            perf_stream_frame::append (
              &ws_pending_frame,
              message.empty () ? NULL : &message[0],
              message.size ());
        }
    }

    // --- Member state ---

    raw_transport_mode_t mode;
    std::string host_;
    int port_;
    boost::asio::io_context io;
    boost::asio::ssl::context tls_ctx;

    // One socket active per mode (the rest are nullptr).
    std::unique_ptr<boost::asio::ip::tcp::socket> tcp_socket;
    std::unique_ptr<boost::asio::ssl::stream<boost::asio::ip::tcp::socket> >
      tls_socket;
    std::unique_ptr<boost::beast::websocket::stream<boost::asio::ip::tcp::socket> >
      ws_socket;
    std::unique_ptr<boost::beast::websocket::stream<
      boost::asio::ssl::stream<boost::asio::ip::tcp::socket> > > wss_socket;

    // Reassembly buffer for WS/WSS: accumulates bytes across WS messages
    // until a complete len32be frame can be extracted.
    perf_stream_frame::buffer_t ws_pending_frame;
};

#endif
