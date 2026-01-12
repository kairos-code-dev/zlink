/* SPDX-License-Identifier: MPL-2.0 */

#include "../precompiled.hpp"
#if defined ZMQ_IOTHREAD_POLLER_USE_ASIO

#include "asio_tcp_listener.hpp"
#include "asio_poller.hpp"
#include "asio_zmtp_engine.hpp"
#include "../io_thread.hpp"
#include "../session_base.hpp"
#include "../socket_base.hpp"
#include "../zmtp_engine.hpp"
#include "../raw_engine.hpp"
#include "../config.hpp"
#include "../err.hpp"
#include "../ip.hpp"
#include "../tcp.hpp"

#ifndef ZMQ_HAVE_WINDOWS
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <netinet/in.h>
#include <netdb.h>
#include <fcntl.h>
#endif

// Debug logging for ASIO TCP listener - set to 1 to enable
#define ASIO_LISTENER_DEBUG 0

#if ASIO_LISTENER_DEBUG
#include <cstdio>
#define LISTENER_DBG(fmt, ...)                                                 \
    fprintf (stderr, "[ASIO_TCP_LISTENER] " fmt "\n", ##__VA_ARGS__)
#else
#define LISTENER_DBG(fmt, ...)
#endif

zmq::asio_tcp_listener_t::asio_tcp_listener_t (io_thread_t *io_thread_,
                                               socket_base_t *socket_,
                                               const options_t &options_) :
    own_t (io_thread_, options_),
    io_object_t (io_thread_),
    _io_context (io_thread_->get_io_context ()),
    _acceptor (_io_context),
    _accept_socket (_io_context),
    _socket (socket_),
    _accepting (false),
    _terminating (false),
    _linger (0)
{
    LISTENER_DBG ("Constructor called, this=%p", static_cast<void *> (this));
}

zmq::asio_tcp_listener_t::~asio_tcp_listener_t ()
{
    LISTENER_DBG ("Destructor called, this=%p", static_cast<void *> (this));
}

int zmq::asio_tcp_listener_t::set_local_address (const char *addr_)
{
    LISTENER_DBG ("set_local_address: addr=%s", addr_);

    //  Parse the address
    int rc = _address.resolve (addr_, true, options.ipv6);
    if (rc != 0)
        return -1;

    //  Determine protocol family
    boost::asio::ip::tcp protocol = _address.family () == AF_INET6
                                      ? boost::asio::ip::tcp::v6 ()
                                      : boost::asio::ip::tcp::v4 ();

    boost::system::error_code ec;

    //  Open the acceptor
    _acceptor.open (protocol, ec);
    if (ec) {
        LISTENER_DBG ("Failed to open acceptor: %s", ec.message ().c_str ());
        errno = EADDRINUSE;
        return -1;
    }

    //  Set socket options

    //  Allow reusing of the address (SO_REUSEADDR)
#ifdef ZMQ_HAVE_WINDOWS
    //  On Windows, use SO_EXCLUSIVEADDRUSE instead
    _acceptor.set_option (
      boost::asio::detail::socket_option::boolean<SOL_SOCKET,
                                                   SO_EXCLUSIVEADDRUSE> (true),
      ec);
#else
    _acceptor.set_option (boost::asio::socket_base::reuse_address (true), ec);
#endif
    if (ec) {
        LISTENER_DBG ("Failed to set reuse_address: %s", ec.message ().c_str ());
        _acceptor.close ();
        errno = EADDRINUSE;
        return -1;
    }

    //  For IPv6, set IPV6_V6ONLY option based on ipv6 setting
    if (_address.family () == AF_INET6) {
        _acceptor.set_option (boost::asio::ip::v6_only (!options.ipv6), ec);
        if (ec) {
            LISTENER_DBG ("Failed to set v6only: %s", ec.message ().c_str ());
            //  Non-fatal, continue
        }
    }

    //  Construct boost endpoint from zmq address
    boost::asio::ip::tcp::endpoint bind_endpoint;
    const struct sockaddr *sa = _address.addr ();
    if (sa->sa_family == AF_INET) {
        const struct sockaddr_in *sin =
          reinterpret_cast<const struct sockaddr_in *> (sa);
        bind_endpoint = boost::asio::ip::tcp::endpoint (
          boost::asio::ip::address_v4 (ntohl (sin->sin_addr.s_addr)),
          ntohs (sin->sin_port));
    } else {
        const struct sockaddr_in6 *sin6 =
          reinterpret_cast<const struct sockaddr_in6 *> (sa);
        boost::asio::ip::address_v6::bytes_type bytes;
        memcpy (bytes.data (), sin6->sin6_addr.s6_addr, 16);
        bind_endpoint = boost::asio::ip::tcp::endpoint (
          boost::asio::ip::address_v6 (bytes, sin6->sin6_scope_id),
          ntohs (sin6->sin6_port));
    }

    //  Bind the acceptor
    _acceptor.bind (bind_endpoint, ec);
    if (ec) {
        LISTENER_DBG ("Failed to bind: %s", ec.message ().c_str ());
        _acceptor.close ();
        errno = EADDRINUSE;
        return -1;
    }

    //  Listen for incoming connections
    _acceptor.listen (options.backlog, ec);
    if (ec) {
        LISTENER_DBG ("Failed to listen: %s", ec.message ().c_str ());
        _acceptor.close ();
        errno = EADDRINUSE;
        return -1;
    }

    //  Get endpoint string for events (this resolves wildcard port)
    _endpoint = get_socket_name (_acceptor.native_handle (), socket_end_local);

    _socket->event_listening (make_unconnected_bind_endpoint_pair (_endpoint),
                              _acceptor.native_handle ());

    LISTENER_DBG ("Listening on %s (fd=%d)", _endpoint.c_str (),
                  static_cast<int> (_acceptor.native_handle ()));

    return 0;
}

int zmq::asio_tcp_listener_t::get_local_address (std::string &addr_) const
{
    addr_ = _endpoint;
    return addr_.empty () ? -1 : 0;
}

std::string
zmq::asio_tcp_listener_t::get_socket_name (fd_t fd_,
                                           socket_end_t socket_end_) const
{
    return zmq::get_socket_name<tcp_address_t> (fd_, socket_end_);
}

void zmq::asio_tcp_listener_t::process_plug ()
{
    LISTENER_DBG ("process_plug called");

    //  Start accepting connections
    start_accept ();
}

void zmq::asio_tcp_listener_t::process_term (int linger_)
{
    LISTENER_DBG ("process_term called, linger=%d, accepting=%d", linger_,
                  _accepting);

    _terminating = true;
    _linger = linger_;

    //  Close the acceptor - this cancels any pending async_accept
    if (_acceptor.is_open ()) {
        fd_t fd = _acceptor.native_handle ();
        boost::system::error_code ec;
        _acceptor.close (ec);
        _socket->event_closed (make_unconnected_bind_endpoint_pair (_endpoint),
                               fd);
    }

    //  Process any pending handlers (including the cancelled async_accept)
    //  to ensure the callback fires while the object is still alive.
    //  The _terminating flag ensures the callback is a no-op.
    if (_accepting) {
        _io_context.poll ();
    }

    //  Now it's safe to call own_t::process_term to terminate child sessions
    own_t::process_term (linger_);
}

void zmq::asio_tcp_listener_t::start_accept ()
{
    if (_accepting || !_acceptor.is_open ())
        return;

    _accepting = true;
    LISTENER_DBG ("start_accept: starting async_accept");

    _acceptor.async_accept (
      _accept_socket,
      [this] (const boost::system::error_code &ec) { on_accept (ec); });
}

void zmq::asio_tcp_listener_t::on_accept (const boost::system::error_code &ec)
{
    _accepting = false;
    LISTENER_DBG ("on_accept: ec=%s, terminating=%d", ec.message ().c_str (),
                  _terminating);

    //  If terminating, just return - process_term already handled everything
    if (_terminating) {
        LISTENER_DBG ("on_accept: terminating, ignoring callback");
        //  Close any accepted socket if the accept succeeded during shutdown
        if (!ec) {
            _accept_socket.close ();
        }
        return;
    }

    if (ec) {
        if (ec == boost::asio::error::operation_aborted) {
            LISTENER_DBG ("on_accept: operation aborted");
            return;
        }

        //  Report accept failure
        _socket->event_accept_failed (
          make_unconnected_bind_endpoint_pair (_endpoint),
          ec.value ());

        //  Continue accepting
        _accept_socket = boost::asio::ip::tcp::socket (_io_context);
        start_accept ();
        return;
    }

    //  Get the native handle before releasing ownership
    fd_t fd = _accept_socket.native_handle ();
    LISTENER_DBG ("on_accept: accepted connection, fd=%d", fd);

    //  Get peer address for accept filter
    boost::system::error_code peer_ec;
    boost::asio::ip::tcp::endpoint remote_endpoint =
      _accept_socket.remote_endpoint (peer_ec);

    //  Store peer address in sockaddr_storage for filter check
    struct sockaddr_storage ss;
    socklen_t ss_len = sizeof (ss);
    memset (&ss, 0, sizeof (ss));

    if (!peer_ec) {
        if (remote_endpoint.address ().is_v4 ()) {
            struct sockaddr_in *sin =
              reinterpret_cast<struct sockaddr_in *> (&ss);
            sin->sin_family = AF_INET;
            sin->sin_port = htons (remote_endpoint.port ());
            sin->sin_addr.s_addr =
              htonl (remote_endpoint.address ().to_v4 ().to_uint ());
            ss_len = sizeof (struct sockaddr_in);
        } else {
            struct sockaddr_in6 *sin6 =
              reinterpret_cast<struct sockaddr_in6 *> (&ss);
            sin6->sin6_family = AF_INET6;
            sin6->sin6_port = htons (remote_endpoint.port ());
            auto bytes = remote_endpoint.address ().to_v6 ().to_bytes ();
            memcpy (sin6->sin6_addr.s6_addr, bytes.data (), 16);
            sin6->sin6_scope_id =
              remote_endpoint.address ().to_v6 ().scope_id ();
            ss_len = sizeof (struct sockaddr_in6);
        }
    }

    //  Apply accept filters
    if (!apply_accept_filters (fd, ss, ss_len)) {
        LISTENER_DBG ("on_accept: connection rejected by filter");
        _accept_socket.close ();
        _accept_socket = boost::asio::ip::tcp::socket (_io_context);
        start_accept ();
        return;
    }

    //  Release ownership of the socket (we'll manage the fd directly)
    _accept_socket.release ();

    //  Tune the accepted socket
    if (tune_socket (fd) != 0) {
        LISTENER_DBG ("on_accept: tune_socket failed");
        _socket->event_accept_failed (
          make_unconnected_bind_endpoint_pair (_endpoint), zmq_errno ());
#ifdef ZMQ_HAVE_WINDOWS
        closesocket (fd);
#else
        ::close (fd);
#endif
        _accept_socket = boost::asio::ip::tcp::socket (_io_context);
        start_accept ();
        return;
    }

    //  Create the engine for this connection
    create_engine (fd);

    //  Prepare for next connection
    _accept_socket = boost::asio::ip::tcp::socket (_io_context);
    start_accept ();
}

void zmq::asio_tcp_listener_t::create_engine (fd_t fd_)
{
    LISTENER_DBG ("create_engine: fd=%d", fd_);

    const endpoint_uri_pair_t endpoint_pair (
      get_socket_name (fd_, socket_end_local),
      get_socket_name (fd_, socket_end_remote), endpoint_type_bind);

    i_engine *engine;
    if (options.raw_socket)
        engine = new (std::nothrow) raw_engine_t (fd_, options, endpoint_pair);
    else
        //  Phase 1-C: Use ASIO ZMTP engine for true proactor mode
        engine = new (std::nothrow) asio_zmtp_engine_t (fd_, options, endpoint_pair);
    alloc_assert (engine);

    //  Choose I/O thread to run engine in. Given that we are already
    //  running in an I/O thread, there must be at least one available.
    io_thread_t *io_thread = choose_io_thread (options.affinity);
    zmq_assert (io_thread);

    //  Create and launch a session object.
    session_base_t *session =
      session_base_t::create (io_thread, false, _socket, options, NULL);
    errno_assert (session);
    session->inc_seqnum ();
    launch_child (session);
    send_attach (session, engine, false);

    _socket->event_accepted (endpoint_pair, fd_);
}

void zmq::asio_tcp_listener_t::close ()
{
    LISTENER_DBG ("close called");

    if (_acceptor.is_open ()) {
        fd_t fd = _acceptor.native_handle ();
        boost::system::error_code ec;
        _acceptor.close (ec);

        _socket->event_closed (make_unconnected_bind_endpoint_pair (_endpoint),
                               fd);
    }
}

int zmq::asio_tcp_listener_t::tune_socket (fd_t fd_) const
{
    int rc = tune_tcp_socket (fd_);
    rc = rc
         | tune_tcp_keepalives (fd_, options.tcp_keepalive,
                                options.tcp_keepalive_cnt,
                                options.tcp_keepalive_idle,
                                options.tcp_keepalive_intvl);
    rc = rc | tune_tcp_maxrt (fd_, options.tcp_maxrt);
    return rc;
}

bool zmq::asio_tcp_listener_t::apply_accept_filters (
  fd_t fd_, const struct sockaddr_storage &ss, socklen_t ss_len) const
{
    //  Make socket non-inheritable
    make_socket_noninheritable (fd_);

    //  Check accept filters
    if (!options.tcp_accept_filters.empty ()) {
        bool matched = false;
        for (options_t::tcp_accept_filters_t::size_type i = 0,
                                                        size =
                                                          options
                                                            .tcp_accept_filters
                                                            .size ();
             i != size; ++i) {
            if (options.tcp_accept_filters[i].match_address (
                  reinterpret_cast<const struct sockaddr *> (&ss), ss_len)) {
                matched = true;
                break;
            }
        }
        if (!matched) {
            return false;
        }
    }

    //  Set NOSIGPIPE on the accepted socket
    if (zmq::set_nosigpipe (fd_)) {
        return false;
    }

    //  Set the IP Type-Of-Service priority for this client socket
    if (options.tos != 0)
        set_ip_type_of_service (fd_, options.tos);

    //  Set the protocol-defined priority for this client socket
    if (options.priority != 0)
        set_socket_priority (fd_, options.priority);

    return true;
}

#endif  // ZMQ_IOTHREAD_POLLER_USE_ASIO
