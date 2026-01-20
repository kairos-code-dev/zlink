/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"
#if defined ZMQ_IOTHREAD_POLLER_USE_ASIO && defined ZMQ_HAVE_WS

#include "asio_ws_engine.hpp"
#include "asio_poller.hpp"
#include "../io_thread.hpp"
#include "../session_base.hpp"
#include "../socket_base.hpp"
#include "../null_mechanism.hpp"
#include "../plain_client.hpp"
#include "../plain_server.hpp"
#include "../config.hpp"
#include "../err.hpp"
#include "../ip.hpp"
#include "../tcp.hpp"
#include "../likely.hpp"
#include "../v1_decoder.hpp"
#include "../v1_encoder.hpp"
#include "../v2_decoder.hpp"
#include "../v2_encoder.hpp"
#include "../zmp_decoder.hpp"
#include "../zmp_encoder.hpp"
#include "../zmp_protocol.hpp"
#include "../wire.hpp"

#ifndef ZMQ_HAVE_WINDOWS
#include <unistd.h>
#endif

#if defined ZMQ_HAVE_ASIO_SSL
#include <boost/asio/ssl.hpp>
#endif

#include <sstream>
#include <cstring>

namespace
{
const size_t zmp_hello_min_body = 3;
const size_t zmp_hello_max_body = 3 + 255;
}

//  Debug logging for ASIO WS engine - set to 1 to enable
#define ASIO_WS_ENGINE_DEBUG 0

#if ASIO_WS_ENGINE_DEBUG
#include <cstdio>
#define WS_ENGINE_DBG(fmt, ...)                                                \
    fprintf (stderr, "[ASIO_WS_ENGINE] " fmt "\n", ##__VA_ARGS__)
#else
#define WS_ENGINE_DBG(fmt, ...)
#endif

//  Message property names
#define ZMQ_MSG_PROPERTY_ROUTING_ID "Routing-Id"
#define ZMQ_MSG_PROPERTY_SOCKET_TYPE "Socket-Type"
#define ZMQ_MSG_PROPERTY_USER_ID "User-Id"
#define ZMQ_MSG_PROPERTY_PEER_ADDRESS "Peer-Address"

static std::string get_peer_address (zmq::fd_t s_)
{
    std::string peer_address;
    const int family = zmq::get_peer_ip_address (s_, peer_address);
    if (family == 0)
        peer_address.clear ();
    return peer_address;
}

zmq::asio_ws_engine_t::asio_ws_engine_t (
  fd_t fd_,
  const options_t &options_,
  const endpoint_uri_pair_t &endpoint_uri_pair_,
  bool is_client_,
  std::unique_ptr<i_asio_transport> transport_) :
    _transport (std::move (transport_)),
    _is_client (is_client_),
    _ws_handshake_complete (false),
    _session (NULL),
    _socket (NULL),
    _fd (fd_),
    _io_context (NULL),
    _options (options_),
    _endpoint_uri_pair (endpoint_uri_pair_),
    _peer_address (get_peer_address (fd_)),
    _inpos (NULL),
    _insize (0),
    _decoder (NULL),
    _input_in_decoder_buffer (false),
    _outpos (NULL),
    _outsize (0),
    _encoder (NULL),
    _mechanism (NULL),
    _next_msg (NULL),
    _process_msg (NULL),
    _metadata (NULL),
    _plugged (false),
    _handshaking (true),
    _input_stopped (false),
    _output_stopped (false),
    _io_error (false),
    _read_pending (false),
    _write_pending (false),
    _terminating (false),
    _read_buffer (read_buffer_size),
    _read_buffer_ptr (NULL),
    _greeting_size (v3_greeting_size),
    _greeting_bytes_read (0),
    _zmp_mode (zmp_protocol_enabled ()),
    _hello_sent (false),
    _hello_received (false),
    _hello_header_bytes (0),
    _hello_body_bytes (0),
    _hello_body_len (0),
    _hello_send_size (0),
    _peer_routing_id_size (0),
    _subscription_required (false),
    _heartbeat_timeout (0),
    _current_timer_id (-1),
    _has_handshake_timer (false),
    _has_ttl_timer (false),
    _has_timeout_timer (false),
    _has_heartbeat_timer (false)
{
    WS_ENGINE_DBG ("Constructor: fd=%d, client=%d", fd_, is_client_);

    int rc = _tx_msg.init ();
    errno_assert (rc == 0);

    rc = _routing_id_msg.init ();
    errno_assert (rc == 0);

    rc = _pong_msg.init ();
    errno_assert (rc == 0);

    //  Initialize greeting buffers
    memset (_greeting_recv, 0, sizeof (_greeting_recv));
    memset (_greeting_send, 0, sizeof (_greeting_send));
    memset (_hello_recv, 0, sizeof (_hello_recv));
    memset (_hello_send, 0, sizeof (_hello_send));
    memset (_peer_routing_id, 0, sizeof (_peer_routing_id));

    zmq_assert (_transport);

    //  Put socket into non-blocking mode
    unblock_socket (_fd);
}

#if defined ZMQ_HAVE_ASIO_SSL
zmq::asio_ws_engine_t::asio_ws_engine_t (
  fd_t fd_,
  const options_t &options_,
  const endpoint_uri_pair_t &endpoint_uri_pair_,
  bool is_client_,
  std::unique_ptr<i_asio_transport> transport_,
  std::unique_ptr<boost::asio::ssl::context> ssl_context_) :
    _transport (std::move (transport_)),
    _is_client (is_client_),
    _ws_handshake_complete (false),
    _session (NULL),
    _socket (NULL),
    _fd (fd_),
    _io_context (NULL),
    _options (options_),
    _endpoint_uri_pair (endpoint_uri_pair_),
    _peer_address (get_peer_address (fd_)),
    _inpos (NULL),
    _insize (0),
    _decoder (NULL),
    _input_in_decoder_buffer (false),
    _outpos (NULL),
    _outsize (0),
    _encoder (NULL),
    _mechanism (NULL),
    _next_msg (NULL),
    _process_msg (NULL),
    _metadata (NULL),
    _plugged (false),
    _handshaking (true),
    _input_stopped (false),
    _output_stopped (false),
    _io_error (false),
    _read_pending (false),
    _write_pending (false),
    _terminating (false),
    _read_buffer (read_buffer_size),
    _read_buffer_ptr (NULL),
    _greeting_size (v3_greeting_size),
    _greeting_bytes_read (0),
    _zmp_mode (zmp_protocol_enabled ()),
    _hello_sent (false),
    _hello_received (false),
    _hello_header_bytes (0),
    _hello_body_bytes (0),
    _hello_body_len (0),
    _hello_send_size (0),
    _peer_routing_id_size (0),
    _subscription_required (false),
    _heartbeat_timeout (0),
    _current_timer_id (-1),
    _has_handshake_timer (false),
    _has_ttl_timer (false),
    _has_timeout_timer (false),
    _has_heartbeat_timer (false),
    _ssl_context (std::move (ssl_context_))
{
    WS_ENGINE_DBG ("Constructor: fd=%d, client=%d (custom transport)", fd_,
                   is_client_);

    int rc = _tx_msg.init ();
    errno_assert (rc == 0);

    rc = _routing_id_msg.init ();
    errno_assert (rc == 0);

    rc = _pong_msg.init ();
    errno_assert (rc == 0);

    //  Initialize greeting buffers
    memset (_greeting_recv, 0, sizeof (_greeting_recv));
    memset (_greeting_send, 0, sizeof (_greeting_send));
    memset (_hello_recv, 0, sizeof (_hello_recv));
    memset (_hello_send, 0, sizeof (_hello_send));
    memset (_peer_routing_id, 0, sizeof (_peer_routing_id));

    zmq_assert (_transport);

    //  Put socket into non-blocking mode
    unblock_socket (_fd);
}
#endif

zmq::asio_ws_engine_t::~asio_ws_engine_t ()
{
    WS_ENGINE_DBG ("Destructor called");

    zmq_assert (!_plugged);

    //  Close transport (handles graceful close)
    if (_transport) {
        _transport->close ();
        _transport.reset ();
    }

    //  Close the underlying socket if still open
    if (_fd != retired_fd) {
#ifdef ZMQ_HAVE_WINDOWS
        int rc = closesocket (_fd);
        wsa_assert (rc != SOCKET_ERROR);
#else
        int rc = close (_fd);
#if defined(__FreeBSD_kernel__) || defined(__FreeBSD__)
        if (rc == -1 && errno == ECONNRESET)
            rc = 0;
#endif
        errno_assert (rc == 0);
#endif
        _fd = retired_fd;
    }

    int rc = _tx_msg.close ();
    errno_assert (rc == 0);

    rc = _routing_id_msg.close ();
    errno_assert (rc == 0);

    rc = _pong_msg.close ();
    errno_assert (rc == 0);

    if (_metadata != NULL) {
        if (_metadata->drop_ref ()) {
            LIBZMQ_DELETE (_metadata);
        }
    }

    LIBZMQ_DELETE (_encoder);
    LIBZMQ_DELETE (_decoder);
    LIBZMQ_DELETE (_mechanism);
}

void zmq::asio_ws_engine_t::plug (io_thread_t *io_thread_,
                                   session_base_t *session_)
{
    WS_ENGINE_DBG ("plug called");

    zmq_assert (!_plugged);
    _plugged = true;

    _session = session_;
    _socket = _session->get_socket ();

    //  Get reference to io_context
    asio_poller_t *poller =
      static_cast<asio_poller_t *> (io_thread_->get_poller ());
    _io_context = &poller->get_io_context ();

    //  Create timer
    _timer.reset (new boost::asio::steady_timer (*_io_context));

    //  Initialize WebSocket transport with the socket
    if (!_transport->open (*_io_context, _fd)) {
        WS_ENGINE_DBG ("Failed to open WebSocket transport");
        error (connection_error);
        return;
    }

    //  Mark FD as taken by transport (don't close it in destructor)
    _fd = retired_fd;

    _io_error = false;

    //  Start WebSocket handshake
    start_ws_handshake ();
}

void zmq::asio_ws_engine_t::start_ws_handshake ()
{
    WS_ENGINE_DBG ("Starting WebSocket handshake, is_client=%d", _is_client);

    //  Set handshake timer if configured
    if (_options.handshake_ivl > 0) {
        set_handshake_timer ();
    }

    //  Start WebSocket handshake (client or server)
    const int handshake_type = _is_client ? 0 : 1;

    _transport->async_handshake (
      handshake_type,
      [this] (const boost::system::error_code &ec, std::size_t) {
          on_ws_handshake_complete (ec);
      });
}

void zmq::asio_ws_engine_t::on_ws_handshake_complete (
  const boost::system::error_code &ec)
{
    WS_ENGINE_DBG ("WebSocket handshake complete, ec=%s", ec.message ().c_str ());

    if (_terminating)
        return;

    if (ec) {
        WS_ENGINE_DBG ("WebSocket handshake failed");
        error (connection_error);
        return;
    }

    _ws_handshake_complete = true;

    //  Cancel handshake timer if running (we'll restart for ZMTP handshake)
    if (_has_handshake_timer) {
        cancel_handshake_timer ();
    }

    //  Start ZMTP handshake over WebSocket
    start_zmtp_handshake ();
}

void zmq::asio_ws_engine_t::start_zmtp_handshake ()
{
    if (_zmp_mode) {
        start_zmp_handshake ();
        return;
    }

    WS_ENGINE_DBG ("Starting ZMTP handshake over WebSocket");

    //  Set handshake timer for ZMTP phase
    if (_options.handshake_ivl > 0) {
        set_handshake_timer ();
    }

    //  Prepare ZMTP 3.x greeting
    memset (_greeting_send, 0, v3_greeting_size);
    _greeting_send[0] = 0xff;
    put_uint64 (_greeting_send + 1, 0x7fffffffffffffffULL);
    _greeting_send[9] = 0x7f;
    _greeting_send[10] = 3;  // Major version
    _greeting_send[11] = 1;  // Minor version

    //  Set the security mechanism
    const char *mechanism = "NULL";
    memcpy (_greeting_send + 12, mechanism, strlen (mechanism));

    //  as-server field
    _greeting_send[32] = _options.as_server ? 1 : 0;

    //  Send greeting
    _outpos = _greeting_send;
    _outsize = v3_greeting_size;

    //  Start reading greeting from peer
    _inpos = _greeting_recv;
    _insize = 0;
    _greeting_bytes_read = 0;

    //  Start I/O
    start_async_write ();
    start_async_read ();
}

void zmq::asio_ws_engine_t::start_zmp_handshake ()
{
    WS_ENGINE_DBG ("Starting ZMP handshake over WebSocket");

    if (_options.handshake_ivl > 0)
        set_handshake_timer ();

    const size_t identity_len =
      std::min (static_cast<size_t> (_options.routing_id_size),
                static_cast<size_t> (255));
    const size_t body_len = zmp_hello_min_body + identity_len;
    _hello_send[0] = zmp_magic;
    _hello_send[1] = zmp_version;
    _hello_send[2] = zmp_flag_control;
    _hello_send[3] = 0;
    put_uint32 (_hello_send + 4, static_cast<uint32_t> (body_len));
    _hello_send[zmp_header_size + 0] = zmp_control_hello;
    _hello_send[zmp_header_size + 1] =
      static_cast<unsigned char> (_options.type);
    _hello_send[zmp_header_size + 2] =
      static_cast<unsigned char> (identity_len);
    if (identity_len > 0)
        memcpy (_hello_send + zmp_header_size + zmp_hello_min_body,
                _options.routing_id, identity_len);

    _hello_send_size = zmp_header_size + body_len;
    _outpos = _hello_send;
    _outsize = _hello_send_size;
    _hello_sent = true;

    _hello_header_bytes = 0;
    _hello_body_bytes = 0;
    _hello_body_len = 0;

    start_async_write ();
    start_async_read ();
}

void zmq::asio_ws_engine_t::start_async_read ()
{
    if (_read_pending || _input_stopped || _io_error || !_ws_handshake_complete)
        return;

    WS_ENGINE_DBG ("start_async_read");

    _read_pending = true;

    //  Determine read target buffer
    unsigned char *buffer;
    size_t buffer_size;

    if (_handshaking) {
        if (_zmp_mode) {
            if (_hello_header_bytes < zmp_header_size)
                buffer_size = zmp_header_size - _hello_header_bytes;
            else
                buffer_size = _hello_body_len - _hello_body_bytes;
            buffer_size = std::min (buffer_size, _read_buffer.size ());
            buffer = _read_buffer.data ();
        } else {
            //  During handshake, read into greeting buffer
            buffer = _greeting_recv + _greeting_bytes_read;
            buffer_size = _greeting_size - _greeting_bytes_read;
        }
    } else if (_decoder) {
        //  Normal operation: read into decoder buffer
        _decoder->get_buffer (&_read_buffer_ptr, &buffer_size);
        buffer = _read_buffer_ptr;
    } else {
        //  Use internal buffer
        buffer = _read_buffer.data ();
        buffer_size = _read_buffer.size ();
    }

    _transport->async_read_some (
      buffer, buffer_size,
      [this] (const boost::system::error_code &ec,
              std::size_t bytes_transferred) {
          on_read_complete (ec, bytes_transferred);
      });
}

void zmq::asio_ws_engine_t::on_read_complete (
  const boost::system::error_code &ec, std::size_t bytes_transferred)
{
    _read_pending = false;

    WS_ENGINE_DBG ("on_read_complete: ec=%s, bytes=%zu", ec.message ().c_str (),
                   bytes_transferred);

    if (_terminating)
        return;

    if (ec) {
        if (ec == boost::asio::error::operation_aborted)
            return;
        WS_ENGINE_DBG ("Read error");
        error (connection_error);
        return;
    }

    if (bytes_transferred == 0) {
        WS_ENGINE_DBG ("Connection closed by peer");
        error (connection_error);
        return;
    }

    if (_handshaking) {
        if (_zmp_mode) {
            _inpos = _read_buffer.data ();
            _insize = bytes_transferred;
            _input_in_decoder_buffer = false;
            if (!handshake ()) {
                //  Handshake not complete, continue reading
                start_async_read ();
                return;
            }
        } else {
            _greeting_bytes_read += bytes_transferred;
            if (!handshake ()) {
                //  Handshake not complete, continue reading
                start_async_read ();
                return;
            }
        }
        //  Handshake complete, proceed to normal operation
        _handshaking = false;

        //  Cancel handshake timer
        if (_has_handshake_timer) {
            cancel_handshake_timer ();
        }

        if (_zmp_mode) {
            if (_session)
                _session->engine_ready ();

            _next_msg = &asio_ws_engine_t::pull_msg_from_session;
            _process_msg = &asio_ws_engine_t::decode_and_push;

            _encoder = new (std::nothrow) zmp_encoder_t (_options.out_batch_size);
            alloc_assert (_encoder);
            _decoder = new (std::nothrow)
              zmp_decoder_t (_options.in_batch_size, _options.maxmsgsize);
            alloc_assert (_decoder);

            if (_options.recv_routing_id) {
                msg_t routing_id;
                const int rc = routing_id.init_size (_peer_routing_id_size);
                errno_assert (rc == 0);
                if (_peer_routing_id_size > 0)
                    memcpy (routing_id.data (), _peer_routing_id,
                            _peer_routing_id_size);
                routing_id.set_flags (msg_t::routing_id);
                const int push_rc = _session->push_msg (&routing_id);
                errno_assert (push_rc == 0);
                _session->flush ();
            }
        } else {
            //  Check if subscription message is required for PUB/XPUB sockets
            if (_options.type == ZMQ_PUB || _options.type == ZMQ_XPUB)
                _subscription_required = true;

            //  Set up message processing for routing_id exchange
            //  Just like asio_zmtp_engine_t, we start with routing_id exchange
            _next_msg = &asio_ws_engine_t::routing_id_msg;
            _process_msg = &asio_ws_engine_t::process_routing_id_msg;

            //  Create encoder/decoder for v3.1
            _encoder = new (std::nothrow) v2_encoder_t (_options.out_batch_size);
            alloc_assert (_encoder);
            _decoder = new (std::nothrow)
              v2_decoder_t (_options.in_batch_size, _options.maxmsgsize,
                            _options.zero_copy);
            alloc_assert (_decoder);

            //  Create NULL mechanism (no authentication for now)
            _mechanism = new (std::nothrow)
              null_mechanism_t (_session, _peer_address, _options);
            alloc_assert (_mechanism);
        }

        mechanism_ready ();
    } else {
        //  Normal operation: process received data
        //  Data was read directly into decoder buffer via get_buffer()
        _inpos = _read_buffer_ptr;
        _insize = bytes_transferred;
        _input_in_decoder_buffer = true;

        WS_ENGINE_DBG ("on_read_complete: calling process_input, inpos=%p, insize=%zu",
                       static_cast<void *> (_inpos), _insize);

        if (!process_input ()) {
            return;
        }
    }

    //  Continue reading
    start_async_read ();
}

void zmq::asio_ws_engine_t::start_async_write ()
{
    if (_write_pending || _output_stopped || _io_error || !_ws_handshake_complete)
        return;

    WS_ENGINE_DBG ("start_async_write: outsize=%zu", _outsize);

    //  If there is already data prepared in _outpos/_outsize (from speculative_write
    //  fallback or partial write), copy it to _write_buffer for async operation.
    //  This is necessary because encoder buffer may be invalidated before async
    //  write completion.
    if (_outsize > 0 && _outpos != NULL) {
        //  Copy encoder buffer to write buffer for async lifetime safety
        const size_t out_batch_size =
          static_cast<size_t> (_options.out_batch_size);
        const size_t target =
          _outsize > out_batch_size ? _outsize : out_batch_size;
        if (_write_buffer.capacity () < target)
            _write_buffer.reserve (target);
        _write_buffer.assign (_outpos, _outpos + _outsize);

        WS_ENGINE_DBG ("start_async_write: copied %zu bytes for async", _outsize);

        //  Clear _outpos/_outsize as data is now in _write_buffer
        _outpos = NULL;
        _outsize = 0;
    } else {
        //  No data prepared, try to get from encoder via process_output
        process_output ();

        if (_write_buffer.empty ()) {
            WS_ENGINE_DBG ("No data to write");
            _output_stopped = true;
            return;
        }
    }

    WS_ENGINE_DBG ("start_async_write: sending %zu bytes", _write_buffer.size ());

    _write_pending = true;

    _transport->async_write_some (
      _write_buffer.data (), _write_buffer.size (),
      [this] (const boost::system::error_code &ec,
              std::size_t bytes_transferred) {
          on_write_complete (ec, bytes_transferred);
      });
}

void zmq::asio_ws_engine_t::on_write_complete (
  const boost::system::error_code &ec, std::size_t bytes_transferred)
{
    _write_pending = false;

    WS_ENGINE_DBG ("on_write_complete: ec=%s, bytes=%zu", ec.message ().c_str (),
                   bytes_transferred);

    if (_terminating)
        return;

    if (ec) {
        if (ec == boost::asio::error::operation_aborted)
            return;
        WS_ENGINE_DBG ("Write error");
        error (connection_error);
        return;
    }

    //  Clear write buffer - async write is complete
    _write_buffer.clear ();

    //  If still handshaking and there's nothing more to write, just return
    if (_handshaking && _outsize == 0)
        return;

    //  Continue writing if more data available
    if (!_output_stopped)
        start_async_write ();
}

bool zmq::asio_ws_engine_t::handshake ()
{
    if (_zmp_mode)
        return handshake_zmp ();

    WS_ENGINE_DBG ("handshake: bytes_read=%u, size=%zu", _greeting_bytes_read,
                   _greeting_size);

    //  Need at least signature (10 bytes) to detect protocol
    if (_greeting_bytes_read < signature_size)
        return false;

    //  Check ZMTP signature
    if (_greeting_recv[0] != 0xff || (_greeting_recv[9] & 0x01) == 0) {
        //  Not ZMTP 3.x, check for older versions
        if (_greeting_recv[0] != 0xff) {
            WS_ENGINE_DBG ("Invalid ZMTP signature");
            error (protocol_error);
            return false;
        }
    }

    //  Need full v3 greeting
    if (_greeting_bytes_read < v3_greeting_size)
        return false;

    //  Verify the protocol version
    if (_greeting_recv[10] != 3) {
        WS_ENGINE_DBG ("Unsupported ZMTP version: %d", _greeting_recv[10]);
        error (protocol_error);
        return false;
    }

    WS_ENGINE_DBG ("ZMTP handshake complete, version 3.%d", _greeting_recv[11]);
    return true;
}

bool zmq::asio_ws_engine_t::handshake_zmp ()
{
    if (!_hello_received) {
        if (!receive_hello ()) {
            errno = EAGAIN;
            return false;
        }
    }

    if (!_hello_sent)
        return false;

    return true;
}

bool zmq::asio_ws_engine_t::receive_hello ()
{
    while (_insize > 0) {
        if (_hello_header_bytes < zmp_header_size) {
            const size_t to_copy =
              std::min (_insize, zmp_header_size - _hello_header_bytes);
            memcpy (_hello_recv + _hello_header_bytes, _inpos, to_copy);
            _hello_header_bytes += to_copy;
            _inpos += to_copy;
            _insize -= to_copy;
            if (_hello_header_bytes < zmp_header_size)
                return false;
            _hello_body_len = get_uint32 (_hello_recv + 4);
            if (_hello_body_len < zmp_hello_min_body
                || _hello_body_len > zmp_hello_max_body) {
                errno = EPROTO;
                error (protocol_error);
                return false;
            }
        }

        if (_hello_body_bytes < _hello_body_len) {
            const size_t to_copy =
              std::min (_insize, static_cast<size_t> (_hello_body_len)
                                    - _hello_body_bytes);
            memcpy (_hello_recv + zmp_header_size + _hello_body_bytes, _inpos,
                    to_copy);
            _hello_body_bytes += to_copy;
            _inpos += to_copy;
            _insize -= to_copy;
            if (_hello_body_bytes < _hello_body_len)
                return false;
        }

        if (parse_hello (_hello_recv, zmp_header_size + _hello_body_len)) {
            _hello_received = true;
            return true;
        }

        return false;
    }

    return false;
}

bool zmq::asio_ws_engine_t::parse_hello (const unsigned char *data_,
                                         size_t size_)
{
    if (size_ < zmp_header_size + zmp_hello_min_body) {
        errno = EPROTO;
        error (protocol_error);
        return false;
    }

    if (data_[0] != zmp_magic || data_[1] != zmp_version) {
        errno = EPROTO;
        _socket->event_handshake_failed_protocol (
          _session->get_endpoint (),
          ZMQ_PROTOCOL_ERROR_ZMTP_MALFORMED_COMMAND_HELLO);
        error (protocol_error);
        return false;
    }

    const unsigned char flags = data_[2];
    if (data_[3] != 0 || flags != zmp_flag_control) {
        errno = EPROTO;
        _socket->event_handshake_failed_protocol (
          _session->get_endpoint (),
          ZMQ_PROTOCOL_ERROR_ZMTP_MALFORMED_COMMAND_HELLO);
        error (protocol_error);
        return false;
    }

    const uint32_t body_len = get_uint32 (data_ + 4);
    if (body_len + zmp_header_size != size_) {
        errno = EPROTO;
        error (protocol_error);
        return false;
    }

    const unsigned char *body = data_ + zmp_header_size;
    if (body[0] != zmp_control_hello) {
        errno = EPROTO;
        error (protocol_error);
        return false;
    }

    const int peer_type = body[1];
    if (!is_socket_type_compatible (peer_type)) {
        errno = EPROTO;
        error (protocol_error);
        return false;
    }

    const unsigned char identity_len = body[2];
    if (body_len != static_cast<uint32_t> (zmp_hello_min_body + identity_len)) {
        errno = EPROTO;
        error (protocol_error);
        return false;
    }

    _peer_routing_id_size = identity_len;
    if (identity_len > 0)
        memcpy (_peer_routing_id, body + zmp_hello_min_body, identity_len);

    return true;
}

bool zmq::asio_ws_engine_t::is_socket_type_compatible (int peer_type_) const
{
    switch (_options.type) {
        case ZMQ_DEALER:
            return peer_type_ == ZMQ_DEALER || peer_type_ == ZMQ_ROUTER;
        case ZMQ_ROUTER:
            return peer_type_ == ZMQ_DEALER || peer_type_ == ZMQ_ROUTER;
        case ZMQ_PUB:
            return peer_type_ == ZMQ_SUB || peer_type_ == ZMQ_XSUB;
        case ZMQ_SUB:
            return peer_type_ == ZMQ_PUB || peer_type_ == ZMQ_XPUB;
        case ZMQ_XPUB:
            return peer_type_ == ZMQ_SUB || peer_type_ == ZMQ_XSUB;
        case ZMQ_XSUB:
            return peer_type_ == ZMQ_PUB || peer_type_ == ZMQ_XPUB;
        case ZMQ_PAIR:
            return peer_type_ == ZMQ_PAIR;
        default:
            break;
    }
    return false;
}

void zmq::asio_ws_engine_t::mechanism_ready ()
{
    WS_ENGINE_DBG ("mechanism_ready");

    if (_options.heartbeat_interval > 0) {
        _heartbeat_timeout = _options.heartbeat_timeout;
        if (_heartbeat_timeout == -1)
            _heartbeat_timeout = _options.heartbeat_interval;
    }

    //  Compile metadata
    properties_t properties;
    init_properties (properties);

    zmq_assert (_metadata == NULL);
    if (!properties.empty ()) {
        _metadata = new (std::nothrow) metadata_t (properties);
        alloc_assert (_metadata);
    }

    //  Notify session that engine is ready to send/receive messages
    if (_session) {
        _session->engine_ready ();
    }

    //  Notify socket about successful handshake
    if (_socket) {
        _socket->event_handshake_succeeded (_endpoint_uri_pair, 0);
    }

    //  Trigger output to start sending any pending messages
    if (!_output_stopped && !_write_pending) {
        start_async_write ();
    }
}

bool zmq::asio_ws_engine_t::process_input ()
{
    WS_ENGINE_DBG ("process_input: insize=%zu, inpos=%p, in_decoder_buffer=%d",
                   _insize, static_cast<void *> (_inpos), _input_in_decoder_buffer);

    if (!_decoder)
        return true;

    const bool input_in_decoder_buffer = _input_in_decoder_buffer;

    int rc = 0;
    size_t processed = 0;

    while (_insize > 0) {
        unsigned char *decode_buf;
        size_t decode_size;

        decode_buf = _inpos;
        decode_size = _insize;

        if (!input_in_decoder_buffer) {
            //  Data came from external buffer (e.g., greeting); copy into
            //  decoder buffer to preserve decoder buffer invariants.
            _decoder->get_buffer (&decode_buf, &decode_size);
            decode_size = std::min (_insize, decode_size);
            memcpy (decode_buf, _inpos, decode_size);
            _decoder->resize_buffer (decode_size);
            WS_ENGINE_DBG ("process_input: copied %zu bytes from external buffer to decoder", decode_size);
        }

        WS_ENGINE_DBG ("process_input: calling decode with size=%zu", decode_size);
        rc = _decoder->decode (decode_buf, decode_size, processed);
        WS_ENGINE_DBG ("process_input: decode rc=%d, processed=%zu", rc, processed);

        zmq_assert (processed <= decode_size);
        _inpos += processed;
        _insize -= processed;

        if (rc == 0 || rc == -1)
            break;

        //  Message decoded, push to session
        msg_t *msg = _decoder->msg ();
        WS_ENGINE_DBG ("process_input: got message, size=%zu, flags=0x%x",
                       msg->size (), msg->flags ());
        rc = (this->*_process_msg) (msg);
        WS_ENGINE_DBG ("process_input: process_msg rc=%d", rc);
        if (rc == -1)
            break;
    }

    //  Tear down connection on decode/process error
    if (rc == -1) {
        if (errno != EAGAIN) {
            WS_ENGINE_DBG ("process_input: decode/process error, errno=%d", errno);
            error (protocol_error);
            return false;
        }
        _input_stopped = true;
    }

    //  Flush any messages to the socket
    if (_session) {
        _session->flush ();
    }

    return true;
}

bool zmq::asio_ws_engine_t::prepare_output_buffer ()
{
    WS_ENGINE_DBG ("prepare_output_buffer: outsize=%zu", _outsize);

    //  If we already have data prepared, return true.
    if (_outsize > 0)
        return true;

    if (!_encoder)
        return false;

    //  Get initial data from encoder
    _outpos = NULL;
    _outsize = _encoder->encode (&_outpos, 0);

    //  Fill the output buffer up to batch size
    while (_outsize < static_cast<size_t> (_options.out_batch_size)) {
        msg_t msg;
        int rc = msg.init ();
        errno_assert (rc == 0);

        rc = (this->*_next_msg) (&msg);
        if (rc == -1) {
            rc = msg.close ();
            errno_assert (rc == 0);
            //  Note: we don't set _output_stopped here, let caller decide
            break;
        }

        _encoder->load_msg (&msg);
        unsigned char *bufptr = _outpos + _outsize;
        const size_t n =
          _encoder->encode (&bufptr, _options.out_batch_size - _outsize);
        zmq_assert (n > 0);
        if (_outpos == NULL)
            _outpos = bufptr;
        _outsize += n;
    }

    WS_ENGINE_DBG ("prepare_output_buffer: prepared %zu bytes", _outsize);
    return _outsize > 0;
}

void zmq::asio_ws_engine_t::speculative_write ()
{
    WS_ENGINE_DBG ("speculative_write: write_pending=%d, output_stopped=%d",
                   _write_pending, _output_stopped);

    //  Guard: If async write is already in progress, skip.
    //  This ensures single write-in-flight invariant.
    if (_write_pending)
        return;

    //  Guard: Don't write during I/O errors
    if (_io_error)
        return;

    //  Guard: WebSocket handshake must be complete
    if (!_ws_handshake_complete)
        return;

    //  Prepare output buffer from encoder
    if (!prepare_output_buffer ()) {
        _output_stopped = true;
        WS_ENGINE_DBG ("speculative_write: no data to send, output_stopped=true");
        return;
    }

    if (!_transport->supports_speculative_write ()) {
        WS_ENGINE_DBG ("speculative_write: transport prefers async");
        start_async_write ();
        return;
    }

    //  Attempt synchronous write using transport's write_some()
    //  Note: For WebSocket, this writes a complete frame or returns 0 with EAGAIN
    zmq_assert (_transport);
    const std::size_t bytes =
      _transport->write_some (reinterpret_cast<const std::uint8_t *> (_outpos),
                              _outsize);

    WS_ENGINE_DBG ("speculative_write: write_some returned %zu, errno=%d", bytes,
                   errno);

    if (bytes == 0) {
        //  Check if it's would_block (retry later) or actual error
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            //  would_block - fall back to async write.
            WS_ENGINE_DBG ("speculative_write: would_block, falling back to async");
            start_async_write ();
            return;
        }
        //  Actual error
        WS_ENGINE_DBG ("speculative_write: error, errno=%d", errno);
        error (connection_error);
        return;
    }

    //  For WebSocket, frame-based write means we either wrote the entire
    //  frame or got would_block. Update buffer pointers.
    _outpos += bytes;
    _outsize -= bytes;

    WS_ENGINE_DBG ("speculative_write: wrote %zu bytes, remaining=%zu", bytes,
                   _outsize);

    if (_outsize > 0) {
        //  Partial write (shouldn't happen for WebSocket frame-based write,
        //  but handle it for robustness).
        WS_ENGINE_DBG ("speculative_write: partial write, async for remaining %zu",
                       _outsize);
        start_async_write ();
    } else {
        //  Complete write succeeded.
        //  Try to get more data and continue speculative writing.
        _output_stopped = false;

        //  During handshake, don't loop - let handshake control flow proceed.
        if (_handshaking)
            return;

        //  Try to prepare and write more data speculatively.
        while (prepare_output_buffer ()) {
            const std::size_t more_bytes = _transport->write_some (
              reinterpret_cast<const std::uint8_t *> (_outpos), _outsize);

            WS_ENGINE_DBG ("speculative_write loop: wrote %zu, errno=%d",
                           more_bytes, errno);

            if (more_bytes == 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    //  would_block - remaining data via async
                    start_async_write ();
                    return;
                }
                //  Actual error
                error (connection_error);
                return;
            }

            _outpos += more_bytes;
            _outsize -= more_bytes;

            if (_outsize > 0) {
                //  Partial write - async for remaining
                start_async_write ();
                return;
            }
        }

        //  No more data to send
        _output_stopped = true;
        WS_ENGINE_DBG ("speculative_write: all data sent, output_stopped=true");
    }
}

void zmq::asio_ws_engine_t::process_output ()
{
    WS_ENGINE_DBG ("process_output: outsize=%zu", _outsize);

    //  This function is called from start_async_write() when there's no data
    //  in _outpos/_outsize. It fills the encoder buffer and copies to _write_buffer
    //  for async write operation.
    //
    //  NOTE: For zero-copy, speculative_write() uses prepare_output_buffer() which
    //  sets _outpos/_outsize to point directly to encoder buffer. The copy here
    //  is only for the async-only path (not via speculative_write).

    if (!_encoder)
        return;

    //  If there's still data from previous encode, don't get more
    if (_outsize > 0)
        return;

    //  Get initial data from encoder
    _outpos = NULL;
    _outsize = _encoder->encode (&_outpos, 0);

    //  Fill the output buffer up to batch size
    while (_outsize < static_cast<size_t> (_options.out_batch_size)) {
        msg_t msg;
        int rc = msg.init ();
        errno_assert (rc == 0);

        rc = (this->*_next_msg) (&msg);
        if (rc == -1) {
            rc = msg.close ();
            errno_assert (rc == 0);
            if (errno == EAGAIN) {
                _output_stopped = true;
            } else {
                error (protocol_error);
            }
            break;
        }

        _encoder->load_msg (&msg);
        unsigned char *bufptr = _outpos + _outsize;
        const size_t n =
          _encoder->encode (&bufptr, _options.out_batch_size - _outsize);
        zmq_assert (n > 0);
        if (_outpos == NULL)
            _outpos = bufptr;
        _outsize += n;
    }

    //  If there is no data to send, return
    if (_outsize == 0)
        return;

    //  Copy data to write buffer for async write operation.
    //  This copy is necessary because encoder buffer may be invalidated before
    //  async write completes. The copy is performed here (for async-only path)
    //  or in start_async_write() (for speculative_write fallback path).
    const size_t out_batch_size =
      static_cast<size_t> (_options.out_batch_size);
    const size_t target =
      _outsize > out_batch_size ? _outsize : out_batch_size;
    if (_write_buffer.capacity () < target)
        _write_buffer.reserve (target);
    _write_buffer.assign (_outpos, _outpos + _outsize);

    WS_ENGINE_DBG ("process_output: copied %zu bytes to write buffer", _outsize);

    //  Clear _outpos/_outsize as data is now in _write_buffer
    _outpos = NULL;
    _outsize = 0;
}

int zmq::asio_ws_engine_t::pull_msg_from_session (msg_t *msg_)
{
    return _session->pull_msg (msg_);
}

int zmq::asio_ws_engine_t::push_msg_to_session (msg_t *msg_)
{
    return _session->push_msg (msg_);
}

void zmq::asio_ws_engine_t::error (error_reason_t reason_)
{
    WS_ENGINE_DBG ("error: reason=%d", reason_);

    _io_error = true;

    if (_session) {
        _session->engine_error (false, reason_);
    }
}

void zmq::asio_ws_engine_t::terminate ()
{
    WS_ENGINE_DBG ("terminate: read_pending=%d, write_pending=%d",
                   _read_pending, _write_pending);

    _terminating = true;

    //  Cancel all timers
    if (_has_handshake_timer) {
        cancel_timer (handshake_timer_id);
        _has_handshake_timer = false;
    }

    if (_has_ttl_timer) {
        cancel_timer (heartbeat_ttl_timer_id);
        _has_ttl_timer = false;
    }

    if (_has_timeout_timer) {
        cancel_timer (heartbeat_timeout_timer_id);
        _has_timeout_timer = false;
    }

    if (_has_heartbeat_timer) {
        cancel_timer (heartbeat_ivl_timer_id);
        _has_heartbeat_timer = false;
    }

    //  Close transport - this will cause pending async ops to fail
    if (_transport) {
        _transport->close ();
    }

    _plugged = false;
    _session = NULL;

    //  Run pending async handlers until they complete
    //  The close() above will cause pending reads/writes to fail with
    //  operation_aborted or eof, and our handlers check _terminating flag
    if (_io_context) {
        //  Run until no more pending handlers
        int max_iterations = 100;
        while ((_read_pending || _write_pending) && max_iterations-- > 0) {
            WS_ENGINE_DBG ("terminate: polling, read_pending=%d, write_pending=%d",
                           _read_pending, _write_pending);
            size_t handled = _io_context->poll_one ();
            if (handled == 0) {
                WS_ENGINE_DBG ("terminate: poll_one returned 0");
                break;
            }
        }
    }

    WS_ENGINE_DBG ("terminate: done, deleting this");
    delete this;
}

bool zmq::asio_ws_engine_t::restart_input ()
{
    WS_ENGINE_DBG ("restart_input");

    _input_stopped = false;

    if (!_read_pending) {
        start_async_read ();
    }

    return true;
}

void zmq::asio_ws_engine_t::restart_output ()
{
    WS_ENGINE_DBG ("restart_output: io_error=%d, output_stopped=%d, write_pending=%d",
                   _io_error, _output_stopped, _write_pending);

    if (_io_error)
        return;

    _output_stopped = false;

    //  Use speculative write for immediate transmission.
    //  This tries synchronous write first, falling back to async if needed.
    speculative_write ();
}

void zmq::asio_ws_engine_t::zap_msg_available ()
{
    //  ZAP not supported in this implementation
}

const zmq::endpoint_uri_pair_t &zmq::asio_ws_engine_t::get_endpoint () const
{
    return _endpoint_uri_pair;
}

void zmq::asio_ws_engine_t::set_handshake_timer ()
{
    add_timer (_options.handshake_ivl, handshake_timer_id);
    _has_handshake_timer = true;
}

void zmq::asio_ws_engine_t::cancel_handshake_timer ()
{
    cancel_timer (handshake_timer_id);
    _has_handshake_timer = false;
}

void zmq::asio_ws_engine_t::add_timer (int timeout_, int id_)
{
    if (!_timer)
        return;

    _current_timer_id = id_;

    _timer->expires_after (std::chrono::milliseconds (timeout_));
    _timer->async_wait ([this, id_] (const boost::system::error_code &ec) {
        on_timer (id_, ec);
    });
}

void zmq::asio_ws_engine_t::cancel_timer (int id_)
{
    if (!_timer)
        return;

    if (_current_timer_id == id_) {
        _timer->cancel ();
        _current_timer_id = -1;
    }
}

void zmq::asio_ws_engine_t::on_timer (int id_,
                                       const boost::system::error_code &ec)
{
    if (_terminating || ec == boost::asio::error::operation_aborted)
        return;

    WS_ENGINE_DBG ("on_timer: id=%d", id_);

    if (id_ == handshake_timer_id) {
        _has_handshake_timer = false;
        error (timeout_error);
    }
}

bool zmq::asio_ws_engine_t::init_properties (properties_t &properties_)
{
    if (_peer_address.empty ())
        return false;

    properties_.ZMQ_MAP_INSERT_OR_EMPLACE (
      std::string (ZMQ_MSG_PROPERTY_PEER_ADDRESS), _peer_address);

    return true;
}

int zmq::asio_ws_engine_t::pull_and_encode (msg_t *msg_)
{
    return pull_msg_from_session (msg_);
}

int zmq::asio_ws_engine_t::decode_and_push (msg_t *msg_)
{
    if (!_zmp_mode)
        return push_msg_to_session (msg_);

    if (msg_->flags () & msg_t::command) {
        const int rc = process_command_message (msg_);
        if (rc != 0)
            return -1;
        return 0;
    }

    if ((msg_->flags () & msg_t::routing_id) && !_options.recv_routing_id) {
        int rc = msg_->close ();
        errno_assert (rc == 0);
        rc = msg_->init ();
        errno_assert (rc == 0);
        return 0;
    }

    if (_metadata)
        msg_->set_metadata (_metadata);

    if (push_msg_to_session (msg_) == -1) {
        if (errno == EAGAIN)
            _process_msg = &asio_ws_engine_t::push_one_then_decode_and_push;
        return -1;
    }
    return 0;
}

int zmq::asio_ws_engine_t::push_one_then_decode_and_push (msg_t *msg_)
{
    return push_msg_to_session (msg_);
}

int zmq::asio_ws_engine_t::next_handshake_command (msg_t *msg_)
{
    LIBZMQ_UNUSED (msg_);
    return -1;
}

int zmq::asio_ws_engine_t::process_handshake_command (msg_t *msg_)
{
    LIBZMQ_UNUSED (msg_);
    return -1;
}

int zmq::asio_ws_engine_t::write_credential (msg_t *msg_)
{
    LIBZMQ_UNUSED (msg_);
    return -1;
}

int zmq::asio_ws_engine_t::process_command_message (msg_t *msg_)
{
    if (!_zmp_mode) {
        LIBZMQ_UNUSED (msg_);
        return -1;
    }

    if (msg_->size () < 1)
        return -1;

    const uint8_t type = *(static_cast<const uint8_t *> (msg_->data ()));
    if (type == zmp_control_heartbeat || type == zmp_control_heartbeat_ack)
        return process_heartbeat_message (msg_);

    errno = EPROTO;
    return -1;
}

int zmq::asio_ws_engine_t::produce_ping_message (msg_t *msg_)
{
    if (!_zmp_mode) {
        LIBZMQ_UNUSED (msg_);
        return -1;
    }

    int rc = msg_->init_size (1);
    errno_assert (rc == 0);
    msg_->set_flags (msg_t::command);
    *static_cast<unsigned char *> (msg_->data ()) = zmp_control_heartbeat;

    _next_msg = &asio_ws_engine_t::pull_msg_from_session;
    if (!_has_timeout_timer && _heartbeat_timeout > 0) {
        add_timer (_heartbeat_timeout, heartbeat_timeout_timer_id);
        _has_timeout_timer = true;
    }

    return 0;
}

int zmq::asio_ws_engine_t::process_heartbeat_message (msg_t *msg_)
{
    if (!_zmp_mode) {
        LIBZMQ_UNUSED (msg_);
        return -1;
    }

    if (msg_->size () != 1) {
        errno = EPROTO;
        return -1;
    }
    return 0;
}

int zmq::asio_ws_engine_t::produce_pong_message (msg_t *msg_)
{
    LIBZMQ_UNUSED (msg_);
    return -1;
}

int zmq::asio_ws_engine_t::routing_id_msg (msg_t *msg_)
{
    const int rc = msg_->init_size (_options.routing_id_size);
    errno_assert (rc == 0);
    if (_options.routing_id_size > 0)
        memcpy (msg_->data (), _options.routing_id, _options.routing_id_size);
    _next_msg = &asio_ws_engine_t::pull_msg_from_session;
    return 0;
}

int zmq::asio_ws_engine_t::process_routing_id_msg (msg_t *msg_)
{
    if (_options.recv_routing_id) {
        msg_->set_flags (msg_t::routing_id);
        const int rc = _session->push_msg (msg_);
        errno_assert (rc == 0);
    } else {
        int rc = msg_->close ();
        errno_assert (rc == 0);
        rc = msg_->init ();
        errno_assert (rc == 0);
    }

    if (_subscription_required) {
        msg_t subscription;

        int rc = subscription.init_size (1);
        errno_assert (rc == 0);
        *static_cast<unsigned char *> (subscription.data ()) = 1;
        rc = _session->push_msg (&subscription);
        errno_assert (rc == 0);
    }

    _process_msg = &asio_ws_engine_t::push_msg_to_session;

    return 0;
}

#endif  // ZMQ_IOTHREAD_POLLER_USE_ASIO && ZMQ_HAVE_WS
