/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"
#if defined ZMQ_IOTHREAD_POLLER_USE_ASIO && defined ZMQ_HAVE_WS

#include "asio_ws_listener.hpp"
#include "asio_ws_engine.hpp"
#include "asio_poller.hpp"
#include "ssl_context_helper.hpp"
#include "ws_transport.hpp"
#if defined ZMQ_HAVE_WSS
#include "wss_transport.hpp"
#endif
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

//  Debug logging for ASIO WS listener - set to 1 to enable
#define ASIO_WS_LISTENER_DEBUG 0

#if ASIO_WS_LISTENER_DEBUG
#include <cstdio>
#define WS_LISTENER_DBG(fmt, ...)                                              \
    fprintf (stderr, "[ASIO_WS_LISTENER] " fmt "\n", ##__VA_ARGS__)
#else
#define WS_LISTENER_DBG(fmt, ...)
#endif

#if defined ZMQ_HAVE_WSS
namespace
{
std::unique_ptr<boost::asio::ssl::context>
create_wss_server_context (const zmq::options_t &options_)
{
    //  Server requires certificate and private key
    if (options_.tls_cert.empty () || options_.tls_key.empty ()) {
        return std::unique_ptr<boost::asio::ssl::context> ();
    }

    std::unique_ptr<boost::asio::ssl::context> ssl_context =
      zmq::ssl_context_helper_t::create_server_context (
        options_.tls_cert, options_.tls_key, options_.tls_password);
    if (!ssl_context)
        return std::unique_ptr<boost::asio::ssl::context> ();

    const bool require_client_cert = options_.tls_require_client_cert != 0;
    const bool trust_system = options_.tls_trust_system != 0;

    if (require_client_cert) {
        if (options_.tls_ca.empty () && !trust_system) {
            return std::unique_ptr<boost::asio::ssl::context> ();
        }

        if (!options_.tls_ca.empty ()) {
            if (!zmq::ssl_context_helper_t::load_ca_certificate (
                  *ssl_context, options_.tls_ca)) {
                return std::unique_ptr<boost::asio::ssl::context> ();
            }
        } else if (trust_system) {
            ssl_context->set_default_verify_paths ();
        }
    } else if (!options_.tls_ca.empty ()) {
        if (!zmq::ssl_context_helper_t::load_ca_certificate (
              *ssl_context, options_.tls_ca)) {
            return std::unique_ptr<boost::asio::ssl::context> ();
        }
    }

    if (!zmq::ssl_context_helper_t::configure_server_verification (
          *ssl_context, require_client_cert)) {
        return std::unique_ptr<boost::asio::ssl::context> ();
    }

    return ssl_context;
}
}
#endif

zmq::asio_ws_listener_t::asio_ws_listener_t (io_thread_t *io_thread_,
                                              socket_base_t *socket_,
                                              const options_t &options_) :
    own_t (io_thread_, options_),
    io_object_t (io_thread_),
    _io_context (io_thread_->get_io_context ()),
    _acceptor (_io_context),
    _accept_socket (_io_context),
    _socket (socket_),
    _path ("/"),
    _port (0),
    _secure (false),
    _accepting (false),
    _terminating (false),
    _linger (0)
{
    WS_LISTENER_DBG ("Constructor called, this=%p", static_cast<void *> (this));
}

zmq::asio_ws_listener_t::~asio_ws_listener_t ()
{
    WS_LISTENER_DBG ("Destructor called, this=%p", static_cast<void *> (this));
}

int zmq::asio_ws_listener_t::set_local_address (const ws_address_t *addr_,
                                                bool secure_)
{
    WS_LISTENER_DBG ("set_local_address: host=%s, port=%u, path=%s",
                     addr_->host ().c_str (), addr_->port (),
                     addr_->path ().c_str ());

    _secure = secure_;
#if !defined ZMQ_HAVE_WSS
    if (_secure) {
        errno = EPROTONOSUPPORT;
        return -1;
    }
#endif

    //  Store WebSocket-specific address components
    _host = addr_->host ();
    _path = addr_->path ();
    _port = addr_->port ();

    //  Determine protocol family from the resolved address
    boost::asio::ip::tcp protocol = addr_->family () == AF_INET6
                                      ? boost::asio::ip::tcp::v6 ()
                                      : boost::asio::ip::tcp::v4 ();

    boost::system::error_code ec;

    //  Open the acceptor
    _acceptor.open (protocol, ec);
    if (ec) {
        WS_LISTENER_DBG ("Failed to open acceptor: %s", ec.message ().c_str ());
        errno = EADDRINUSE;
        return -1;
    }

    //  Set socket options
#ifdef ZMQ_HAVE_WINDOWS
    _acceptor.set_option (
      boost::asio::detail::socket_option::boolean<SOL_SOCKET,
                                                   SO_EXCLUSIVEADDRUSE> (true),
      ec);
#else
    _acceptor.set_option (boost::asio::socket_base::reuse_address (true), ec);
#endif
    if (ec) {
        WS_LISTENER_DBG ("Failed to set reuse_address: %s",
                         ec.message ().c_str ());
        _acceptor.close ();
        errno = EADDRINUSE;
        return -1;
    }

    //  For IPv6, set IPV6_V6ONLY option
    if (addr_->family () == AF_INET6) {
        _acceptor.set_option (boost::asio::ip::v6_only (!options.ipv6), ec);
        if (ec) {
            WS_LISTENER_DBG ("Failed to set v6only: %s", ec.message ().c_str ());
            //  Non-fatal, continue
        }
    }

    //  Construct boost endpoint from ws_address
    boost::asio::ip::tcp::endpoint bind_endpoint;
    const struct sockaddr *sa = addr_->addr ();
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
        WS_LISTENER_DBG ("Failed to bind: %s", ec.message ().c_str ());
        _acceptor.close ();
        errno = EADDRINUSE;
        return -1;
    }

    //  Listen for incoming connections
    _acceptor.listen (options.backlog, ec);
    if (ec) {
        WS_LISTENER_DBG ("Failed to listen: %s", ec.message ().c_str ());
        _acceptor.close ();
        errno = EADDRINUSE;
        return -1;
    }

    //  Get the actual bound port (in case port 0 was specified)
    boost::asio::ip::tcp::endpoint local_ep = _acceptor.local_endpoint (ec);
    if (!ec) {
        _port = local_ep.port ();
    }

    //  Build endpoint string with actual bound port (not the input port)
    //  Format: ws://host:port/path or ws://[ipv6]:port/path
    const std::string prefix = _secure ? "wss://" : "ws://";
    if (addr_->family () == AF_INET6) {
        _endpoint = prefix + "[" + _host + "]:" + std::to_string (_port) + _path;
    } else {
        _endpoint = prefix + _host + ":" + std::to_string (_port) + _path;
    }

    _socket->event_listening (make_unconnected_bind_endpoint_pair (_endpoint),
                              _acceptor.native_handle ());

    WS_LISTENER_DBG ("Listening on %s (fd=%d)", _endpoint.c_str (),
                     static_cast<int> (_acceptor.native_handle ()));

    return 0;
}

int zmq::asio_ws_listener_t::get_local_address (std::string &addr_) const
{
    addr_ = _endpoint;
    return addr_.empty () ? -1 : 0;
}

std::string
zmq::asio_ws_listener_t::get_socket_name (fd_t fd_,
                                           socket_end_t socket_end_) const
{
    return zmq::get_socket_name<tcp_address_t> (fd_, socket_end_);
}

void zmq::asio_ws_listener_t::process_plug ()
{
    WS_LISTENER_DBG ("process_plug called");

    //  Start accepting connections
    start_accept ();
}

void zmq::asio_ws_listener_t::process_term (int linger_)
{
    WS_LISTENER_DBG ("process_term called, linger=%d, accepting=%d", linger_,
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

    //  Process pending handlers
    if (_accepting) {
        _io_context.poll ();
    }

    own_t::process_term (linger_);
}

void zmq::asio_ws_listener_t::start_accept ()
{
    if (_accepting || !_acceptor.is_open ())
        return;

    _accepting = true;
    WS_LISTENER_DBG ("start_accept: starting async_accept");

    _acceptor.async_accept (
      _accept_socket,
      [this] (const boost::system::error_code &ec) { on_accept (ec); });
}

void zmq::asio_ws_listener_t::on_accept (const boost::system::error_code &ec)
{
    _accepting = false;
    WS_LISTENER_DBG ("on_accept: ec=%s, terminating=%d", ec.message ().c_str (),
                     _terminating);

    if (_terminating) {
        if (!ec) {
            _accept_socket.close ();
        }
        return;
    }

    if (ec) {
        if (ec == boost::asio::error::operation_aborted) {
            WS_LISTENER_DBG ("on_accept: operation aborted");
            return;
        }

        _socket->event_accept_failed (
          make_unconnected_bind_endpoint_pair (_endpoint), ec.value ());

        _accept_socket = boost::asio::ip::tcp::socket (_io_context);
        start_accept ();
        return;
    }

    //  Get the native handle before releasing ownership
    fd_t fd = _accept_socket.native_handle ();
    WS_LISTENER_DBG ("on_accept: accepted connection, fd=%d", fd);

    //  Get peer address for accept filter
    boost::system::error_code peer_ec;
    boost::asio::ip::tcp::endpoint remote_endpoint =
      _accept_socket.remote_endpoint (peer_ec);

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
        WS_LISTENER_DBG ("on_accept: connection rejected by filter");
        _accept_socket.close ();
        _accept_socket = boost::asio::ip::tcp::socket (_io_context);
        start_accept ();
        return;
    }

    //  Release ownership of the socket
    _accept_socket.release ();

    //  Tune the accepted socket
    if (tune_socket (fd) != 0) {
        WS_LISTENER_DBG ("on_accept: tune_socket failed");
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

    //  Create engine for this connection
    create_engine (fd);

    //  Prepare for next connection
    _accept_socket = boost::asio::ip::tcp::socket (_io_context);
    start_accept ();
}

void zmq::asio_ws_listener_t::create_engine (fd_t fd_)
{
    WS_LISTENER_DBG ("create_engine: fd=%d", fd_);

    std::string local_addr = get_socket_name (fd_, socket_end_local);
    std::string remote_addr = get_socket_name (fd_, socket_end_remote);

    if (local_addr.compare (0, 6, "tcp://") == 0)
        local_addr = local_addr.substr (6);
    if (remote_addr.compare (0, 6, "tcp://") == 0)
        remote_addr = remote_addr.substr (6);

    const std::string prefix = _secure ? "wss://" : "ws://";
    const endpoint_uri_pair_t endpoint_pair (
      prefix + local_addr + _path, prefix + remote_addr + _path,
      endpoint_type_bind);

    std::unique_ptr<i_asio_transport> transport;
#if defined ZMQ_HAVE_WSS
    std::unique_ptr<boost::asio::ssl::context> ssl_context;
    if (_secure) {
        ssl_context = create_wss_server_context (options);
        if (!ssl_context) {
            _socket->event_accept_failed (
              make_unconnected_bind_endpoint_pair (_endpoint), EINVAL);
#ifdef ZMQ_HAVE_WINDOWS
            closesocket (fd_);
#else
            ::close (fd_);
#endif
            return;
        }
        std::unique_ptr<wss_transport_t> wss_transport (
          new (std::nothrow)
            wss_transport_t (*ssl_context, _path, _host));
        alloc_assert (wss_transport);
        transport.reset (wss_transport.release ());
    } else
#endif
    {
        std::unique_ptr<ws_transport_t> ws_transport (
          new (std::nothrow) ws_transport_t (_path, _host));
        alloc_assert (ws_transport);
        transport.reset (ws_transport.release ());
    }

    i_engine *engine = NULL;
#if defined ZMQ_HAVE_WSS
    if (_secure) {
        engine = new (std::nothrow) asio_ws_engine_t (
          fd_, options, endpoint_pair, false, std::move (transport),
          std::move (ssl_context));
    } else
#endif
    {
        engine = new (std::nothrow) asio_ws_engine_t (
          fd_, options, endpoint_pair, false, std::move (transport));
    }
    alloc_assert (engine);

    //  Choose I/O thread for engine
    io_thread_t *io_thread = choose_io_thread (options.affinity);
    zmq_assert (io_thread);

    //  Create and launch session
    session_base_t *session =
      session_base_t::create (io_thread, false, _socket, options, NULL);
    errno_assert (session);
    session->inc_seqnum ();
    launch_child (session);
    send_attach (session, engine, false);

    _socket->event_accepted (endpoint_pair, fd_);
}

void zmq::asio_ws_listener_t::close ()
{
    WS_LISTENER_DBG ("close called");

    if (_acceptor.is_open ()) {
        fd_t fd = _acceptor.native_handle ();
        boost::system::error_code ec;
        _acceptor.close (ec);

        _socket->event_closed (make_unconnected_bind_endpoint_pair (_endpoint),
                               fd);
    }
}

int zmq::asio_ws_listener_t::tune_socket (fd_t fd_) const
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

bool zmq::asio_ws_listener_t::apply_accept_filters (
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

#endif  // ZMQ_IOTHREAD_POLLER_USE_ASIO && ZMQ_HAVE_WS
