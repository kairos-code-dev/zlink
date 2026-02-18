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
#include "utils/likely.hpp"
#include "zlink.h"

#ifndef ZLINK_HAVE_WINDOWS
#include <unistd.h>
#include <sys/uio.h>
#endif

#include <algorithm>
#include <cstdlib>
#include <limits>

namespace
{
size_t normalize_buffer_size (size_t value_, size_t fallback_)
{
    return value_ > 0 ? value_ : fallback_;
}

bool echo_loopback_enabled ()
{
    const char *env = std::getenv ("ZLINK_STREAM_ECHO_LOOPBACK");
    return env && *env && *env != '0';
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
    _recv_offset (0),
    _recv_size (0),
    _recv_limit (
      _options.maxmsgsize > 0 ? static_cast<size_t> (_options.maxmsgsize)
                              : static_cast<size_t> (8 * 1024 * 1024)),
    _send_buffer_flush_offset (0),
    _send_buffer_limit (
      std::max<size_t> (normalize_buffer_size (_options.out_batch_size, 65536),
                        static_cast<size_t> (64 * 1024))),
    _echo_loopback (echo_loopback_enabled ()),
    _session (NULL),
    _socket (NULL),
    _connection_routing_id (0),
    _read_deferred (false),
    _zero_copy_active (false),
    _on_helper_context (false)
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
    _recv_offset (0),
    _recv_size (0),
    _recv_limit (
      _options.maxmsgsize > 0 ? static_cast<size_t> (_options.maxmsgsize)
                              : static_cast<size_t> (8 * 1024 * 1024)),
    _send_buffer_flush_offset (0),
    _send_buffer_limit (
      std::max<size_t> (normalize_buffer_size (_options.out_batch_size, 65536),
                        static_cast<size_t> (64 * 1024))),
    _echo_loopback (echo_loopback_enabled ()),
    _session (NULL),
    _socket (NULL),
    _connection_routing_id (0),
    _read_deferred (false)
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
    _recv_offset (0),
    _recv_size (0),
    _recv_limit (
      _options.maxmsgsize > 0 ? static_cast<size_t> (_options.maxmsgsize)
                              : static_cast<size_t> (8 * 1024 * 1024)),
    _send_buffer_flush_offset (0),
    _send_buffer_limit (
      std::max<size_t> (normalize_buffer_size (_options.out_batch_size, 65536),
                        static_cast<size_t> (64 * 1024))),
    _echo_loopback (echo_loopback_enabled ()),
    _session (NULL),
    _socket (NULL),
    _connection_routing_id (0),
    _read_deferred (false),
    _zero_copy_active (false),
    _on_helper_context (false),
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
    _on_helper_context =
      (static_cast<void *> (io_thread_)
         == _options.direct_io_helper_thread_ptr
       || static_cast<void *> (io_thread_)
            == _options.direct_io_helper_thread2_ptr);

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

    //  Cache the routing_id for Direct IO pipe bypass.
    //  Non-zero activates the bypass path in push_one_frame/queue_direct_send.
    //  Only activate when Direct IO is in use — the bypass is NOT thread-safe
    //  for the normal multi-threaded engine path.
    if (_options.direct_io_thread_ptr)
        _connection_routing_id = _session->get_server_socket_routing_id ();

    if (_socket) {
        _socket->event_connection_ready (_endpoint_uri_pair, NULL, 0);
    }

    if (_echo_loopback) {
        //  Serialized raw echo: minimize per-connection memory footprint.
        //  Shrink recv buffer from 65KB to 4KB (saves ~60KB per connection,
        //  i.e., 600MB for 10K connections).  Free send buffers entirely.
        std::fprintf (stderr, "[DBG] echo loopback starting for fd=%d\n",
                      static_cast<int> (_fd));
        _recv_buffer.resize (4096);
        _recv_buffer.shrink_to_fit ();
        _recv_offset = 0;
        _recv_size = 0;
        _send_buffer_main.clear ();
        _send_buffer_main.shrink_to_fit ();
        _send_buffer_flush.clear ();
        _send_buffer_flush.shrink_to_fit ();
        _echo_write_size = 0;
        _echo_write_offset = 0;
        start_raw_echo_read ();
        return;
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

    //  Clear direct send engine pointer before losing _socket reference.
    //  Passing NULL msg + NULL engine_hint clears the entry safely.
    if (_socket && _connection_routing_id != 0) {
        _socket->push_msg_direct (NULL, _connection_routing_id, NULL);
        _connection_routing_id = 0;
    }

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
    if (_recv_buffer.size () - _recv_offset - _recv_size < min_free) {
        //  Compact before growing if offset is non-zero
        if (_recv_offset > 0) {
            if (_recv_size > 0)
                memmove (&_recv_buffer[0], &_recv_buffer[_recv_offset],
                         _recv_size);
            _recv_offset = 0;
        }
        if (_recv_buffer.size () - _recv_size < min_free) {
            size_t new_size =
              _recv_buffer.empty () ? 65536 : (_recv_buffer.size () * 2);
            if (new_size - _recv_size < min_free)
                new_size = _recv_size + min_free;
            _recv_buffer.resize (new_size);
        }
    }

    _read_pending = true;
    _transport->async_read_some (
      &_recv_buffer[_recv_offset + _recv_size],
      _recv_buffer.size () - _recv_offset - _recv_size,
      make_custom_alloc_handler (
        _recv_allocator,
        [this] (const boost::system::error_code &ec, std::size_t bytes) {
            on_read_complete (ec, bytes);
        }));
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

    if (!_input_stopped) {
        if (_connection_routing_id != 0 && _zero_copy_active) {
            //  Direct IO zero-copy: defer next read until the socket has
            //  consumed all zero-copy messages referencing this recv buffer.
            //  restart_deferred_read() is called by the socket when the
            //  direct recv queue is drained.
            _read_deferred = true;
        } else {
            start_async_read ();
        }
    }
}

bool zlink::asio_stream_engine_t::process_input_buffer ()
{
    size_t offset = 0;
    bool pushed_any = false;
    _zero_copy_active = false;

    while (!_input_stopped) {
        if ((_recv_size - offset) < 4)
            break;

        const uint32_t payload_size =
          get_uint32 (&_recv_buffer[_recv_offset + offset]);
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

        if (_echo_loopback) {
            //  Echo loopback: just advance offset; data stays in recv buffer.
            //  We write directly from recv buffer below (speculative write).
            pushed_any = true;
            offset += packet_size;
        } else {
            if (!push_one_frame (&_recv_buffer[_recv_offset + offset + 4],
                                 static_cast<size_t> (payload_size))) {
                if (_input_stopped)
                    break;
                return false;
            }
            pushed_any = true;

            if (!_input_stopped)
                offset += packet_size;
        }
    }

    //  Echo loopback: write echoed data before adjusting recv buffer offsets.
    //  The echoed frames are contiguous at _recv_buffer[_recv_offset..+offset).
    if (pushed_any && _echo_loopback && offset > 0) {
        if (!_write_pending && _send_buffer_flush.empty ()
            && _send_buffer_main.empty ()) {
            //  Speculative write: send directly from recv buffer.
            //  Avoids memcpy to send buffer AND async write overhead.
            size_t written_total = 0;
            while (written_total < offset) {
                const size_t n = _transport->write_some (
                  reinterpret_cast<const std::uint8_t *> (
                    &_recv_buffer[_recv_offset + written_total]),
                  offset - written_total);
                if (n == 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK)
                        break;
                    error (connection_error);
                    return false;
                }
                written_total += n;
            }
            //  Queue remainder (if any) for async write.
            if (written_total < offset) {
                const size_t remaining = offset - written_total;
                _send_buffer_main.resize (remaining);
                memcpy (&_send_buffer_main[0],
                        &_recv_buffer[_recv_offset + written_total], remaining);
                start_async_write ();
            }
        } else {
            //  Async write pending: queue in send buffer for later.
            const size_t old_size = _send_buffer_main.size ();
            _send_buffer_main.resize (old_size + offset);
            memcpy (&_send_buffer_main[old_size],
                    &_recv_buffer[_recv_offset], offset);
        }
    }

    if (offset > 0) {
        if (offset < _recv_size) {
            _recv_offset += offset;
            _recv_size -= offset;
            //  Compact only when offset exceeds half the buffer.
            //  Skip memmove when zero-copy is active: the parsed region may
            //  still be referenced by zero-copy messages in the socket's
            //  direct recv queue.  Compaction is deferred to start_async_read
            //  which runs after all zero-copy msgs have been consumed.
            if (!_zero_copy_active
                && _recv_offset > _recv_buffer.size () / 2) {
                memmove (&_recv_buffer[0], &_recv_buffer[_recv_offset],
                         _recv_size);
                _recv_offset = 0;
            }
        } else {
            _recv_size = 0;
            _recv_offset = 0;
        }
    }

    if (pushed_any && !_echo_loopback && _session) {
        if (_connection_routing_id == 0) {
            //  Normal path: flush the pipe so socket thread sees the data.
            _session->flush ();

            //  Speculative outbound send: after pushing inbound frames,
            //  immediately check the outbound ypipe for echo responses from
            //  previous socket-thread processing cycles.  This avoids waiting
            //  for the activate_write command round-trip through the io_thread
            //  mailbox, reducing per-message echo latency.
            fill_send_main_buffer ();
            if (!_send_buffer_main.empty ())
                start_async_write ();
        }
        //  Direct IO bypass: no pipe flush needed — data is in socket's
        //  direct_recv_queue.  Send is triggered by socket's xsend calling
        //  queue_direct_send, which writes to _send_buffer_main directly.
    }

    return true;
}

bool zlink::asio_stream_engine_t::push_one_frame (const unsigned char *data_,
                                                  size_t size_)
{
    if (!_session)
        return false;

    msg_t msg;
    int rc;

    //  Direct IO pipe bypass: push directly to socket, skipping pipe/ypipe/FQ.
    if (_connection_routing_id != 0 && _socket) {
        //  Zero-copy for large messages (>= 4KB): reference recv buffer
        //  directly, avoiding a memcpy.  Safe because async reads are
        //  deferred until xrecv consumes the message and restarts the
        //  specific engine (O(1) per message, no O(N) scan).
        static const size_t zero_copy_min_size = 4096;
        if (size_ >= zero_copy_min_size) {
            rc = msg.init_data (const_cast<unsigned char *> (data_), size_,
                                NULL, NULL);
            _zero_copy_active = true;
        } else {
            rc = msg.init_size (size_);
            errno_assert (rc == 0);
            if (size_ > 0)
                memcpy (msg.data (), data_, size_);
        }
        errno_assert (rc == 0);

        rc = _socket->push_msg_direct (&msg, _connection_routing_id,
                                        static_cast<void *> (this));
        if (rc == 0) {
            rc = msg.close ();
            errno_assert (rc == 0);
            return true;
        }
        //  Fall through to normal pipe path — need copy for cross-thread pipe.
        rc = msg.close ();
        errno_assert (rc == 0);
    }

    //  Normal pipe path (always copy — data crosses threads).
    rc = msg.init_size (size_);
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
    if (!_session || _io_error || _echo_loopback)
        return;

    //  Pre-reserve capacity to avoid per-frame reallocations.
    if (_send_buffer_main.capacity () < _send_buffer_limit)
        _send_buffer_main.reserve (_send_buffer_limit);

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

    //  Speculative synchronous write: try to send immediately without
    //  async callback overhead.  For small frames (e.g. 68-byte echo at
    //  64B payload) the TCP send buffer almost always has room, so this
    //  succeeds in one shot — saving an async_write_some + on_write_complete
    //  round-trip through the ASIO event loop.
    const size_t remaining =
      _send_buffer_flush.size () - _send_buffer_flush_offset;
    const size_t n = _transport->write_some (
      reinterpret_cast<const std::uint8_t *> (
        &_send_buffer_flush[_send_buffer_flush_offset]),
      remaining);

    if (n > 0) {
        _send_buffer_flush_offset += n;
        if (_send_buffer_flush_offset == _send_buffer_flush.size ()) {
            //  All data sent synchronously — no async write needed.
            _send_buffer_flush.clear ();
            _send_buffer_flush_offset = 0;
            return;
        }
    } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
        error (connection_error);
        return;
    }

    //  Partial write or would-block: fall back to async for the remainder.
    _write_pending = true;
    _transport->async_write_some (
      &_send_buffer_flush[0] + _send_buffer_flush_offset,
      _send_buffer_flush.size () - _send_buffer_flush_offset,
      make_custom_alloc_handler (
        _send_allocator,
        [this] (const boost::system::error_code &ec, std::size_t bytes) {
            on_write_complete (ec, bytes);
        }));
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

void zlink::asio_stream_engine_t::start_raw_echo_read ()
{
    if (_terminating || _io_error)
        return;

    tcp_transport_t *tcp =
      static_cast<tcp_transport_t *> (_transport.get ());
    boost::asio::ip::tcp::socket *sock = tcp->raw_socket ();
    if (unlikely (!sock || !sock->is_open ()))
        return;

    //  Serialized echo: read into _recv_buffer, then write it back.
    //  No overlap with write — the handler_allocator is always available
    //  (inline 1KB storage), avoiding heap allocation per-handler.
    sock->async_read_some (
      boost::asio::buffer (_recv_buffer.data (), _recv_buffer.size ()),
      make_custom_alloc_handler (
        _recv_allocator,
        [this] (const boost::system::error_code &ec, std::size_t bytes) {
            if (unlikely (_terminating))
                return;
            if (unlikely (ec || bytes == 0)) {
                if (ec != boost::asio::error::operation_aborted)
                    error (connection_error);
                return;
            }

            //  Echo back the exact bytes we just read — zero-copy from
            //  recv buffer.  No memcpy to send buffer needed.
            _echo_write_size = bytes;
            _echo_write_offset = 0;
            start_raw_echo_write ();
        }));
}

void zlink::asio_stream_engine_t::start_raw_echo_write ()
{
    if (_terminating || _io_error)
        return;

    tcp_transport_t *tcp =
      static_cast<tcp_transport_t *> (_transport.get ());
    boost::asio::ip::tcp::socket *sock = tcp->raw_socket ();
    if (unlikely (!sock || !sock->is_open ()))
        return;

    const size_t remaining = _echo_write_size - _echo_write_offset;
    if (remaining == 0) {
        //  All data written — start next read cycle.
        start_raw_echo_read ();
        return;
    }

    sock->async_write_some (
      boost::asio::buffer (_recv_buffer.data () + _echo_write_offset,
                           remaining),
      make_custom_alloc_handler (
        _send_allocator,
        [this] (const boost::system::error_code &ec, std::size_t bytes) {
            if (unlikely (_terminating))
                return;
            if (unlikely (ec || bytes == 0)) {
                if (ec != boost::asio::error::operation_aborted)
                    error (connection_error);
                return;
            }
            _echo_write_offset += bytes;
            //  Continue writing or start next read.
            start_raw_echo_write ();
        }));
}

int zlink::asio_stream_engine_t::queue_direct_send (msg_t *msg_)
{
    if (_io_error || _terminating || !_transport || !_transport->is_open ())
        return -1;

    const size_t msg_size = msg_->size ();

    //  Helper-context path: post write + restart to helper io_context.
    //  Each helper thread independently handles both read and write for
    //  its assigned connections, parallelizing IO across two threads.
    //  Data is copied to a local buffer because the msg may reference
    //  the engine's recv buffer (zero-copy), which becomes invalid
    //  after restart_deferred_read triggers a new async_read.
    if (_on_helper_context && msg_size > 0 && _io_context) {
        const size_t total = 4U + msg_size;
        std::vector<unsigned char> send_buf (total);
        put_uint32 (send_buf.data (), static_cast<uint32_t> (msg_size));
        memcpy (send_buf.data () + 4, msg_->data (), msg_size);

        boost::asio::post (
          *_io_context,
          [this, buf = std::move (send_buf)] () {
              if (_terminating || _io_error || !_transport
                  || !_transport->is_open ())
                  return;
              tcp_transport_t *tcp =
                static_cast<tcp_transport_t *> (_transport.get ());
              boost::asio::ip::tcp::socket *sock = tcp->raw_socket ();
              if (!sock || !sock->is_open ())
                  return;

              const unsigned char *data = buf.data ();
              const size_t sz = buf.size ();
              size_t sent = 0;
              while (sent < sz) {
                  const ssize_t n =
                    ::write (sock->native_handle (), data + sent, sz - sent);
                  if (n > 0) {
                      sent += static_cast<size_t> (n);
                  } else if (n < 0 && errno == EAGAIN) {
                      sched_yield ();
                  } else {
                      return;
                  }
              }

              //  Write complete — restart deferred read on this thread.
              //  Safe because both read and write run on the same helper.
              if (_read_deferred) {
                  _read_deferred = false;
                  if (!_input_stopped && !_terminating && !_io_error
                      && !_read_pending)
                      start_async_read ();
              }
          });
        return 1; //  Async posted; caller must NOT call restart_deferred_read.
    }

    //  Primary-context fast path: non-blocking scatter-gather write.
    //  Try synchronous write first; on partial write copy remainder to
    //  send buffer and start async_write.  Does not block the app thread.
    if (!_write_pending && _send_buffer_flush.empty ()
        && _send_buffer_main.empty () && msg_size > 0) {
        tcp_transport_t *tcp =
          static_cast<tcp_transport_t *> (_transport.get ());
        boost::asio::ip::tcp::socket *sock = tcp->raw_socket ();
        if (sock && sock->is_open ()) {
            unsigned char hdr[4];
            put_uint32 (hdr, static_cast<uint32_t> (msg_size));
            struct iovec iov[2];
            iov[0].iov_base = hdr;
            iov[0].iov_len = 4;
            iov[1].iov_base =
              const_cast<void *> (static_cast<const void *> (msg_->data ()));
            iov[1].iov_len = msg_size;
            const size_t total = 4U + msg_size;
            const ssize_t n = ::writev (sock->native_handle (), iov, 2);

            if (n > 0 && static_cast<size_t> (n) == total)
                return 0; //  All sent, zero copy!

            if (n > 0) {
                //  Partial write: copy remainder to send buffer.
                const size_t remain = total - static_cast<size_t> (n);
                _send_buffer_main.resize (remain);
                const size_t written = static_cast<size_t> (n);
                if (written < 4) {
                    memcpy (&_send_buffer_main[0], hdr + written,
                            4 - written);
                    memcpy (&_send_buffer_main[4 - written], msg_->data (),
                            msg_size);
                } else {
                    const size_t data_sent = written - 4;
                    memcpy (
                      &_send_buffer_main[0],
                      static_cast<const unsigned char *> (msg_->data ())
                        + data_sent,
                      msg_size - data_sent);
                }
                start_async_write ();
                return 0;
            }

            if (n < 0 && errno != EAGAIN) {
                error (connection_error);
                return -1;
            }
            //  Would block: fall through to copy path.
        }
    }

    //  Copy path: append header + data to send buffer.
    const size_t old_size = _send_buffer_main.size ();
    const size_t packet_size = 4U + msg_size;
    _send_buffer_main.resize (old_size + packet_size);
    put_uint32 (&_send_buffer_main[old_size],
                 static_cast<uint32_t> (msg_size));
    if (msg_size > 0)
        memcpy (&_send_buffer_main[old_size + 4], msg_->data (), msg_size);

    start_async_write ();
    return 0;
}

void zlink::asio_stream_engine_t::restart_deferred_read ()
{
    if (!_read_deferred)
        return;
    _read_deferred = false;
    if (_input_stopped || _terminating || _io_error)
        return;

    if (_on_helper_context && _io_context) {
        //  Helper-thread engine: the socket belongs to the helper io_context.
        //  Post to the correct thread to avoid cross-thread ASIO access.
        boost::asio::post (*_io_context, [this] () {
            if (!_input_stopped && !_terminating && !_io_error
                && !_read_pending)
                start_async_read ();
        });
    } else {
        start_async_read ();
    }
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
