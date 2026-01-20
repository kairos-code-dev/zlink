/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"
#if defined ZMQ_IOTHREAD_POLLER_USE_ASIO

#include "asio_engine.hpp"
#include "asio_poller.hpp"
#include "i_asio_transport.hpp"
#include "tcp_transport.hpp"
#include "../io_thread.hpp"
#include "../session_base.hpp"
#include "../socket_base.hpp"
#include "../null_mechanism.hpp"
#include "../plain_client.hpp"
#include "../plain_server.hpp"
#include "../config.hpp"
#include "../err.hpp"
#include "../zmp_protocol.hpp"
#include "../ip.hpp"
#include "../tcp.hpp"
#include "../likely.hpp"

#ifndef ZMQ_HAVE_WINDOWS
#include <unistd.h>
#endif

#include <cerrno>
#include <cstdlib>
#include <sstream>

// Debug logging for ASIO engine - enable with -DZMQ_ASIO_DEBUG=1
#if defined(ZMQ_ASIO_DEBUG)
#define ASIO_ENGINE_DEBUG 1
#else
#define ASIO_ENGINE_DEBUG 0
#endif

#if ASIO_ENGINE_DEBUG
#include <cstdio>
#define ENGINE_DBG(fmt, ...)                                                   \
    fprintf (stderr, "[ASIO_ENGINE] " fmt "\n", ##__VA_ARGS__)
#else
#define ENGINE_DBG(fmt, ...)
#endif

//  Draft API message property names (kept for internal use)
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
#if defined ZMQ_HAVE_SO_PEERCRED
    else if (family == PF_UNIX) {
        struct ucred cred;
        socklen_t size = sizeof (cred);
        if (!getsockopt (s_, SOL_SOCKET, SO_PEERCRED, &cred, &size)) {
            std::ostringstream buf;
            buf << ":" << cred.uid << ":" << cred.gid << ":" << cred.pid;
            peer_address += buf.str ();
        }
    }
#elif defined ZMQ_HAVE_LOCAL_PEERCRED
    else if (family == PF_UNIX) {
        struct xucred cred;
        socklen_t size = sizeof (cred);
        if (!getsockopt (s_, 0, LOCAL_PEERCRED, &cred, &size)
            && cred.cr_version == XUCRED_VERSION) {
            std::ostringstream buf;
            buf << ":" << cred.cr_uid << ":";
            if (cred.cr_ngroups > 0)
                buf << cred.cr_groups[0];
            buf << ":";
            peer_address += buf.str ();
        }
    }
#endif

    return peer_address;
}

namespace
{
bool gather_write_enabled ()
{
    static int enabled = -1;
    if (enabled == -1) {
        const char *env = std::getenv ("ZMQ_ASIO_GATHER_WRITE");
        enabled = (env && *env && *env != '0') ? 1 : 0;
    }
    return enabled == 1;
}

size_t parse_size_env (const char *name_, size_t fallback_)
{
    const char *env = std::getenv (name_);
    if (!env || !*env)
        return fallback_;
    errno = 0;
    char *end = NULL;
    const unsigned long long value = std::strtoull (env, &end, 10);
    if (errno != 0 || end == env || value == 0)
        return fallback_;
    return static_cast<size_t> (value);
}

bool asio_single_write ()
{
    static int enabled = -1;
    if (enabled == -1) {
        const char *env = std::getenv ("ZMQ_ASIO_SINGLE_WRITE");
        enabled = (env && *env && *env != '0') ? 1 : 0;
    }
    return enabled == 1;
}

size_t gather_threshold ()
{
    static size_t threshold = 0;
    if (threshold == 0) {
        threshold = parse_size_env ("ZMQ_ASIO_GATHER_THRESHOLD", 65536);
    }
    return threshold;
}
}

zmq::asio_engine_t::asio_engine_t (
  fd_t fd_,
  const options_t &options_,
  const endpoint_uri_pair_t &endpoint_uri_pair_,
  std::unique_ptr<i_asio_transport> transport_) :
    _options (options_),
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
    _input_stopped (false),
    _output_stopped (false),
    _endpoint_uri_pair (endpoint_uri_pair_),
    _has_handshake_timer (false),
    _has_ttl_timer (false),
    _has_timeout_timer (false),
    _has_heartbeat_timer (false),
    _peer_address (get_peer_address (fd_)),
    _has_handshake_stage (true),
    _io_context (NULL),
    _transport (std::move (transport_)),
    _current_timer_id (-1),
    _read_buffer (read_buffer_size),
    _total_pending_bytes (0),
    _fd (fd_),
    _plugged (false),
    _handshaking (true),
    _io_error (false),
    _read_pending (false),
    _write_pending (false),
    _async_zero_copy (false),
    _async_gather (false),
    _gather_header_size (0),
    _gather_body (NULL),
    _gather_body_size (0),
    _terminating (false),
    _read_buffer_ptr (NULL),
    _read_from_pending_pool (false),
    _session (NULL),
    _socket (NULL)
{
    ENGINE_DBG ("Constructor called, fd=%d", fd_);

    const int rc = _tx_msg.init ();
    errno_assert (rc == 0);

    if (!_transport) {
        _transport = std::unique_ptr<i_asio_transport> (
          new (std::nothrow) tcp_transport_t ());
        alloc_assert (_transport);
    }

    //  Put the socket into non-blocking mode.
    unblock_socket (_fd);
}

zmq::asio_engine_t::~asio_engine_t ()
{
    ENGINE_DBG ("Destructor called");

    zmq_assert (!_plugged);

    if (_transport) {
        _transport->close ();
    } else if (_fd != retired_fd) {
#ifdef ZMQ_HAVE_WINDOWS
        const int rc = closesocket (_fd);
        wsa_assert (rc != SOCKET_ERROR);
#else
        int rc = close (_fd);
#if defined(__FreeBSD_kernel__) || defined(__FreeBSD__)
        // FreeBSD may return ECONNRESET on close() under load but this is not
        // an error.
        if (rc == -1 && errno == ECONNRESET)
            rc = 0;
#endif
        errno_assert (rc == 0);
#endif
        _fd = retired_fd;
    }

    const int rc = _tx_msg.close ();
    errno_assert (rc == 0);

    //  Drop reference to metadata and destroy it if we are
    //  the only user.
    if (_metadata != NULL) {
        if (_metadata->drop_ref ()) {
            LIBZMQ_DELETE (_metadata);
        }
    }

    LIBZMQ_DELETE (_encoder);
    LIBZMQ_DELETE (_decoder);
    LIBZMQ_DELETE (_mechanism);

    //  Clear pending buffers (True Proactor Pattern)
    _pending_buffers.clear ();
    _total_pending_bytes = 0;

    //  Smart pointers will automatically clean up ASIO objects
    //  (_timer, _socket_handle/_stream_descriptor)
}

void zmq::asio_engine_t::plug (io_thread_t *io_thread_,
                               session_base_t *session_)
{
    ENGINE_DBG ("plug called");

    zmq_assert (!_plugged);
    _plugged = true;

    //  Connect to session object.
    zmq_assert (!_session);
    zmq_assert (session_);
    _session = session_;
    _socket = _session->get_socket ();

    //  Get reference to io_context from the io_thread's poller
    asio_poller_t *poller =
      static_cast<asio_poller_t *> (io_thread_->get_poller ());
    _io_context = &poller->get_io_context ();

    //  Allocate timer with correct io_context
    _timer = std::unique_ptr<boost::asio::steady_timer> (
      new boost::asio::steady_timer (*_io_context));

    _io_error = false;

    if (!_transport || !_transport->open (*_io_context, _fd)) {
        error (connection_error);
        return;
    }

    if (_transport->requires_handshake ()) {
        start_transport_handshake ();
        return;
    }

    plug_internal ();
}

void zmq::asio_engine_t::start_transport_handshake ()
{
    ENGINE_DBG ("start_transport_handshake");

    if (_options.handshake_ivl > 0)
        set_handshake_timer ();

    const int handshake_type =
      _endpoint_uri_pair.local_type == endpoint_type_connect ? 0 : 1;

    _transport->async_handshake (
      handshake_type,
      [this] (const boost::system::error_code &ec, std::size_t) {
          on_transport_handshake (ec);
      });
}

void zmq::asio_engine_t::on_transport_handshake (
  const boost::system::error_code &ec)
{
    ENGINE_DBG ("on_transport_handshake: ec=%s, terminating=%d",
                ec.message ().c_str (), _terminating);

    if (_terminating)
        return;

    if (!_plugged)
        return;

    if (_has_handshake_timer) {
        cancel_timer (handshake_timer_id);
        _has_handshake_timer = false;
    }

    if (ec) {
        error (connection_error);
        return;
    }

    plug_internal ();
}

void zmq::asio_engine_t::unplug ()
{
    ENGINE_DBG ("unplug called");

    zmq_assert (_plugged);
    _plugged = false;

    //  Cancel all timers.
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

    //  Cancel pending async operations by closing the transport
    if (_transport)
        _transport->close ();
    if (_timer)
        _timer->cancel ();

    //  Clear pending buffers (True Proactor Pattern)
    _pending_buffers.clear ();
    _total_pending_bytes = 0;

    _session = NULL;
}

void zmq::asio_engine_t::terminate ()
{
    ENGINE_DBG ("terminate called");

    //  Mark as terminating to prevent callbacks from processing
    _terminating = true;

    unplug ();

    //  Drain any pending async handlers while the object is still alive.
    //  The _terminating flag ensures callbacks are no-ops.
    if (_io_context && (_read_pending || _write_pending)) {
        _io_context->poll ();
    }

    delete this;
}

bool zmq::asio_engine_t::build_gather_header (const msg_t &msg_,
                                              unsigned char *buffer_,
                                              size_t buffer_size_,
                                              size_t &header_size_)
{
    LIBZMQ_UNUSED (msg_);
    LIBZMQ_UNUSED (buffer_);
    LIBZMQ_UNUSED (buffer_size_);
    header_size_ = 0;
    return false;
}

void zmq::asio_engine_t::start_async_read ()
{
    //  True Proactor Pattern: We no longer check _input_stopped here.
    //  Async reads continue even during backpressure, with data being buffered
    //  in _pending_buffers. This eliminates unnecessary recvfrom() EAGAIN calls.
    if (_read_pending || _io_error)
        return;

    ENGINE_DBG ("start_async_read: insize=%zu", _insize);

    _read_pending = true;
    _read_from_pending_pool = false;

    //  Get buffer from decoder if available
    size_t read_size;

    if (_decoder && _input_stopped) {
        read_size = _options.in_batch_size;
        if (read_size == 0)
            read_size = read_buffer_size;

        if (!_pending_buffer_pool.empty ()) {
            _pending_read_buffer = std::move (_pending_buffer_pool.back ());
            _pending_buffer_pool.pop_back ();
        }
        _pending_read_buffer.resize (read_size);
        _read_buffer_ptr = _pending_read_buffer.data ();
        _read_from_pending_pool = true;
    } else if (_decoder) {
        _decoder->get_buffer (&_read_buffer_ptr, &read_size);

        //  If we have partial data from previous read, move it to buffer start.
        //  The decoder expects data to start from the beginning of its buffer.
        if (_insize > 0 && _inpos != _read_buffer_ptr) {
            ENGINE_DBG ("start_async_read: moving %zu partial bytes to buffer start",
                        _insize);
            memmove (_read_buffer_ptr, _inpos, _insize);
            _inpos = _read_buffer_ptr;
        }

        //  Read into buffer after any existing partial data
        if (_insize > 0 && read_size > _insize) {
            _read_buffer_ptr += _insize;
            read_size -= _insize;
        }
    } else {
        //  During handshake, use internal buffer
        _read_buffer_ptr = _read_buffer.data ();
        read_size = _read_buffer.size ();
    }

    ENGINE_DBG ("start_async_read: reading up to %zu bytes", read_size);

    if (_transport) {
        _transport->async_read_some (
          _read_buffer_ptr, read_size,
          [this] (const boost::system::error_code &ec, std::size_t bytes) {
              on_read_complete (ec, bytes);
          });
    }
}

bool zmq::asio_engine_t::speculative_read ()
{
    if (_read_pending || _io_error || !_transport)
        return false;

    //  Prepare read buffer the same way as start_async_read().
    size_t read_size;

    if (_decoder) {
        _decoder->get_buffer (&_read_buffer_ptr, &read_size);

        //  If we have partial data from previous read, move it to buffer start.
        if (_insize > 0 && _inpos != _read_buffer_ptr) {
            ENGINE_DBG ("speculative_read: moving %zu partial bytes to buffer start",
                        _insize);
            memmove (_read_buffer_ptr, _inpos, _insize);
            _inpos = _read_buffer_ptr;
        }

        if (_insize > 0 && read_size > _insize) {
            _read_buffer_ptr += _insize;
            read_size -= _insize;
        }
    } else {
        _read_buffer_ptr = _read_buffer.data ();
        read_size = _read_buffer.size ();
    }

    if (read_size == 0)
        return false;

    errno = 0;
    const std::size_t bytes =
      _transport->read_some (reinterpret_cast<std::uint8_t *> (
                               _read_buffer_ptr),
                             read_size);

    if (bytes == 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return false;
        //  Treat EOF or other errors as connection error.
        error (connection_error);
        return true;
    }

    on_read_complete (boost::system::error_code (), bytes);
    return true;
}

void zmq::asio_engine_t::start_async_write ()
{
    if (_write_pending || _io_error)
        return;

    ENGINE_DBG ("start_async_write: outsize=%zu", _outsize);

    if (_outsize == 0 || _outpos == NULL) {
        //  No data prepared, try gather path first, then fallback to encoder.
        if (prepare_gather_output ())
            return;
        process_output ();
    }

    if (_outsize == 0 || _outpos == NULL) {
        _output_stopped = true;
        return;
    }

    //  Try a synchronous write first when supported (libzmq-like path).
    if (_transport->supports_speculative_write ()) {
        const std::size_t bytes =
          _transport->write_some (reinterpret_cast<const std::uint8_t *> (_outpos),
                                  _outsize);
        if (bytes == 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                error (connection_error);
                return;
            }
        } else {
            _outpos += bytes;
            _outsize -= bytes;
            if (_outsize == 0) {
                return;
            }
        }
    }

    _write_pending = true;
    _async_zero_copy = true;

    if (_transport) {
        _transport->async_write_some (
          _outpos, _outsize,
          [this] (const boost::system::error_code &ec, std::size_t bytes) {
              on_write_complete (ec, bytes);
          });
    }
}

bool zmq::asio_engine_t::prepare_gather_output ()
{
    if (!gather_write_enabled ())
        return false;
    if (!_transport || !_transport->supports_gather_write ())
        return false;
    if (_encoder == NULL || _handshaking)
        return false;

    //  Ensure encoder has no in-progress message before loading a new one.
    unsigned char *pending_buf = NULL;
    const size_t pending =
      _encoder->encode (&pending_buf, 0);
    if (pending > 0) {
        _outpos = pending_buf;
        _outsize = pending;
        return false;
    }

    if ((this->*_next_msg) (&_tx_msg) == -1) {
        if (errno == EAGAIN) {
            _output_stopped = true;
            return true;
        }
        return false;
    }

    const size_t body_size = _tx_msg.size ();
    if (body_size < gather_threshold ()) {
        _encoder->load_msg (&_tx_msg);
        return false;
    }

    size_t header_size = 0;
    if (!build_gather_header (_tx_msg, _gather_header,
                              sizeof (_gather_header), header_size)) {
        _encoder->load_msg (&_tx_msg);
        return false;
    }

    _gather_header_size = header_size;
    _gather_body = static_cast<const unsigned char *> (_tx_msg.data ());
    _gather_body_size = body_size;
    _async_gather = true;
    _write_pending = true;
    _async_zero_copy = false;
    _output_stopped = false;

    _transport->async_writev (
      _gather_header, _gather_header_size, _gather_body, _gather_body_size,
      [this] (const boost::system::error_code &ec, std::size_t bytes) {
          on_write_complete (ec, bytes);
      });
    return true;
}

void zmq::asio_engine_t::finish_gather_output ()
{
    if (!_async_gather)
        return;

    _async_gather = false;
    _gather_header_size = 0;
    _gather_body = NULL;
    _gather_body_size = 0;

    const int rc = _tx_msg.close ();
    errno_assert (rc == 0);
    const int rc_init = _tx_msg.init ();
    errno_assert (rc_init == 0);
}

void zmq::asio_engine_t::on_read_complete (const boost::system::error_code &ec,
                                           std::size_t bytes_transferred)
{
    _read_pending = false;
    ENGINE_DBG ("on_read_complete: ec=%s, bytes=%zu, terminating=%d, input_stopped=%d",
                ec.message ().c_str (), bytes_transferred, _terminating,
                _input_stopped);

    //  If terminating, just return - terminate() is draining handlers
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
        //  Connection closed by peer
        error (connection_error);
        return;
    }

    //  True Proactor Pattern: If backpressure is active, buffer the data
    //  instead of processing it. This keeps async_read always pending,
    //  eliminating unnecessary recvfrom() EAGAIN calls when backpressure clears.
    if (_input_stopped) {
        if (_read_from_pending_pool) {
            const size_t total_pending = _total_pending_bytes + _insize;

            if (total_pending + bytes_transferred > max_pending_buffer_size) {
                ENGINE_DBG ("on_read_complete: pending buffer overflow (%zu + %zu > %zu)",
                            total_pending, bytes_transferred,
                            max_pending_buffer_size);
                _read_from_pending_pool = false;
                error (connection_error);
                return;
            }

            _pending_read_buffer.resize (bytes_transferred);
            _pending_buffers.push_back (std::move (_pending_read_buffer));
            _total_pending_bytes += bytes_transferred;
            _read_from_pending_pool = false;

            ENGINE_DBG ("on_read_complete: buffered %zu bytes (total pending: %zu)",
                        bytes_transferred, _total_pending_bytes);

            start_async_read ();
            return;
        }

        //  Check buffer size limit to prevent memory exhaustion
        //  Bug fix: Include _insize (current partial data) in total calculation
        //  Bug fix: Use _total_pending_bytes for O(1) instead of O(n) iteration
        const size_t total_pending = _total_pending_bytes + _insize;

        if (total_pending + bytes_transferred > max_pending_buffer_size) {
            //  Buffer overflow - close connection to prevent memory exhaustion
            ENGINE_DBG ("on_read_complete: pending buffer overflow (%zu + %zu > %zu)",
                        total_pending, bytes_transferred, max_pending_buffer_size);
            error (connection_error);
            return;
        }

        //  Store data in pending buffer for later processing
        std::vector<unsigned char> buffer (bytes_transferred);
        std::memcpy (buffer.data (), _read_buffer_ptr, bytes_transferred);
        _pending_buffers.push_back (std::move (buffer));
        _total_pending_bytes += bytes_transferred;

        ENGINE_DBG ("on_read_complete: buffered %zu bytes (total pending: %zu)",
                    bytes_transferred, _total_pending_bytes);

        //  CRITICAL: Continue async read even during backpressure.
        //  This is the key to True Proactor pattern - we always have a
        //  pending read, so when data arrives it's immediately buffered.
        start_async_read ();
        return;
    }

    //  Handle buffer pointers based on whether we have partial data
    if (_decoder && _insize > 0) {
        //  We have partial data from previous read.
        //  start_async_read() moved partial data to buffer start and set _inpos there.
        //  New data was read right after the partial data.
        const size_t partial_size = _insize;
        _insize = partial_size + bytes_transferred;
        ENGINE_DBG ("on_read_complete: total %zu bytes (partial=%zu + new=%zu)",
                    _insize, partial_size, bytes_transferred);
        //  Note: Do NOT call resize_buffer() here - it interferes with decoder's
        //  internal state management. The decoder tracks its own buffer state.
    } else {
        //  Fresh read - set buffer pointers
        _inpos = _read_buffer_ptr;
        _insize = bytes_transferred;
        //  Note: Do NOT call resize_buffer() here - let process_input() handle
        //  decoder buffer management when data is copied to decoder buffer.
    }
    _input_in_decoder_buffer = (_decoder != NULL);

    //  Process received data
    if (!process_input ()) {
        //  If handshaking and need more data (EAGAIN), continue I/O
        if (_handshaking && errno == EAGAIN) {
            //  Send any output that was built during greeting processing
            if (_outsize > 0)
                start_async_write ();
            start_async_read ();
        }
        return;
    }

    //  True Proactor Pattern: Always continue reading.
    //  If backpressure was triggered during process_input(), data will be
    //  buffered in the next on_read_complete() call.
    start_async_read ();
}

void zmq::asio_engine_t::on_write_complete (const boost::system::error_code &ec,
                                            std::size_t bytes_transferred)
{
    _write_pending = false;
    ENGINE_DBG ("on_write_complete: ec=%s, bytes=%zu, terminating=%d",
                ec.message ().c_str (), bytes_transferred, _terminating);

    //  If terminating, just return - terminate() is draining handlers
    if (_terminating)
        return;

    if (!_plugged)
        return;

    if (ec) {
        if (ec == boost::asio::error::operation_aborted)
            return;
        //  IO error - stop writing but continue reading to detect connection close
        _io_error = true;
        finish_gather_output ();
        return;
    }

    if (_async_gather)
        finish_gather_output ();

    if (_async_zero_copy) {
        if (bytes_transferred == 0) {
            error (connection_error);
            return;
        }

        _outpos += bytes_transferred;
        _outsize -= bytes_transferred;

        if (_outsize > 0) {
            _write_pending = true;
            if (_transport) {
                _transport->async_write_some (
                  _outpos, _outsize,
                  [this] (const boost::system::error_code &wec,
                          std::size_t bytes) {
                      on_write_complete (wec, bytes);
                  });
            }
            return;
        }

        _async_zero_copy = false;
    }

    //  If still handshaking and there's nothing more to write, just return
    if (_handshaking && _outsize == 0)
        return;

    //  Continue writing if more data available
    if (!_output_stopped)
        start_async_write ();
}

bool zmq::asio_engine_t::process_input ()
{
    ENGINE_DBG ("process_input: handshaking=%d, insize=%zu", _handshaking,
                _insize);

    //  If still handshaking, receive and process the greeting message.
    if (unlikely (_handshaking)) {
        if (handshake ()) {
            //  Handshaking was successful.
            //  Switch into the normal message flow.
            _handshaking = false;

            if (_mechanism == NULL && _has_handshake_stage) {
                _session->engine_ready ();

                if (_has_handshake_timer) {
                    cancel_timer (handshake_timer_id);
                    _has_handshake_timer = false;
                }
            }

            //  After greeting exchange completes, trigger output to send
            //  mechanism handshake (e.g., READY command)
            if (_mechanism != NULL) {
                ENGINE_DBG ("process_input: greeting done, triggering write");
                start_async_write ();
            }
        } else
            return false;
    }

    zmq_assert (_decoder);

    //  If there has been an I/O error, stop.
    if (_input_stopped) {
        _io_error = true;
        return true;
    }

    const bool input_in_decoder_buffer = _input_in_decoder_buffer;

    ENGINE_DBG (
      "process_input: after handshake, insize=%zu, input_in_decoder_buffer=%d",
      _insize, input_in_decoder_buffer);

    int rc = 0;
    size_t processed = 0;

    while (_insize > 0) {
        unsigned char *decode_buf;
        size_t decode_size;

        decode_buf = _inpos;
        decode_size = _insize;

        if (!input_in_decoder_buffer) {
            //  Data came from an external buffer (handshake/greeting); copy
            //  into decoder buffer to preserve decoder buffer invariants.
            _decoder->get_buffer (&decode_buf, &decode_size);
            decode_size = std::min (_insize, decode_size);
            memcpy (decode_buf, _inpos, decode_size);
            _decoder->resize_buffer (decode_size);
            ENGINE_DBG ("process_input: copied %zu bytes from external input "
                        "buffer to decoder buffer",
                        decode_size);
        }

        ENGINE_DBG ("process_input: decode loop decode_size=%zu", decode_size);
        rc = _decoder->decode (decode_buf, decode_size, processed);
        ENGINE_DBG ("process_input: decode returned rc=%d, processed=%zu", rc,
                    processed);
        zmq_assert (processed <= decode_size);
        _inpos += processed;
        _insize -= processed;

        if (rc == 0 || rc == -1)
            break;
        rc = (this->*_process_msg) (_decoder->msg ());
        ENGINE_DBG ("process_input: process_msg returned rc=%d", rc);
        if (rc == -1)
            break;
    }

    //  Tear down the connection if we have failed to decode input data
    //  or the session has rejected the message.
    if (rc == -1) {
        if (errno != EAGAIN) {
            error (protocol_error);
            return false;
        }
        _input_stopped = true;
    }

    _session->flush ();
    return true;
}

bool zmq::asio_engine_t::prepare_output_buffer ()
{
    ENGINE_DBG ("prepare_output_buffer: outsize=%zu", _outsize);

    //  If we already have data prepared, return true.
    if (_outsize > 0)
        return true;

    //  Even when we stop as soon as there is no data to send,
    //  there may be a pending async_write.
    if (unlikely (_encoder == NULL)) {
        zmq_assert (_handshaking);
        return false;
    }

    _outpos = NULL;
    _outsize = _encoder->encode (&_outpos, 0);

    while (_outsize < static_cast<size_t> (_options.out_batch_size)) {
        if ((this->*_next_msg) (&_tx_msg) == -1) {
            if (errno == ECONNRESET)
                return false;
            else
                break;
        }
        _encoder->load_msg (&_tx_msg);
        unsigned char *bufptr = _outpos + _outsize;
        const size_t n =
          _encoder->encode (&bufptr, _options.out_batch_size - _outsize);
        zmq_assert (n > 0);
        if (_outpos == NULL)
            _outpos = bufptr;
        _outsize += n;
    }

    ENGINE_DBG ("prepare_output_buffer: prepared %zu bytes", _outsize);
    return _outsize > 0;
}

void zmq::asio_engine_t::speculative_write ()
{
    ENGINE_DBG ("speculative_write: write_pending=%d, output_stopped=%d",
                _write_pending, _output_stopped);

    //  Guard: If async write is already in progress, skip.
    //  This ensures single write-in-flight invariant.
    if (_write_pending)
        return;

    //  Guard: Don't write during I/O errors
    if (_io_error)
        return;

    //  Try gather path first for large messages.
    if (prepare_gather_output ())
        return;

    //  Prepare output buffer from encoder
    if (!prepare_output_buffer ()) {
        _output_stopped = true;
        ENGINE_DBG ("speculative_write: no data to send, output_stopped=true");
        return;
    }

    if (!_transport->supports_speculative_write ()) {
        ENGINE_DBG ("speculative_write: transport prefers async");
        start_async_write ();
        return;
    }

    //  Attempt synchronous write using transport's write_some()
    zmq_assert (_transport);
    const std::size_t bytes =
      _transport->write_some (reinterpret_cast<const std::uint8_t *> (_outpos),
                              _outsize);

    ENGINE_DBG ("speculative_write: write_some returned %zu, errno=%d", bytes,
                errno);

    if (bytes == 0) {
        //  Check if it's would_block (retry later) or actual error
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            //  would_block - fall back to async write.
            //  Copy data to write buffer for async operation (buffer lifetime).
            ENGINE_DBG ("speculative_write: would_block, falling back to async");
            start_async_write ();
            return;
        }
        //  Actual error
        ENGINE_DBG ("speculative_write: error, errno=%d", errno);
        error (connection_error);
        return;
    }

    //  Partial or complete write succeeded
    _outpos += bytes;
    _outsize -= bytes;

    ENGINE_DBG ("speculative_write: wrote %zu bytes, remaining=%zu", bytes,
                _outsize);

    if (_outsize > 0) {
        //  Partial write - remaining data needs async path.
        //  Copy remaining data to write buffer for async operation.
        ENGINE_DBG ("speculative_write: partial write, async for remaining %zu",
                    _outsize);
        start_async_write ();
    } else {
        //  Complete write succeeded.
        //  Try to get more data and continue speculative writing.
        _output_stopped = false;

        //  During handshake, don't loop - let handshake control flow proceed.
        if (_handshaking)
            return;

        if (asio_single_write ()) {
            start_async_write ();
            return;
        }

        //  Try to prepare and write more data speculatively.
        //  This loop enables efficient burst writes without async overhead.
        while (prepare_output_buffer ()) {
            const std::size_t more_bytes = _transport->write_some (
              reinterpret_cast<const std::uint8_t *> (_outpos), _outsize);

            ENGINE_DBG ("speculative_write loop: wrote %zu, errno=%d",
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
        ENGINE_DBG ("speculative_write: all data sent, output_stopped=true");
    }
}

void zmq::asio_engine_t::process_output ()
{
    ENGINE_DBG ("process_output: outsize=%zu", _outsize);

    //  If write buffer is empty, try to read new data from the encoder.
    if (_outsize == 0) {
        //  Even when we stop as soon as there is no data to send,
        //  there may be a pending async_write.
        if (unlikely (_encoder == NULL)) {
            zmq_assert (_handshaking);
            return;
        }

        _outpos = NULL;
        _outsize = _encoder->encode (&_outpos, 0);

        while (_outsize < static_cast<size_t> (_options.out_batch_size)) {
            if ((this->*_next_msg) (&_tx_msg) == -1) {
                if (errno == ECONNRESET)
                    return;
                else
                    break;
            }
            _encoder->load_msg (&_tx_msg);
            unsigned char *bufptr = _outpos + _outsize;
            const size_t n =
              _encoder->encode (&bufptr, _options.out_batch_size - _outsize);
            zmq_assert (n > 0);
            if (_outpos == NULL)
                _outpos = bufptr;
            _outsize += n;
        }

        //  If there is no data to send, mark output as stopped.
        if (_outsize == 0) {
            _output_stopped = true;
            return;
        }
    }
}

void zmq::asio_engine_t::restart_output ()
{
    ENGINE_DBG ("restart_output: io_error=%d, output_stopped=%d, write_pending=%d",
                _io_error, _output_stopped, _write_pending);

    if (unlikely (_io_error))
        return;

    if (likely (_output_stopped)) {
        _output_stopped = false;
    }

    //  Use speculative write for immediate transmission.
    //  This tries synchronous write first, falling back to async if needed.
    speculative_write ();
}

bool zmq::asio_engine_t::restart_input ()
{
    return restart_input_internal ();
}

bool zmq::asio_engine_t::restart_input_internal ()
{
    zmq_assert (_input_stopped);
    zmq_assert (_session != NULL);
    zmq_assert (_decoder != NULL);

    ENGINE_DBG ("restart_input: pending_buffers=%zu, insize=%zu",
                _pending_buffers.size (), _insize);

    //  First, try to process the previously failed message
    int rc = (this->*_process_msg) (_decoder->msg ());
    if (rc == -1) {
        if (errno == EAGAIN) {
            //  Still backpressure - stay stopped
            _session->flush ();
            ENGINE_DBG ("restart_input: still backpressure on pending msg");
        } else {
            error (protocol_error);
            return false;
        }
        return true;
    }

    //  Process any remaining data in the current input buffer
    while (_insize > 0) {
        size_t processed = 0;
        unsigned char *decode_buf = _inpos;
        size_t decode_size = _insize;

        if (!_input_in_decoder_buffer) {
            _decoder->get_buffer (&decode_buf, &decode_size);
            decode_size = std::min (_insize, decode_size);
            memcpy (decode_buf, _inpos, decode_size);
            _decoder->resize_buffer (decode_size);
        }

        rc = _decoder->decode (decode_buf, decode_size, processed);
        zmq_assert (processed <= _insize);
        _inpos += processed;
        _insize -= processed;
        if (rc == 0 || rc == -1)
            break;
        rc = (this->*_process_msg) (_decoder->msg ());
        if (rc == -1)
            break;
    }

    //  Check for backpressure or errors after processing current buffer
    if (rc == -1 && errno == EAGAIN) {
        _session->flush ();
        ENGINE_DBG ("restart_input: backpressure after current buffer");
        return true;
    } else if (_io_error) {
        error (connection_error);
        return false;
    } else if (rc == -1) {
        error (protocol_error);
        return false;
    }

    //  Process any buffered data from _pending_buffers (if present).
    while (!_pending_buffers.empty ()) {
        std::vector<unsigned char> &buffer = _pending_buffers.front ();
        const size_t original_buffer_size = buffer.size ();

        ENGINE_DBG ("restart_input: processing pending buffer of %zu bytes",
                    buffer.size ());

        //  Copy to decoder buffer and process
        unsigned char *decode_buf;
        size_t decode_size;
        _decoder->get_buffer (&decode_buf, &decode_size);
        decode_size = std::min (buffer.size (), decode_size);
        memcpy (decode_buf, buffer.data (), decode_size);
        _decoder->resize_buffer (decode_size);

        size_t buffer_pos = 0;
        size_t buffer_remaining = buffer.size ();

        while (buffer_remaining > 0) {
            size_t processed = 0;

            //  If we've consumed the initial copy, get more buffer space
            if (buffer_pos > 0) {
                _decoder->get_buffer (&decode_buf, &decode_size);
                const size_t to_copy = std::min (buffer_remaining, decode_size);
                memcpy (decode_buf, buffer.data () + buffer_pos, to_copy);
                _decoder->resize_buffer (to_copy);
                decode_size = to_copy;
            } else {
                //  First iteration uses already copied data
                decode_size = std::min (buffer_remaining, decode_size);
            }

            rc = _decoder->decode (decode_buf, decode_size, processed);
            buffer_pos += processed;
            buffer_remaining -= processed;

            if (rc == 0 || rc == -1)
                break;

            rc = (this->*_process_msg) (_decoder->msg ());
            if (rc == -1)
                break;
        }

        //  If backpressure occurred, keep remaining data in buffer
        if (rc == -1 && errno == EAGAIN) {
            if (buffer_remaining > 0 && buffer_pos > 0) {
                //  Trim processed data from buffer and update tracking
                const size_t bytes_consumed = buffer_pos;
                buffer.erase (buffer.begin (),
                              buffer.begin () + static_cast<long> (buffer_pos));
                _total_pending_bytes -= bytes_consumed;
                ENGINE_DBG ("restart_input: partial pending buffer, %zu bytes "
                            "remaining",
                            buffer.size ());
            }
            _session->flush ();
            ENGINE_DBG ("restart_input: backpressure during pending buffer");
            return true;
        } else if (_io_error) {
            error (connection_error);
            return false;
        } else if (rc == -1) {
            error (protocol_error);
            return false;
        }

        //  Bug fix: rc == 0 means decoder needs more data but buffer_remaining
        //  may still be > 0. In this case, we must preserve the remaining data.
        if (rc == 0 && buffer_remaining > 0) {
            if (buffer_pos > 0) {
                //  Trim processed data from buffer and update tracking
                const size_t bytes_consumed = buffer_pos;
                buffer.erase (buffer.begin (),
                              buffer.begin () + static_cast<long> (buffer_pos));
                _total_pending_bytes -= bytes_consumed;
                ENGINE_DBG ("restart_input: decoder needs more data, %zu bytes "
                            "remaining in buffer",
                            buffer.size ());
            }
            //  Don't pop_front - keep remaining data for next iteration
            //  when more data arrives from network
            break;
        }

        //  Buffer fully processed, remove it and update tracking
        _total_pending_bytes -= original_buffer_size;
        if (_pending_buffer_pool.size () < pending_buffer_pool_max) {
            buffer.clear ();
            _pending_buffer_pool.push_back (std::move (buffer));
        }
        _pending_buffers.pop_front ();
    }

    //  All pending data processed successfully - NOW safe to clear flag
    _input_stopped = false;
    _session->flush ();

    ENGINE_DBG ("restart_input: completed, input resumed");

    //  CRITICAL FIX: Double-check for race condition AFTER flush()
    //  Race scenario (discovered by Gemini):
    //  - We cleared _input_stopped = false above
    //  - _session->flush() may trigger pipe events that lead to on_read_complete()
    //  - on_read_complete() sees _input_stopped = false, so it processes data directly
    //  - BUT if it hits backpressure, it won't buffer (since _input_stopped = false)
    //  Solution: Check if any buffers accumulated and re-enter stopped mode
    if (!_pending_buffers.empty()) {
        ENGINE_DBG ("restart_input: race detected AFTER flush, %zu buffers accumulated, re-entering stopped mode",
                    _pending_buffers.size());
        //  Re-enter stopped mode and recursively drain.
        //  Call restart_input_internal() directly to keep state local.
        _input_stopped = true;
        return restart_input_internal();
    }

    //  Speculative read (libzmq pattern): drain immediately available data
    //  before re-arming async read. This avoids missed wakeups on IPC.
    if (speculative_read ())
        return true;

    start_async_read ();
    return true;
}

int zmq::asio_engine_t::next_handshake_command (msg_t *msg_)
{
    zmq_assert (_mechanism != NULL);

    if (_mechanism->status () == mechanism_t::ready) {
        mechanism_ready ();
        return pull_and_encode (msg_);
    }
    if (_mechanism->status () == mechanism_t::error) {
        errno = EPROTO;
        return -1;
    }
    const int rc = _mechanism->next_handshake_command (msg_);

    if (rc == 0)
        msg_->set_flags (msg_t::command);

    return rc;
}

int zmq::asio_engine_t::process_handshake_command (msg_t *msg_)
{
    zmq_assert (_mechanism != NULL);
    const int rc = _mechanism->process_handshake_command (msg_);
    if (rc == 0) {
        if (_mechanism->status () == mechanism_t::ready)
            mechanism_ready ();
        else if (_mechanism->status () == mechanism_t::error) {
            errno = EPROTO;
            return -1;
        }
        if (_output_stopped)
            restart_output ();
    }

    return rc;
}

void zmq::asio_engine_t::zap_msg_available ()
{
    zmq_assert (_mechanism != NULL);

    const int rc = _mechanism->zap_msg_available ();
    if (rc == -1) {
        error (protocol_error);
        return;
    }
    if (_input_stopped)
        if (!restart_input ())
            return;
    if (_output_stopped)
        restart_output ();
}

const zmq::endpoint_uri_pair_t &zmq::asio_engine_t::get_endpoint () const
{
    return _endpoint_uri_pair;
}

void zmq::asio_engine_t::mechanism_ready ()
{
    if (_options.heartbeat_interval > 0 && !_has_heartbeat_timer) {
        add_timer (_options.heartbeat_interval, heartbeat_ivl_timer_id);
        _has_heartbeat_timer = true;
    }

    if (_has_handshake_stage)
        _session->engine_ready ();

    bool flush_session = false;

    if (_options.recv_routing_id) {
        msg_t routing_id;
        _mechanism->peer_routing_id (&routing_id);
        const int rc = _session->push_msg (&routing_id);
        if (rc == -1 && errno == EAGAIN) {
            return;
        }
        errno_assert (rc == 0);
        flush_session = true;
    }

    if (flush_session)
        _session->flush ();

    _next_msg = &asio_engine_t::pull_and_encode;
    _process_msg = &asio_engine_t::write_credential;

    //  Compile metadata.
    properties_t properties;
    init_properties (properties);

    //  Add ZAP properties.
    const properties_t &zap_properties = _mechanism->get_zap_properties ();
    properties.insert (zap_properties.begin (), zap_properties.end ());

    //  Add ZMTP properties.
    const properties_t &zmtp_properties = _mechanism->get_zmtp_properties ();
    properties.insert (zmtp_properties.begin (), zmtp_properties.end ());

    zmq_assert (_metadata == NULL);
    if (!properties.empty ()) {
        _metadata = new (std::nothrow) metadata_t (properties);
        alloc_assert (_metadata);
    }

    if (_has_handshake_timer) {
        cancel_timer (handshake_timer_id);
        _has_handshake_timer = false;
    }

    _socket->event_handshake_succeeded (_endpoint_uri_pair, 0);
}

int zmq::asio_engine_t::write_credential (msg_t *msg_)
{
    zmq_assert (_mechanism != NULL);
    zmq_assert (_session != NULL);

    const blob_t &credential = _mechanism->get_user_id ();
    if (credential.size () > 0) {
        msg_t msg;
        int rc = msg.init_size (credential.size ());
        zmq_assert (rc == 0);
        memcpy (msg.data (), credential.data (), credential.size ());
        msg.set_flags (msg_t::credential);
        rc = _session->push_msg (&msg);
        if (rc == -1) {
            rc = msg.close ();
            errno_assert (rc == 0);
            return -1;
        }
    }
    _process_msg = &asio_engine_t::decode_and_push;
    return decode_and_push (msg_);
}

int zmq::asio_engine_t::pull_and_encode (msg_t *msg_)
{
    zmq_assert (_mechanism != NULL);

    if (_session->pull_msg (msg_) == -1)
        return -1;
    if (_mechanism->encode (msg_) == -1)
        return -1;
    return 0;
}

int zmq::asio_engine_t::decode_and_push (msg_t *msg_)
{
    zmq_assert (_mechanism != NULL);

    if (_mechanism->decode (msg_) == -1)
        return -1;

    if (_has_timeout_timer) {
        _has_timeout_timer = false;
        cancel_timer (heartbeat_timeout_timer_id);
    }

    if (_has_ttl_timer) {
        _has_ttl_timer = false;
        cancel_timer (heartbeat_ttl_timer_id);
    }

    if (msg_->flags () & msg_t::command) {
        process_command_message (msg_);
    }

    if (_metadata)
        msg_->set_metadata (_metadata);
    if (_session->push_msg (msg_) == -1) {
        if (errno == EAGAIN)
            _process_msg = &asio_engine_t::push_one_then_decode_and_push;
        return -1;
    }
    return 0;
}

int zmq::asio_engine_t::push_one_then_decode_and_push (msg_t *msg_)
{
    const int rc = _session->push_msg (msg_);
    if (rc == 0)
        _process_msg = &asio_engine_t::decode_and_push;
    return rc;
}

int zmq::asio_engine_t::pull_msg_from_session (msg_t *msg_)
{
    return _session->pull_msg (msg_);
}

int zmq::asio_engine_t::push_msg_to_session (msg_t *msg_)
{
    return _session->push_msg (msg_);
}

void zmq::asio_engine_t::error (error_reason_t reason_)
{
    ENGINE_DBG ("error: reason=%d", static_cast<int> (reason_));

    //  Mark as terminating to prevent callbacks from processing
    _terminating = true;

    zmq_assert (_session);

    // protocol errors have been signaled already at the point where they occurred
    if (reason_ != protocol_error && !zmp_protocol_enabled ()
        && (_mechanism == NULL
            || _mechanism->status () == mechanism_t::handshaking)) {
        const int err = errno;
        _socket->event_handshake_failed_no_detail (_endpoint_uri_pair, err);
    }

    _socket->event_disconnected (_endpoint_uri_pair, _fd);
    _session->flush ();
    _session->engine_error (
      !_handshaking
        && (_mechanism == NULL
            || _mechanism->status () != mechanism_t::handshaking),
      reason_);
    unplug ();

    //  Drain any pending async handlers while the object is still alive.
    //  The _terminating flag ensures callbacks are no-ops.
    if (_io_context && (_read_pending || _write_pending)) {
        _io_context->poll ();
    }

    delete this;
}

void zmq::asio_engine_t::set_handshake_timer ()
{
    zmq_assert (!_has_handshake_timer);

    if (_options.handshake_ivl > 0) {
        add_timer (_options.handshake_ivl, handshake_timer_id);
        _has_handshake_timer = true;
    }
}

void zmq::asio_engine_t::cancel_handshake_timer ()
{
    if (_has_handshake_timer) {
        cancel_timer (handshake_timer_id);
        _has_handshake_timer = false;
    }
}

void zmq::asio_engine_t::add_timer (int timeout_, int id_)
{
    ENGINE_DBG ("add_timer: timeout=%d, id=%d", timeout_, id_);

    zmq_assert (_timer);
    _current_timer_id = id_;
    _timer->expires_after (std::chrono::milliseconds (timeout_));
    _timer->async_wait (
      [this, id_] (const boost::system::error_code &ec) {
          on_timer (id_, ec);
      });
}

void zmq::asio_engine_t::cancel_timer (int id_)
{
    ENGINE_DBG ("cancel_timer: id=%d", id_);

    if (_current_timer_id == id_ && _timer) {
        _timer->cancel ();
        _current_timer_id = -1;
    }
}

void zmq::asio_engine_t::on_timer (int id_, const boost::system::error_code &ec)
{
    ENGINE_DBG ("on_timer: id=%d, ec=%s, terminating=%d", id_,
                ec.message ().c_str (), _terminating);

    if (ec == boost::asio::error::operation_aborted)
        return;

    //  If terminating, just return - terminate() is draining handlers
    if (_terminating)
        return;

    if (!_plugged)
        return;

    if (id_ == handshake_timer_id) {
        _has_handshake_timer = false;
        //  handshake timer expired before handshake completed, so engine fail
        error (timeout_error);
    } else if (id_ == heartbeat_ivl_timer_id) {
        _next_msg = &asio_engine_t::produce_ping_message;
        restart_output ();
        add_timer (_options.heartbeat_interval, heartbeat_ivl_timer_id);
    } else if (id_ == heartbeat_ttl_timer_id) {
        _has_ttl_timer = false;
        error (timeout_error);
    } else if (id_ == heartbeat_timeout_timer_id) {
        _has_timeout_timer = false;
        error (timeout_error);
    }
}

bool zmq::asio_engine_t::init_properties (properties_t &properties_)
{
    if (_peer_address.empty ())
        return false;
    properties_.ZMQ_MAP_INSERT_OR_EMPLACE (
      std::string (ZMQ_MSG_PROPERTY_PEER_ADDRESS), _peer_address);

    //  Private property to support deprecated SRCFD
    std::ostringstream stream;
    stream << static_cast<int> (_fd);
    std::string fd_string = stream.str ();
    properties_.ZMQ_MAP_INSERT_OR_EMPLACE (std::string ("__fd"),
                                           ZMQ_MOVE (fd_string));
    return true;
}

#endif  // ZMQ_IOTHREAD_POLLER_USE_ASIO
