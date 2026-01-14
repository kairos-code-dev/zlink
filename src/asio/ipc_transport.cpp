/* SPDX-License-Identifier: MPL-2.0 */

#include "../precompiled.hpp"
#if defined ZMQ_IOTHREAD_POLLER_USE_ASIO && defined ZMQ_HAVE_IPC

#include "ipc_transport.hpp"

#include "../err.hpp"
#include <atomic>
#include <cstdlib>

namespace zmq
{
namespace
{
std::atomic<uint64_t> ipc_async_read_calls (0);
std::atomic<uint64_t> ipc_async_read_bytes (0);
std::atomic<uint64_t> ipc_async_read_errors (0);
std::atomic<uint64_t> ipc_async_write_calls (0);
std::atomic<uint64_t> ipc_async_write_bytes (0);
std::atomic<uint64_t> ipc_async_write_errors (0);
std::atomic<uint64_t> ipc_write_some_calls (0);
std::atomic<uint64_t> ipc_write_some_bytes (0);
std::atomic<uint64_t> ipc_write_some_eagain (0);
std::atomic<uint64_t> ipc_write_some_errors (0);
std::atomic<bool> ipc_stats_registered (false);

bool ipc_stats_enabled ()
{
    static int enabled = -1;
    if (enabled == -1) {
        const char *env = std::getenv ("ZMQ_ASIO_IPC_STATS");
        enabled = (env && *env && *env != '0') ? 1 : 0;
    }
    return enabled == 1;
}

bool ipc_force_async ()
{
    static int enabled = -1;
    if (enabled == -1) {
        const char *env = std::getenv ("ZMQ_ASIO_IPC_FORCE_ASYNC");
        enabled = (env && *env && *env != '0') ? 1 : 0;
    }
    return enabled == 1;
}

bool ipc_allow_sync_write ()
{
    static int enabled = -1;
    if (enabled == -1) {
        const char *env = std::getenv ("ZMQ_ASIO_IPC_SYNC_WRITE");
        enabled = (env && *env && *env != '0') ? 1 : 0;
    }
    return enabled == 1;
}

void ipc_stats_dump ()
{
    std::fprintf (stderr,
                  "[ASIO_IPC_STATS] async_read calls=%llu bytes=%llu errors=%llu\n"
                  "[ASIO_IPC_STATS] async_write calls=%llu bytes=%llu errors=%llu\n"
                  "[ASIO_IPC_STATS] write_some calls=%llu bytes=%llu eagain=%llu errors=%llu\n",
                  static_cast<unsigned long long> (ipc_async_read_calls.load ()),
                  static_cast<unsigned long long> (ipc_async_read_bytes.load ()),
                  static_cast<unsigned long long> (ipc_async_read_errors.load ()),
                  static_cast<unsigned long long> (ipc_async_write_calls.load ()),
                  static_cast<unsigned long long> (ipc_async_write_bytes.load ()),
                  static_cast<unsigned long long> (ipc_async_write_errors.load ()),
                  static_cast<unsigned long long> (ipc_write_some_calls.load ()),
                  static_cast<unsigned long long> (ipc_write_some_bytes.load ()),
                  static_cast<unsigned long long> (ipc_write_some_eagain.load ()),
                  static_cast<unsigned long long> (ipc_write_some_errors.load ()));
}

void ipc_stats_maybe_register ()
{
    if (!ipc_stats_enabled ())
        return;
    bool expected = false;
    if (ipc_stats_registered.compare_exchange_strong (expected, true)) {
        std::atexit (ipc_stats_dump);
    }
}
}

ipc_transport_t::ipc_transport_t ()
{
}

ipc_transport_t::~ipc_transport_t ()
{
    close ();
}

bool ipc_transport_t::open (boost::asio::io_context &io_context, fd_t fd)
{
    try {
#ifndef ZMQ_HAVE_WINDOWS
        _stream_descriptor =
          std::unique_ptr<boost::asio::posix::stream_descriptor> (
            new boost::asio::posix::stream_descriptor (io_context, fd));
#else
        _socket = std::unique_ptr<boost::asio::local::stream_protocol::socket> (
          new boost::asio::local::stream_protocol::socket (io_context));
#endif
    } catch (const std::bad_alloc &) {
        return false;
    }

#ifdef ZMQ_HAVE_WINDOWS
    boost::asio::local::stream_protocol protocol;
    boost::system::error_code ec;
    _socket->assign (protocol, fd, ec);
    if (ec) {
        const int tmp_errno = ec.value ();
        errno = tmp_errno;
        _socket.reset ();
        return false;
    }
#endif

    return true;
}

bool ipc_transport_t::is_open () const
{
#ifndef ZMQ_HAVE_WINDOWS
    return _stream_descriptor && _stream_descriptor->is_open ();
#else
    return _socket && _socket->is_open ();
#endif
}

void ipc_transport_t::close ()
{
#ifndef ZMQ_HAVE_WINDOWS
    if (_stream_descriptor) {
        boost::system::error_code ec;
        _stream_descriptor->close (ec);
        _stream_descriptor.reset ();
    }
#else
    if (_socket) {
        boost::system::error_code ec;
        _socket->close (ec);
        _socket.reset ();
    }
#endif
}

void ipc_transport_t::async_read_some (
  unsigned char *buffer,
  std::size_t buffer_size,
  completion_handler_t handler)
{
    if (ipc_stats_enabled ()) {
        ipc_stats_maybe_register ();
        ++ipc_async_read_calls;
    }

#ifndef ZMQ_HAVE_WINDOWS
    if (_stream_descriptor) {
        if (ipc_stats_enabled ()) {
            _stream_descriptor->async_read_some (
              boost::asio::buffer (buffer, buffer_size),
              [handler](const boost::system::error_code &ec,
                        std::size_t bytes) {
                  if (ec)
                      ++ipc_async_read_errors;
                  else
                      ipc_async_read_bytes += bytes;
                  if (handler)
                      handler (ec, bytes);
              });
        } else {
            _stream_descriptor->async_read_some (
              boost::asio::buffer (buffer, buffer_size), handler);
        }
    } else if (handler) {
        handler (boost::asio::error::bad_descriptor, 0);
    }
#else
    if (_socket) {
        if (ipc_stats_enabled ()) {
            _socket->async_read_some (
              boost::asio::buffer (buffer, buffer_size),
              [handler](const boost::system::error_code &ec,
                        std::size_t bytes) {
                  if (ec)
                      ++ipc_async_read_errors;
                  else
                      ipc_async_read_bytes += bytes;
                  if (handler)
                      handler (ec, bytes);
              });
        } else {
            _socket->async_read_some (
              boost::asio::buffer (buffer, buffer_size), handler);
        }
    } else if (handler) {
        handler (boost::asio::error::bad_descriptor, 0);
    }
#endif
}

void ipc_transport_t::async_write_some (
  const unsigned char *buffer,
  std::size_t buffer_size,
  completion_handler_t handler)
{
    if (ipc_stats_enabled ()) {
        ipc_stats_maybe_register ();
        ++ipc_async_write_calls;
    }

#ifndef ZMQ_HAVE_WINDOWS
    if (_stream_descriptor) {
        if (ipc_stats_enabled ()) {
            boost::asio::async_write (
              *_stream_descriptor, boost::asio::buffer (buffer, buffer_size),
              [handler](const boost::system::error_code &ec,
                        std::size_t bytes) {
                  if (ec)
                      ++ipc_async_write_errors;
                  else
                      ipc_async_write_bytes += bytes;
                  if (handler)
                      handler (ec, bytes);
              });
        } else {
            boost::asio::async_write (
              *_stream_descriptor, boost::asio::buffer (buffer, buffer_size),
              handler);
        }
    } else if (handler) {
        handler (boost::asio::error::bad_descriptor, 0);
    }
#else
    if (_socket) {
        if (ipc_stats_enabled ()) {
            boost::asio::async_write (
              *_socket, boost::asio::buffer (buffer, buffer_size),
              [handler](const boost::system::error_code &ec,
                        std::size_t bytes) {
                  if (ec)
                      ++ipc_async_write_errors;
                  else
                      ipc_async_write_bytes += bytes;
                  if (handler)
                      handler (ec, bytes);
              });
        } else {
            boost::asio::async_write (
              *_socket, boost::asio::buffer (buffer, buffer_size), handler);
        }
    } else if (handler) {
        handler (boost::asio::error::bad_descriptor, 0);
    }
#endif
}

std::size_t ipc_transport_t::write_some (const std::uint8_t *data,
                                         std::size_t len)
{
    if (len == 0) {
        return 0;
    }

#ifndef ZMQ_HAVE_WINDOWS
    if (!_stream_descriptor || !_stream_descriptor->is_open ()) {
        errno = EBADF;
        return 0;
    }
#else
    if (!_socket || !_socket->is_open ()) {
        errno = EBADF;
        return 0;
    }
#endif

    if (ipc_force_async () || !ipc_allow_sync_write ()) {
        errno = EAGAIN;
        if (ipc_stats_enabled ()) {
            ipc_stats_maybe_register ();
            ++ipc_write_some_calls;
            ++ipc_write_some_eagain;
        }
        return 0;
    }

    if (ipc_stats_enabled ()) {
        ipc_stats_maybe_register ();
        ++ipc_write_some_calls;
    }

    boost::system::error_code ec;
#ifndef ZMQ_HAVE_WINDOWS
    const std::size_t bytes_written =
      _stream_descriptor->write_some (boost::asio::buffer (data, len), ec);
#else
    const std::size_t bytes_written =
      _socket->write_some (boost::asio::buffer (data, len), ec);
#endif

    if (ec) {
        if (ec == boost::asio::error::would_block
            || ec == boost::asio::error::try_again) {
            errno = EAGAIN;
            if (ipc_stats_enabled ())
                ++ipc_write_some_eagain;
            return 0;
        }
        if (ec == boost::asio::error::broken_pipe
            || ec == boost::asio::error::connection_reset) {
            errno = EPIPE;
        } else if (ec == boost::asio::error::not_connected) {
            errno = ENOTCONN;
        } else if (ec == boost::asio::error::bad_descriptor) {
            errno = EBADF;
        } else {
            errno = EIO;
        }
        if (ipc_stats_enabled ())
            ++ipc_write_some_errors;
        return 0;
    }

    errno = 0;
    if (ipc_stats_enabled ())
        ipc_write_some_bytes += bytes_written;
    return bytes_written;
}

}  // namespace zmq

#endif  // ZMQ_IOTHREAD_POLLER_USE_ASIO && ZMQ_HAVE_IPC
