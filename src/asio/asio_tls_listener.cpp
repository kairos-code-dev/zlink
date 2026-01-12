/* SPDX-License-Identifier: MPL-2.0 */

#include "../precompiled.hpp"
#if defined ZMQ_IOTHREAD_POLLER_USE_ASIO && defined ZMQ_HAVE_ASIO_SSL

#include "asio_tls_listener.hpp"
#include "asio_poller.hpp"
#include "asio_zmtp_engine.hpp"
#include "ssl_context_helper.hpp"
#include "../io_thread.hpp"
#include "../session_base.hpp"
#include "../socket_base.hpp"
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

// Debug logging for ASIO TLS listener - set to 1 to enable
#define ASIO_TLS_LISTENER_DEBUG 0

#if ASIO_TLS_LISTENER_DEBUG
#include <cstdio>
#define TLS_LISTENER_DBG(fmt, ...)                                             \
    fprintf (stderr, "[ASIO_TLS_LISTENER] " fmt "\n", ##__VA_ARGS__)
#else
#define TLS_LISTENER_DBG(fmt, ...)
#endif

zmq::asio_tls_listener_t::asio_tls_listener_t (io_thread_t *io_thread_,
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
    TLS_LISTENER_DBG ("Constructor called, this=%p", static_cast<void *> (this));
}

zmq::asio_tls_listener_t::~asio_tls_listener_t ()
{
    TLS_LISTENER_DBG ("Destructor called, this=%p", static_cast<void *> (this));
}

int zmq::asio_tls_listener_t::set_local_address (const char *addr_)
{
    TLS_LISTENER_DBG ("set_local_address: addr=%s", addr_);

    //  Create SSL context first (server requires cert+key)
    if (!create_ssl_context ()) {
        TLS_LISTENER_DBG ("set_local_address: failed to create SSL context");
        errno = EINVAL;
        return -1;
    }

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
        TLS_LISTENER_DBG ("Failed to open acceptor: %s", ec.message ().c_str ());
        errno = EADDRINUSE;
        return -1;
    }

    //  Set socket options
#ifdef ZMQ_HAVE_WINDOWS
    //  Windows: use SO_EXCLUSIVEADDRUSE
    _acceptor.set_option (
      boost::asio::detail::socket_option::boolean<SOL_SOCKET,
                                                   SO_EXCLUSIVEADDRUSE> (true),
      ec);
#else
    //  POSIX: use SO_REUSEADDR
    _acceptor.set_option (boost::asio::socket_base::reuse_address (true), ec);
#endif
    if (ec) {
        TLS_LISTENER_DBG ("Failed to set reuse_address: %s",
                          ec.message ().c_str ());
        _acceptor.close ();
        errno = EADDRINUSE;
        return -1;
    }

    //  For IPv6, set IPV6_V6ONLY based on options
    if (_address.family () == AF_INET6) {
        _acceptor.set_option (boost::asio::ip::v6_only (!options.ipv6), ec);
        if (ec) {
            TLS_LISTENER_DBG ("Failed to set v6only: %s", ec.message ().c_str ());
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
        TLS_LISTENER_DBG ("Failed to bind: %s", ec.message ().c_str ());
        _acceptor.close ();
        errno = EADDRINUSE;
        return -1;
    }

    //  Listen for incoming connections
    _acceptor.listen (options.backlog, ec);
    if (ec) {
        TLS_LISTENER_DBG ("Failed to listen: %s", ec.message ().c_str ());
        _acceptor.close ();
        errno = EADDRINUSE;
        return -1;
    }

    //  Get endpoint string for events (resolves wildcard port)
    _endpoint = get_socket_name (_acceptor.native_handle (), socket_end_local);

    _socket->event_listening (make_unconnected_bind_endpoint_pair (_endpoint),
                              _acceptor.native_handle ());

    TLS_LISTENER_DBG ("Listening on %s (fd=%d)", _endpoint.c_str (),
                      static_cast<int> (_acceptor.native_handle ()));

    return 0;
}

int zmq::asio_tls_listener_t::get_local_address (std::string &addr_) const
{
    addr_ = _endpoint;
    return addr_.empty () ? -1 : 0;
}

std::string zmq::asio_tls_listener_t::get_socket_name (
  fd_t fd_,
  socket_end_t socket_end_) const
{
    return zmq::get_socket_name<tcp_address_t> (fd_, socket_end_);
}

void zmq::asio_tls_listener_t::process_plug ()
{
    TLS_LISTENER_DBG ("process_plug called");
    start_accept ();
}

void zmq::asio_tls_listener_t::process_term (int linger_)
{
    TLS_LISTENER_DBG ("process_term called, linger=%d, accepting=%d", linger_,
                      _accepting);

    _terminating = true;
    _linger = linger_;

    //  Close the acceptor - cancels pending async_accept
    if (_acceptor.is_open ()) {
        fd_t fd = _acceptor.native_handle ();
        boost::system::error_code ec;
        _acceptor.close (ec);
        _socket->event_closed (make_unconnected_bind_endpoint_pair (_endpoint),
                               fd);
    }

    //  Process pending handlers while object is still alive
    if (_accepting) {
        _io_context.poll ();
    }

    own_t::process_term (linger_);
}

void zmq::asio_tls_listener_t::start_accept ()
{
    if (_accepting || !_acceptor.is_open ())
        return;

    _accepting = true;
    TLS_LISTENER_DBG ("start_accept: starting async_accept");

    _acceptor.async_accept (
      _accept_socket,
      [this] (const boost::system::error_code &ec) { on_tcp_accept (ec); });
}

void zmq::asio_tls_listener_t::on_tcp_accept (
  const boost::system::error_code &ec)
{
    _accepting = false;
    TLS_LISTENER_DBG ("on_tcp_accept: ec=%s, terminating=%d",
                      ec.message ().c_str (), _terminating);

    if (_terminating) {
        TLS_LISTENER_DBG ("on_tcp_accept: terminating, ignoring callback");
        if (!ec) {
            _accept_socket.close ();
        }
        return;
    }

    if (ec) {
        if (ec == boost::asio::error::operation_aborted) {
            TLS_LISTENER_DBG ("on_tcp_accept: operation aborted");
            return;
        }

        //  Report accept failure
        _socket->event_accept_failed (
          make_unconnected_bind_endpoint_pair (_endpoint), ec.value ());

        //  Continue accepting
        _accept_socket = boost::asio::ip::tcp::socket (_io_context);
        start_accept ();
        return;
    }

    TLS_LISTENER_DBG ("on_tcp_accept: TCP accepted, fd=%d",
                      static_cast<int> (_accept_socket.native_handle ()));

    //  Get the native handle before wrapping in SSL
    fd_t fd = _accept_socket.native_handle ();

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
        TLS_LISTENER_DBG ("on_tcp_accept: connection rejected by filter");
        _accept_socket.close ();
        _accept_socket = boost::asio::ip::tcp::socket (_io_context);
        start_accept ();
        return;
    }

    //  Wrap socket in SSL stream for handshake
    typedef boost::asio::ssl::stream<boost::asio::ip::tcp::socket> ssl_stream_t;
    std::unique_ptr<ssl_stream_t> ssl_stream;

    try {
        ssl_stream = std::unique_ptr<ssl_stream_t> (
          new ssl_stream_t (std::move (_accept_socket), *_ssl_context));
    } catch (const std::bad_alloc &) {
        TLS_LISTENER_DBG ("on_tcp_accept: failed to allocate SSL stream");
        _socket->event_accept_failed (
          make_unconnected_bind_endpoint_pair (_endpoint), ENOMEM);
        //  Socket was moved, create new one
        _accept_socket = boost::asio::ip::tcp::socket (_io_context);
        start_accept ();
        return;
    }

    TLS_LISTENER_DBG ("on_tcp_accept: starting SSL handshake");

    //  Start SSL handshake (server side)
    //  Note: Boost.Asio requires copyable handlers in C++11,
    //  so we use shared_ptr instead of unique_ptr
    typedef boost::asio::ssl::stream<boost::asio::ip::tcp::socket> ssl_stream_t;
    std::shared_ptr<ssl_stream_t> ssl_stream_shared (ssl_stream.release ());

    ssl_stream_shared->async_handshake (
      boost::asio::ssl::stream_base::server,
      [this, fd, ssl_stream_shared] (const boost::system::error_code &ec) {
          on_ssl_handshake (ec, fd, ssl_stream_shared);
      });

    //  Note: We don't mark _accepting = true here because the handshake
    //  is not an accept operation. We'll start the next accept after handshake.
}

void zmq::asio_tls_listener_t::on_ssl_handshake (
  const boost::system::error_code &ec,
  fd_t fd_,
  std::shared_ptr<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>>
    ssl_stream)
{
    TLS_LISTENER_DBG ("on_ssl_handshake: ec=%s, fd=%d, terminating=%d",
                      ec.message ().c_str (), fd_, _terminating);

    if (_terminating) {
        TLS_LISTENER_DBG ("on_ssl_handshake: terminating, closing connection");
        //  shared_ptr will clean up automatically
        _accept_socket = boost::asio::ip::tcp::socket (_io_context);
        return;
    }

    if (ec) {
        TLS_LISTENER_DBG ("on_ssl_handshake: SSL handshake failed: %s",
                          ec.message ().c_str ());
        _socket->event_accept_failed (
          make_unconnected_bind_endpoint_pair (_endpoint), ec.value ());

        //  shared_ptr will clean up automatically, prepare for next accept
        _accept_socket = boost::asio::ip::tcp::socket (_io_context);
        start_accept ();
        return;
    }

    TLS_LISTENER_DBG ("on_ssl_handshake: SSL handshake successful, fd=%d", fd_);

    //  Release socket from ASIO management (engine will take ownership)
    ssl_stream->lowest_layer ().release ();

    //  Tune the socket
    if (tune_socket (fd_) != 0) {
        TLS_LISTENER_DBG ("on_ssl_handshake: tune_socket failed");
        _socket->event_accept_failed (
          make_unconnected_bind_endpoint_pair (_endpoint), zmq_errno ());
#ifdef ZMQ_HAVE_WINDOWS
        closesocket (fd_);
#else
        ::close (fd_);
#endif
        ssl_stream.reset ();
        _accept_socket = boost::asio::ip::tcp::socket (_io_context);
        start_accept ();
        return;
    }

    //  Create the engine (Phase 4-A: regular engine, Phase 4-B: SSL engine)
    //  TODO: Create asio_zmtp_engine with SSL transport
    create_engine (fd_);

    //  Clean up SSL stream (engine took ownership of fd)
    ssl_stream.reset ();

    //  Prepare for next connection
    _accept_socket = boost::asio::ip::tcp::socket (_io_context);
    start_accept ();
}

bool zmq::asio_tls_listener_t::create_ssl_context ()
{
    TLS_LISTENER_DBG ("create_ssl_context: cert=%s, key=%s, ca=%s",
                      options.tls_cert.c_str (), options.tls_key.c_str (),
                      options.tls_ca.c_str ());

    //  Server requires certificate and private key
    if (options.tls_cert.empty () || options.tls_key.empty ()) {
        TLS_LISTENER_DBG (
          "create_ssl_context: server requires tls_cert and tls_key");
        return false;
    }

    //  Create server SSL context
    _ssl_context = ssl_context_helper_t::create_server_context (
      options.tls_cert, options.tls_key, "");

    if (!_ssl_context) {
        TLS_LISTENER_DBG ("create_ssl_context: failed to create SSL context: %s",
                          ssl_context_helper_t::get_ssl_error_string ().c_str ());
        return false;
    }

    //  Configure client certificate verification based on options
    //  If tls_require_client_cert is set and CA is provided, enable mTLS
    bool require_client_cert = (options.tls_require_client_cert != 0);

    if (require_client_cert) {
        //  mTLS mode requires CA certificate to verify client
        if (options.tls_ca.empty ()) {
            TLS_LISTENER_DBG (
              "create_ssl_context: mTLS mode requires tls_ca to be set");
            return false;
        }

        TLS_LISTENER_DBG (
          "create_ssl_context: enabling mTLS (client certificate required)");

        if (!ssl_context_helper_t::load_ca_certificate (*_ssl_context,
                                                         options.tls_ca)) {
            TLS_LISTENER_DBG ("create_ssl_context: failed to load CA: %s",
                              ssl_context_helper_t::get_ssl_error_string ().c_str ());
            return false;
        }
    } else if (!options.tls_ca.empty ()) {
        //  CA specified but client cert not required - optional client auth
        TLS_LISTENER_DBG (
          "create_ssl_context: loading CA for optional client certificate verification");

        if (!ssl_context_helper_t::load_ca_certificate (*_ssl_context,
                                                         options.tls_ca)) {
            TLS_LISTENER_DBG ("create_ssl_context: failed to load CA: %s",
                              ssl_context_helper_t::get_ssl_error_string ().c_str ());
            return false;
        }
    }

    //  Configure server verification mode
    if (!ssl_context_helper_t::configure_server_verification (*_ssl_context,
                                                               require_client_cert)) {
        TLS_LISTENER_DBG (
          "create_ssl_context: failed to configure server verification: %s",
          ssl_context_helper_t::get_ssl_error_string ().c_str ());
        return false;
    }

    TLS_LISTENER_DBG ("create_ssl_context: SSL context created successfully");
    return true;
}

void zmq::asio_tls_listener_t::create_engine (fd_t fd_)
{
    TLS_LISTENER_DBG ("create_engine: fd=%d", fd_);

    const endpoint_uri_pair_t endpoint_pair (
      get_socket_name (fd_, socket_end_local),
      get_socket_name (fd_, socket_end_remote), endpoint_type_bind);

    //  Phase 4-A: Use regular ASIO ZMTP engine
    //  Phase 4-B will use SSL-aware engine with ssl_transport_t
    //  TODO: Create asio_zmtp_engine with SSL transport
    i_engine *engine =
      new (std::nothrow) asio_zmtp_engine_t (fd_, options, endpoint_pair);
    alloc_assert (engine);

    //  Choose I/O thread to run engine in
    io_thread_t *io_thread = choose_io_thread (options.affinity);
    zmq_assert (io_thread);

    //  Create and launch a session
    session_base_t *session =
      session_base_t::create (io_thread, false, _socket, options, NULL);
    errno_assert (session);
    session->inc_seqnum ();
    launch_child (session);
    send_attach (session, engine, false);

    _socket->event_accepted (endpoint_pair, fd_);
}

void zmq::asio_tls_listener_t::close ()
{
    TLS_LISTENER_DBG ("close called");

    if (_acceptor.is_open ()) {
        fd_t fd = _acceptor.native_handle ();
        boost::system::error_code ec;
        _acceptor.close (ec);

        _socket->event_closed (make_unconnected_bind_endpoint_pair (_endpoint),
                               fd);
    }
}

int zmq::asio_tls_listener_t::tune_socket (fd_t fd_) const
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

bool zmq::asio_tls_listener_t::apply_accept_filters (
  fd_t fd_,
  const struct sockaddr_storage &ss,
  socklen_t ss_len) const
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

    //  Set NOSIGPIPE
    if (zmq::set_nosigpipe (fd_)) {
        return false;
    }

    //  Set IP Type-Of-Service
    if (options.tos != 0)
        set_ip_type_of_service (fd_, options.tos);

    //  Set socket priority
    if (options.priority != 0)
        set_socket_priority (fd_, options.priority);

    return true;
}

#endif  // ZMQ_IOTHREAD_POLLER_USE_ASIO && ZMQ_HAVE_ASIO_SSL
