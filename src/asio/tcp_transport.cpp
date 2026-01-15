/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"
#include "tcp_transport.hpp"

#if defined ZMQ_IOTHREAD_POLLER_USE_ASIO

#include "asio_debug.hpp"
#include "../address.hpp"

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

tcp_transport_t::tcp_transport_t ()
{
}

tcp_transport_t::~tcp_transport_t ()
{
    close ();
}

bool tcp_transport_t::open (boost::asio::io_context &io_context, fd_t fd)
{
#if defined ZMQ_HAVE_WINDOWS
    //  Windows: Create TCP socket and assign native handle
    try {
        _socket = std::unique_ptr<boost::asio::ip::tcp::socket> (
          new boost::asio::ip::tcp::socket (io_context));
    } catch (const std::bad_alloc &) {
        return false;
    }

    boost::system::error_code ec;
    _socket->assign (protocol_for_fd (fd), fd, ec);
    if (ec) {
        ASIO_GLOBAL_ERROR ("tcp_transport open failed: %s", ec.message ().c_str ());
        _socket.reset ();
        return false;
    }
#else
    //  POSIX: Create stream descriptor from file descriptor
    try {
        _stream_descriptor =
          std::unique_ptr<boost::asio::posix::stream_descriptor> (
            new boost::asio::posix::stream_descriptor (io_context, fd));
    } catch (const std::bad_alloc &) {
        return false;
    }
#endif

    return true;
}

bool tcp_transport_t::is_open () const
{
#if defined ZMQ_HAVE_WINDOWS
    return _socket && _socket->is_open ();
#else
    return _stream_descriptor && _stream_descriptor->is_open ();
#endif
}

void tcp_transport_t::close ()
{
#if defined ZMQ_HAVE_WINDOWS
    if (_socket) {
        boost::system::error_code ec;
        _socket->close (ec);
        //  Ignore close errors
        _socket.reset ();
    }
#else
    if (_stream_descriptor) {
        boost::system::error_code ec;
        _stream_descriptor->close (ec);
        //  Ignore close errors
        _stream_descriptor.reset ();
    }
#endif
}

void tcp_transport_t::async_read_some (unsigned char *buffer,
                                       std::size_t buffer_size,
                                       completion_handler_t handler)
{
#if defined ZMQ_HAVE_WINDOWS
    if (_socket) {
        _socket->async_read_some (boost::asio::buffer (buffer, buffer_size),
                                  handler);
    } else if (handler) {
        handler (boost::asio::error::bad_descriptor, 0);
    }
#else
    if (_stream_descriptor) {
        _stream_descriptor->async_read_some (
          boost::asio::buffer (buffer, buffer_size), handler);
    } else if (handler) {
        handler (boost::asio::error::bad_descriptor, 0);
    }
#endif
}

void tcp_transport_t::async_write_some (const unsigned char *buffer,
                                        std::size_t buffer_size,
                                        completion_handler_t handler)
{
#if defined ZMQ_HAVE_WINDOWS
    if (_socket) {
        _socket->async_write_some (boost::asio::buffer (buffer, buffer_size),
                                   handler);
    } else if (handler) {
        handler (boost::asio::error::bad_descriptor, 0);
    }
#else
    if (_stream_descriptor) {
        boost::asio::async_write (*_stream_descriptor,
                                  boost::asio::buffer (buffer, buffer_size),
                                  handler);
    } else if (handler) {
        handler (boost::asio::error::bad_descriptor, 0);
    }
#endif
}

std::size_t tcp_transport_t::write_some (const std::uint8_t *data,
                                         std::size_t len)
{
    if (len == 0) {
        return 0;
    }

    boost::system::error_code ec;
    std::size_t bytes_written = 0;

#if defined ZMQ_HAVE_WINDOWS
    if (!_socket || !_socket->is_open ()) {
        errno = EBADF;
        return 0;
    }

    //  Perform synchronous write_some on Windows socket
    bytes_written =
      _socket->write_some (boost::asio::buffer (data, len), ec);
#else
    if (!_stream_descriptor || !_stream_descriptor->is_open ()) {
        errno = EBADF;
        return 0;
    }

    //  Perform synchronous write_some on POSIX stream descriptor
    bytes_written =
      _stream_descriptor->write_some (boost::asio::buffer (data, len), ec);
#endif

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
        } else {
            errno = EIO;
        }
        return 0;
    }

    errno = 0;
    return bytes_written;
}

}  // namespace zmq

#endif  // ZMQ_IOTHREAD_POLLER_USE_ASIO
