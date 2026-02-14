/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"
#if defined ZLINK_IOTHREAD_POLLER_USE_ASIO

#include "engine/asio/asio_stream_engine.hpp"

#include "core/io_thread.hpp"
#include "core/msg.hpp"
#include "core/session_base.hpp"
#include "protocol/wire.hpp"
#include "sockets/socket_base.hpp"
#include "transports/tcp/tcp_transport.hpp"
#include "utils/err.hpp"
#include "zlink.h"

#ifndef ZLINK_HAVE_WINDOWS
#include <unistd.h>
#endif

#include <algorithm>
#include <limits>

namespace
{
size_t normalize_buffer_size (size_t value_, size_t fallback_)
{
    return value_ > 0 ? value_ : fallback_;
}
}

zlink::asio_stream_engine_t::asio_stream_engine_t (
  fd_t fd_,
  const options_t &options_,
  const endpoint_uri_pair_t &endpoint_uri_pair_) :
    _options (options_),
    _endpoint_uri_pair (endpoint_uri_pair_),
    _fd (fd_),
    _transport (new (std::nothrow) tcp_transport_t ()),
    _io_context (NULL),
    _has_handshake_stage (true),
    _handshaking (true),
    _plugged (false),
    _terminating (false),
    _io_error (false),
    _read_pending (false),
    _write_pending (false),
    _input_stopped (false),
    _output_stopped (false),
    _recv_buffer (normalize_buffer_size (_options.in_batch_size, 65536)),
    _recv_size (0),
    _recv_limit (
      _options.maxmsgsize > 0 ? static_cast<size_t> (_options.maxmsgsize)
                              : static_cast<size_t> (8 * 1024 * 1024)),
    _send_buffer_flush_offset (0),
    _send_buffer_limit (
      std::max<size_t> (normalize_buffer_size (_options.out_batch_size, 65536),
                        static_cast<size_t> (512 * 1024))),
    _session (NULL),
    _socket (NULL)
{
    alloc_assert (_transport);
}

zlink::asio_stream_engine_t::asio_stream_engine_t (
  fd_t fd_,
  const options_t &options_,
  const endpoint_uri_pair_t &endpoint_uri_pair_,
  std::unique_ptr<i_asio_transport> transport_) :
    _options (options_),
    _endpoint_uri_pair (endpoint_uri_pair_),
    _fd (fd_),
    _transport (std::move (transport_)),
    _io_context (NULL),
    _has_handshake_stage (true),
    _handshaking (true),
    _plugged (false),
    _terminating (false),
    _io_error (false),
    _read_pending (false),
    _write_pending (false),
    _input_stopped (false),
    _output_stopped (false),
    _recv_buffer (normalize_buffer_size (_options.in_batch_size, 65536)),
    _recv_size (0),
    _recv_limit (
      _options.maxmsgsize > 0 ? static_cast<size_t> (_options.maxmsgsize)
                              : static_cast<size_t> (8 * 1024 * 1024)),
    _send_buffer_flush_offset (0),
    _send_buffer_limit (
      std::max<size_t> (normalize_buffer_size (_options.out_batch_size, 65536),
                        static_cast<size_t> (512 * 1024))),
    _session (NULL),
    _socket (NULL)
{
    if (!_transport) {
        _transport = std::unique_ptr<i_asio_transport> (
          new (std::nothrow) tcp_transport_t ());
    }
    alloc_assert (_transport);
}

#if defined ZLINK_HAVE_ASIO_SSL
zlink::asio_stream_engine_t::asio_stream_engine_t (
  fd_t fd_,
  const options_t &options_,
  const endpoint_uri_pair_t &endpoint_uri_pair_,
  std::unique_ptr<i_asio_transport> transport_,
  std::unique_ptr<boost::asio::ssl::context> ssl_context_) :
    _options (options_),
    _endpoint_uri_pair (endpoint_uri_pair_),
    _fd (fd_),
    _transport (std::move (transport_)),
    _io_context (NULL),
    _has_handshake_stage (true),
    _handshaking (true),
    _plugged (false),
    _terminating (false),
    _io_error (false),
    _read_pending (false),
    _write_pending (false),
    _input_stopped (false),
    _output_stopped (false),
    _recv_buffer (normalize_buffer_size (_options.in_batch_size, 65536)),
    _recv_size (0),
    _recv_limit (
      _options.maxmsgsize > 0 ? static_cast<size_t> (_options.maxmsgsize)
                              : static_cast<size_t> (8 * 1024 * 1024)),
    _send_buffer_flush_offset (0),
    _send_buffer_limit (
      std::max<size_t> (normalize_buffer_size (_options.out_batch_size, 65536),
                        static_cast<size_t> (512 * 1024))),
    _session (NULL),
    _socket (NULL),
    _ssl_context (std::move (ssl_context_))
{
    if (!_transport) {
        _transport = std::unique_ptr<i_asio_transport> (
          new (std::nothrow) tcp_transport_t ());
    }
    alloc_assert (_transport);
}
#endif

zlink::asio_stream_engine_t::~asio_stream_engine_t ()
{
    if (_transport) {
        _transport->close ();
    } else if (_fd != retired_fd) {
#ifdef ZLINK_HAVE_WINDOWS
        const int rc = closesocket (_fd);
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
}

bool zlink::asio_stream_engine_t::has_handshake_stage ()
{
    return _has_handshake_stage;
}

void zlink::asio_stream_engine_t::plug (io_thread_t *io_thread_,
                                        session_base_t *session_)
{
    zlink_assert (!_plugged);
    zlink_assert (session_);

    _plugged = true;
    _session = session_;
    _socket = _session->get_socket ();
    _io_context = &io_thread_->get_io_context ();
    _io_error = false;
    _terminating = false;

    if (!_transport || !_transport->open (*_io_context, _fd)) {
        error (connection_error);
        return;
    }

    if (_transport->requires_handshake ()) {
        start_transport_handshake ();
        return;
    }

    complete_handshake ();
}

void zlink::asio_stream_engine_t::start_transport_handshake ()
{
    const int handshake_type =
      _endpoint_uri_pair.local_type == endpoint_type_connect ? 0 : 1;

    _transport->async_handshake (
      handshake_type,
      [this] (const boost::system::error_code &ec, std::size_t) {
          on_transport_handshake (ec);
      });
}

void zlink::asio_stream_engine_t::on_transport_handshake (
  const boost::system::error_code &ec)
{
    if (_terminating)
        return;
    if (!_plugged)
        return;
    if (ec) {
        error (connection_error);
        return;
    }

    complete_handshake ();
}

void zlink::asio_stream_engine_t::complete_handshake ()
{
    if (_terminating || !_plugged)
        return;

    _handshaking = false;
    if (_has_handshake_stage) {
        _session->engine_ready ();
        _has_handshake_stage = false;
    }

    _session->set_peer_routing_id (NULL, 0);
    if (_socket) {
        _socket->event_connection_ready (_endpoint_uri_pair, NULL, 0);
    }

    fill_send_main_buffer ();
    start_async_write ();
    start_async_read ();
}

void zlink::asio_stream_engine_t::unplug ()
{
    if (!_plugged)
        return;

    _plugged = false;
    if (_transport)
        _transport->close ();

    _session = NULL;
    _socket = NULL;
}

void zlink::asio_stream_engine_t::terminate ()
{
    if (_terminating)
        return;

    _terminating = true;
    unplug ();

    if (_io_context && (_read_pending || _write_pending)) {
        _io_context->poll ();
    }

    delete this;
}

bool zlink::asio_stream_engine_t::restart_input ()
{
    if (_terminating || _io_error)
        return false;

    _input_stopped = false;
    if (!process_input_buffer ())
        return false;
    if (!_input_stopped)
        start_async_read ();
    return !_terminating;
}

void zlink::asio_stream_engine_t::restart_output ()
{
    if (_terminating || _io_error)
        return;

    fill_send_main_buffer ();
    start_async_write ();
}

const zlink::endpoint_uri_pair_t &zlink::asio_stream_engine_t::get_endpoint () const
{
    return _endpoint_uri_pair;
}

void zlink::asio_stream_engine_t::start_async_read ()
{
    if (_read_pending || _input_stopped || _io_error || !_transport
        || !_transport->is_open ())
        return;

    const size_t min_free = 4096;
    if (_recv_buffer.size () - _recv_size < min_free) {
        size_t new_size =
          _recv_buffer.empty () ? 65536 : (_recv_buffer.size () * 2);
        if (new_size - _recv_size < min_free)
            new_size = _recv_size + min_free;
        _recv_buffer.resize (new_size);
    }

    _read_pending = true;
    _transport->async_read_some (
      &_recv_buffer[0] + _recv_size, _recv_buffer.size () - _recv_size,
      [this] (const boost::system::error_code &ec, std::size_t bytes) {
          on_read_complete (ec, bytes);
      });
}

void zlink::asio_stream_engine_t::on_read_complete (
  const boost::system::error_code &ec,
  std::size_t bytes_transferred)
{
    _read_pending = false;

    if (_terminating)
        return;
    if (!_plugged)
        return;

    if (ec) {
        if (ec == boost::asio::error::operation_aborted)
            return;
        error (connection_error);
        return;
    }

    if (bytes_transferred == 0) {
        error (connection_error);
        return;
    }

    _recv_size += bytes_transferred;
    if (!process_input_buffer ())
        return;

    if (!_input_stopped)
        start_async_read ();
}

bool zlink::asio_stream_engine_t::process_input_buffer ()
{
    size_t offset = 0;
    bool pushed_any = false;

    while (!_input_stopped) {
        if ((_recv_size - offset) < 4)
            break;

        const uint32_t payload_size = get_uint32 (&_recv_buffer[offset]);
        if (payload_size == 0) {
            errno = EPROTO;
            error (protocol_error);
            return false;
        }
        if (payload_size > _recv_limit) {
            errno = EMSGSIZE;
            error (protocol_error);
            return false;
        }

        const size_t packet_size = 4U + static_cast<size_t> (payload_size);
        if ((_recv_size - offset) < packet_size)
            break;

        if (!push_one_frame (&_recv_buffer[offset + 4],
                             static_cast<size_t> (payload_size))) {
            if (_input_stopped)
                break;
            return false;
        }
        pushed_any = true;

        if (!_input_stopped)
            offset += packet_size;
    }

    if (offset > 0) {
        if (offset < _recv_size) {
            memmove (&_recv_buffer[0], &_recv_buffer[offset], _recv_size - offset);
            _recv_size -= offset;
        } else {
            _recv_size = 0;
        }
    }

    if (pushed_any && _session)
        _session->flush ();

    return true;
}

bool zlink::asio_stream_engine_t::push_one_frame (const unsigned char *data_,
                                                  size_t size_)
{
    if (!_session)
        return false;

    msg_t msg;
    int rc = msg.init_size (size_);
    errno_assert (rc == 0);
    if (size_ > 0)
        memcpy (msg.data (), data_, size_);

    if (_session->push_msg (&msg) == -1) {
        rc = msg.close ();
        errno_assert (rc == 0);
        if (errno == EAGAIN) {
            _input_stopped = true;
            return false;
        }
        error (connection_error);
        return false;
    }

    rc = msg.close ();
    errno_assert (rc == 0);
    return true;
}

void zlink::asio_stream_engine_t::fill_send_main_buffer ()
{
    if (!_session || _io_error)
        return;

    msg_t msg;
    int rc = msg.init ();
    errno_assert (rc == 0);

    while (_send_buffer_main.size () < _send_buffer_limit) {
        if (_session->pull_msg (&msg) == -1) {
            if (errno == EAGAIN) {
                _output_stopped = true;
                break;
            }
            rc = msg.close ();
            errno_assert (rc == 0);
            error (connection_error);
            return;
        }

        const size_t msg_size = msg.size ();
        if (msg_size > static_cast<size_t> (std::numeric_limits<uint32_t>::max ())) {
            rc = msg.close ();
            errno_assert (rc == 0);
            errno = EMSGSIZE;
            error (protocol_error);
            return;
        }

        const size_t old_size = _send_buffer_main.size ();
        const size_t packet_size = 4U + msg_size;
        _send_buffer_main.resize (old_size + packet_size);
        put_uint32 (&_send_buffer_main[old_size], static_cast<uint32_t> (msg_size));
        if (msg_size > 0)
            memcpy (&_send_buffer_main[old_size + 4], msg.data (), msg_size);

        rc = msg.close ();
        errno_assert (rc == 0);
        rc = msg.init ();
        errno_assert (rc == 0);
        _output_stopped = false;

        if (_send_buffer_main.size () >= _send_buffer_limit)
            break;
    }

    rc = msg.close ();
    errno_assert (rc == 0);
}

void zlink::asio_stream_engine_t::start_async_write ()
{
    if (_write_pending || _io_error || _terminating || !_transport
        || !_transport->is_open ())
        return;

    if (_send_buffer_flush.empty ()) {
        if (_send_buffer_main.empty ())
            fill_send_main_buffer ();
        if (!_send_buffer_main.empty ()) {
            _send_buffer_flush.swap (_send_buffer_main);
            _send_buffer_flush_offset = 0;
        }
    }

    if (_send_buffer_flush.empty ())
        return;

    _write_pending = true;
    _transport->async_write_some (
      &_send_buffer_flush[0] + _send_buffer_flush_offset,
      _send_buffer_flush.size () - _send_buffer_flush_offset,
      [this] (const boost::system::error_code &ec, std::size_t bytes) {
          on_write_complete (ec, bytes);
      });
}

void zlink::asio_stream_engine_t::on_write_complete (
  const boost::system::error_code &ec,
  std::size_t bytes_transferred)
{
    _write_pending = false;

    if (_terminating)
        return;
    if (!_plugged)
        return;

    if (ec) {
        if (ec == boost::asio::error::operation_aborted)
            return;
        error (connection_error);
        return;
    }

    if (bytes_transferred == 0) {
        error (connection_error);
        return;
    }

    _send_buffer_flush_offset += bytes_transferred;
    if (_send_buffer_flush_offset > _send_buffer_flush.size ()) {
        errno = EPROTO;
        error (protocol_error);
        return;
    }

    if (_send_buffer_flush_offset == _send_buffer_flush.size ()) {
        _send_buffer_flush.clear ();
        _send_buffer_flush_offset = 0;
        fill_send_main_buffer ();
        if (!_send_buffer_main.empty ()) {
            _send_buffer_flush.swap (_send_buffer_main);
            _send_buffer_flush_offset = 0;
        }
    }

    start_async_write ();
}

void zlink::asio_stream_engine_t::error (i_engine::error_reason_t reason_)
{
    if (_terminating)
        return;

    _io_error = true;
    _terminating = true;

    if (_session == NULL) {
        if (_io_context) {
            boost::asio::post (*_io_context, [this] () { delete this; });
            return;
        }
        delete this;
        return;
    }

    if (reason_ != protocol_error && _handshaking && _socket) {
        const int err = errno;
        _socket->event_handshake_failed_no_detail (_endpoint_uri_pair, err);
    }

    uint64_t disconnect_reason = ZLINK_DISCONNECT_UNKNOWN;
    if (_socket && _socket->is_ctx_terminated ()) {
        disconnect_reason = ZLINK_DISCONNECT_CTX_TERM;
    } else if (_handshaking || reason_ == protocol_error) {
        disconnect_reason = ZLINK_DISCONNECT_HANDSHAKE_FAILED;
    } else if (reason_ == timeout_error || reason_ == connection_error) {
        disconnect_reason = ZLINK_DISCONNECT_TRANSPORT_ERROR;
    }

    const zlink::blob_t *routing_id = _session ? &_session->peer_routing_id ()
                                             : NULL;
    const unsigned char *routing_id_data =
      routing_id ? routing_id->data () : NULL;
    const size_t routing_id_size = routing_id ? routing_id->size () : 0;
    if (_socket) {
        _socket->event_disconnected (_endpoint_uri_pair, disconnect_reason,
                                     routing_id_data, routing_id_size);
    }

    _session->flush ();
    _session->engine_error (!_handshaking, reason_);
    unplug ();

    if (_io_context) {
        boost::asio::post (*_io_context, [this] () { delete this; });
        return;
    }

    delete this;
}

#endif  // ZLINK_IOTHREAD_POLLER_USE_ASIO
