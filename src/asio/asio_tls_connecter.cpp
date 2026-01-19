/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"
#if defined ZMQ_IOTHREAD_POLLER_USE_ASIO && defined ZMQ_HAVE_ASIO_SSL

#include "asio_tls_connecter.hpp"
#include "asio_poller.hpp"
#include "asio_zmtp_engine.hpp"
#include "asio_zmp_engine.hpp"
#include "ssl_transport.hpp"
#include "ssl_context_helper.hpp"
#include "../io_thread.hpp"
#include "../session_base.hpp"
#include "../address.hpp"
#include "../tcp_address.hpp"
#include "../random.hpp"
#include "../err.hpp"
#include "../zmp_protocol.hpp"
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

#include <limits>

// Debug logging for ASIO TLS connecter - set to 1 to enable
#define ASIO_TLS_CONNECTER_DEBUG 0

#if ASIO_TLS_CONNECTER_DEBUG
#include <cstdio>
#define TLS_CONNECTER_DBG(fmt, ...)                                            \
    fprintf (stderr, "[ASIO_TLS_CONNECTER] " fmt "\n", ##__VA_ARGS__)
#else
#define TLS_CONNECTER_DBG(fmt, ...)
#endif

namespace
{
std::string extract_tls_hostname (const std::string &address)
{
    std::string target = address;
    const std::string::size_type delim = target.rfind (';');
    if (delim != std::string::npos)
        target = target.substr (delim + 1);

    if (target.empty ())
        return std::string ();

    if (target[0] == '[') {
        const std::string::size_type end = target.find (']');
        if (end == std::string::npos)
            return std::string ();
        return target.substr (1, end - 1);
    }

    const std::string::size_type colon = target.rfind (':');
    if (colon == std::string::npos)
        return std::string ();

    const std::string host = target.substr (0, colon);
    if (host.empty () || host == "*")
        return std::string ();

    return host;
}
}

zmq::asio_tls_connecter_t::asio_tls_connecter_t (
  io_thread_t *io_thread_,
  session_base_t *session_,
  const options_t &options_,
  address_t *addr_,
  bool delayed_start_) :
    own_t (io_thread_, options_),
    io_object_t (io_thread_),
    _io_context (io_thread_->get_io_context ()),
    _socket (_io_context),
    _addr (addr_),
    _session (session_),
    _socket_ptr (session_->get_socket ()),
    _delayed_start (delayed_start_),
    _reconnect_timer_started (false),
    _connect_timer_started (false),
    _tcp_connecting (false),
    _terminating (false),
    _linger (0),
    _current_reconnect_ivl (-1)
{
    zmq_assert (_addr);
    zmq_assert (_addr->protocol == protocol_name::tls);
    _addr->to_string (_endpoint_str);

    TLS_CONNECTER_DBG ("Constructor called, endpoint=%s, this=%p",
                       _endpoint_str.c_str (), static_cast<void *> (this));
}

zmq::asio_tls_connecter_t::~asio_tls_connecter_t ()
{
    TLS_CONNECTER_DBG ("Destructor called, this=%p", static_cast<void *> (this));
    zmq_assert (!_reconnect_timer_started);
    zmq_assert (!_connect_timer_started);
}

void zmq::asio_tls_connecter_t::process_plug ()
{
    TLS_CONNECTER_DBG ("process_plug called, delayed_start=%d", _delayed_start);

    if (_delayed_start)
        add_reconnect_timer ();
    else
        start_connecting ();
}

void zmq::asio_tls_connecter_t::process_term (int linger_)
{
    TLS_CONNECTER_DBG ("process_term called, linger=%d, tcp_connecting=%d",
                       linger_, _tcp_connecting);

    _terminating = true;
    _linger = linger_;

    if (_reconnect_timer_started) {
        cancel_timer (reconnect_timer_id);
        _reconnect_timer_started = false;
    }

    if (_connect_timer_started) {
        cancel_timer (connect_timer_id);
        _connect_timer_started = false;
    }

    //  Close socket/stream - this cancels any pending operations
    close ();

    //  Process pending handlers while object is still alive
    if (_tcp_connecting) {
        _io_context.poll ();
    }

    own_t::process_term (linger_);
}

void zmq::asio_tls_connecter_t::timer_event (int id_)
{
    TLS_CONNECTER_DBG ("timer_event: id=%d", id_);

    if (id_ == reconnect_timer_id) {
        _reconnect_timer_started = false;
        start_connecting ();
    } else if (id_ == connect_timer_id) {
        _connect_timer_started = false;
        //  TCP connection timed out
        if (_tcp_connecting) {
            boost::system::error_code ec;
            _socket.cancel (ec);
            _tcp_connecting = false;
        }
        close ();
        add_reconnect_timer ();
    } else {
        zmq_assert (false);
    }
}

void zmq::asio_tls_connecter_t::start_connecting ()
{
    TLS_CONNECTER_DBG ("start_connecting: endpoint=%s", _endpoint_str.c_str ());

    //  Determine effective TLS hostname for SNI/verification
    _tls_hostname = !options.tls_hostname.empty ()
                      ? options.tls_hostname
                      : extract_tls_hostname (_addr->address);

    //  Create SSL context if not already done
    if (!_ssl_context && !create_ssl_context (_tls_hostname)) {
        TLS_CONNECTER_DBG ("start_connecting: failed to create SSL context");
        add_reconnect_timer ();
        return;
    }

    //  Resolve the address if not already done
    if (_addr->resolved.tcp_addr != NULL) {
        LIBZMQ_DELETE (_addr->resolved.tcp_addr);
    }

    _addr->resolved.tcp_addr = new (std::nothrow) tcp_address_t ();
    alloc_assert (_addr->resolved.tcp_addr);

    int rc = _addr->resolved.tcp_addr->resolve (_addr->address.c_str (), false,
                                                 options.ipv6);
    if (rc != 0) {
        TLS_CONNECTER_DBG ("start_connecting: resolve failed");
        LIBZMQ_DELETE (_addr->resolved.tcp_addr);
        add_reconnect_timer ();
        return;
    }

    const tcp_address_t *tcp_addr = _addr->resolved.tcp_addr;

    //  Create endpoint from resolved address
    const struct sockaddr *sa = tcp_addr->addr ();
    if (sa->sa_family == AF_INET) {
        const struct sockaddr_in *sin =
          reinterpret_cast<const struct sockaddr_in *> (sa);
        _endpoint = boost::asio::ip::tcp::endpoint (
          boost::asio::ip::address_v4 (ntohl (sin->sin_addr.s_addr)),
          ntohs (sin->sin_port));
    } else {
        const struct sockaddr_in6 *sin6 =
          reinterpret_cast<const struct sockaddr_in6 *> (sa);
        boost::asio::ip::address_v6::bytes_type bytes;
        memcpy (bytes.data (), sin6->sin6_addr.s6_addr, 16);
        _endpoint = boost::asio::ip::tcp::endpoint (
          boost::asio::ip::address_v6 (bytes, sin6->sin6_scope_id),
          ntohs (sin6->sin6_port));
    }

    //  Open the socket
    boost::asio::ip::tcp protocol = _endpoint.address ().is_v6 ()
                                      ? boost::asio::ip::tcp::v6 ()
                                      : boost::asio::ip::tcp::v4 ();

    boost::system::error_code ec;
    _socket.open (protocol, ec);
    if (ec) {
        TLS_CONNECTER_DBG ("start_connecting: socket open failed: %s",
                           ec.message ().c_str ());
        add_reconnect_timer ();
        return;
    }

    //  Bind to source address if specified
    if (tcp_addr->has_src_addr ()) {
        _socket.set_option (boost::asio::socket_base::reuse_address (true), ec);

        //  Create source endpoint
        boost::asio::ip::tcp::endpoint src_endpoint;
        const struct sockaddr *src_sa = tcp_addr->src_addr ();
        if (src_sa->sa_family == AF_INET) {
            const struct sockaddr_in *sin =
              reinterpret_cast<const struct sockaddr_in *> (src_sa);
            src_endpoint = boost::asio::ip::tcp::endpoint (
              boost::asio::ip::address_v4 (ntohl (sin->sin_addr.s_addr)),
              ntohs (sin->sin_port));
        } else {
            const struct sockaddr_in6 *sin6 =
              reinterpret_cast<const struct sockaddr_in6 *> (src_sa);
            boost::asio::ip::address_v6::bytes_type bytes;
            memcpy (bytes.data (), sin6->sin6_addr.s6_addr, 16);
            src_endpoint = boost::asio::ip::tcp::endpoint (
              boost::asio::ip::address_v6 (bytes, sin6->sin6_scope_id),
              ntohs (sin6->sin6_port));
        }

        _socket.bind (src_endpoint, ec);
        if (ec) {
            TLS_CONNECTER_DBG ("start_connecting: bind failed: %s",
                               ec.message ().c_str ());
            close ();
            add_reconnect_timer ();
            return;
        }
    }

    TLS_CONNECTER_DBG ("start_connecting: initiating async_connect to %s:%d",
                       _endpoint.address ().to_string ().c_str (),
                       _endpoint.port ());

    //  Start TCP connection
    _tcp_connecting = true;
    _socket.async_connect (_endpoint,
                           [this] (const boost::system::error_code &ec) {
                               on_tcp_connect (ec);
                           });

    //  Add connect timeout
    add_connect_timer ();

    _socket_ptr->event_connect_delayed (
      make_unconnected_connect_endpoint_pair (_endpoint_str), 0);
}

void zmq::asio_tls_connecter_t::on_tcp_connect (
  const boost::system::error_code &ec)
{
    _tcp_connecting = false;
    TLS_CONNECTER_DBG ("on_tcp_connect: ec=%s, terminating=%d",
                       ec.message ().c_str (), _terminating);

    if (_terminating) {
        TLS_CONNECTER_DBG ("on_tcp_connect: terminating, ignoring callback");
        return;
    }

    //  Cancel connect timer
    if (_connect_timer_started) {
        cancel_timer (connect_timer_id);
        _connect_timer_started = false;
    }

    if (ec) {
        if (ec == boost::asio::error::operation_aborted) {
            TLS_CONNECTER_DBG ("on_tcp_connect: operation aborted");
            return;
        }

        TLS_CONNECTER_DBG ("on_tcp_connect: TCP connection failed: %s",
                           ec.message ().c_str ());
        close ();
        add_reconnect_timer ();
        return;
    }

    TLS_CONNECTER_DBG ("on_tcp_connect: TCP connected");

    //  Get the native handle before any further operations
    fd_t fd = _socket.native_handle ();
    TLS_CONNECTER_DBG ("on_tcp_connect: connected, fd=%d", fd);

    //  Release socket from ASIO management (transport takes ownership)
    _socket.release ();

    //  Tune the socket
    if (!tune_socket (fd)) {
        TLS_CONNECTER_DBG ("on_tcp_connect: tune_socket failed");
#ifdef ZMQ_HAVE_WINDOWS
        closesocket (fd);
#else
        ::close (fd);
#endif
        add_reconnect_timer ();
        return;
    }

    //  Get local address for engine
    std::string local_address =
      get_socket_name<tcp_address_t> (fd, socket_end_local);

    //  Create the engine with SSL transport
    create_engine (fd, local_address);
}

void zmq::asio_tls_connecter_t::add_connect_timer ()
{
    if (options.connect_timeout > 0) {
        TLS_CONNECTER_DBG ("add_connect_timer: timeout=%d",
                           options.connect_timeout);
        add_timer (options.connect_timeout, connect_timer_id);
        _connect_timer_started = true;
    }
}

void zmq::asio_tls_connecter_t::add_reconnect_timer ()
{
    if (options.reconnect_ivl > 0) {
        const int interval = get_new_reconnect_ivl ();
        TLS_CONNECTER_DBG ("add_reconnect_timer: interval=%d", interval);
        add_timer (interval, reconnect_timer_id);
        _socket_ptr->event_connect_retried (
          make_unconnected_connect_endpoint_pair (_endpoint_str), interval);
        _reconnect_timer_started = true;
    }
}

int zmq::asio_tls_connecter_t::get_new_reconnect_ivl ()
{
    if (options.reconnect_ivl_max > 0) {
        int candidate_interval = 0;
        if (_current_reconnect_ivl == -1)
            candidate_interval = options.reconnect_ivl;
        else if (_current_reconnect_ivl > std::numeric_limits<int>::max () / 2)
            candidate_interval = std::numeric_limits<int>::max ();
        else
            candidate_interval = _current_reconnect_ivl * 2;

        if (candidate_interval > options.reconnect_ivl_max)
            _current_reconnect_ivl = options.reconnect_ivl_max;
        else
            _current_reconnect_ivl = candidate_interval;
        return _current_reconnect_ivl;
    } else {
        if (_current_reconnect_ivl == -1)
            _current_reconnect_ivl = options.reconnect_ivl;
        const int random_jitter = generate_random () % options.reconnect_ivl;
        const int interval =
          _current_reconnect_ivl
              < std::numeric_limits<int>::max () - random_jitter
            ? _current_reconnect_ivl + random_jitter
            : std::numeric_limits<int>::max ();
        return interval;
    }
}

bool zmq::asio_tls_connecter_t::create_ssl_context (
  const std::string &hostname_)
{
    TLS_CONNECTER_DBG ("create_ssl_context: ca=%s, cert=%s, key=%s",
                       options.tls_ca.c_str (), options.tls_cert.c_str (),
                       options.tls_key.c_str ());

    const bool verify_peer = options.tls_verify != 0;
    const bool trust_system = options.tls_trust_system != 0;

    //  Determine if we're doing mutual TLS (client cert auth)
    bool has_client_cert = !options.tls_cert.empty () && !options.tls_key.empty ();

    if (verify_peer && options.tls_ca.empty () && !trust_system) {
        TLS_CONNECTER_DBG (
          "create_ssl_context: tls_verify=1 requires tls_ca or tls_trust_system");
        return false;
    }

    const ssl_context_helper_t::verification_mode verify_mode =
      verify_peer ? ssl_context_helper_t::verify_peer
                  : ssl_context_helper_t::verify_none;

    if (has_client_cert) {
        //  Client with certificate for mutual TLS
        _ssl_context = ssl_context_helper_t::create_client_context_with_cert (
          options.tls_ca, options.tls_cert, options.tls_key,
          options.tls_password, trust_system, verify_mode);
    } else {
        //  Client without certificate (server auth only)
        _ssl_context = ssl_context_helper_t::create_client_context (
          options.tls_ca, trust_system, verify_mode);
    }

    if (!_ssl_context) {
        TLS_CONNECTER_DBG ("create_ssl_context: failed to create SSL context: %s",
                           ssl_context_helper_t::get_ssl_error_string ().c_str ());
        return false;
    }

    //  Configure hostname verification if enabled and hostname is specified
    if (verify_peer && !hostname_.empty ()) {
        TLS_CONNECTER_DBG ("create_ssl_context: setting hostname verification: %s",
                           hostname_.c_str ());
        if (!ssl_context_helper_t::set_hostname_verification (*_ssl_context,
                                                               hostname_)) {
            TLS_CONNECTER_DBG ("create_ssl_context: failed to set hostname verification");
            return false;
        }
    }

    TLS_CONNECTER_DBG ("create_ssl_context: SSL context created successfully");
    return true;
}

void zmq::asio_tls_connecter_t::create_engine (fd_t fd_,
                                                const std::string &local_address_)
{
    TLS_CONNECTER_DBG ("create_engine: fd=%d, local=%s", fd_,
                       local_address_.c_str ());

    std::string local_endpoint = local_address_;
    if (local_endpoint.compare (0, 6, "tcp://") == 0)
        local_endpoint.replace (0, 6, "tls://");

    std::string remote_endpoint = _endpoint_str;
    if (remote_endpoint.compare (0, 6, "tcp://") == 0)
        remote_endpoint.replace (0, 6, "tls://");

    const endpoint_uri_pair_t endpoint_pair (local_endpoint, remote_endpoint,
                                             endpoint_type_connect);

    if (!_ssl_context) {
        TLS_CONNECTER_DBG ("create_engine: SSL context missing");
        close ();
        add_reconnect_timer ();
        return;
    }

    std::unique_ptr<ssl_transport_t> transport (
      new (std::nothrow) ssl_transport_t (*_ssl_context));
    alloc_assert (transport);
    if (!_tls_hostname.empty ())
        transport->set_hostname (_tls_hostname);

    i_engine *engine = NULL;
    if (zmp_protocol_enabled ()) {
        engine = new (std::nothrow) asio_zmp_engine_t (
          fd_, options, endpoint_pair, std::unique_ptr<i_asio_transport> (
                                       transport.release ()),
          std::move (_ssl_context));
    } else {
        engine = new (std::nothrow) asio_zmtp_engine_t (
          fd_, options, endpoint_pair, std::unique_ptr<i_asio_transport> (
                                       transport.release ()),
          std::move (_ssl_context));
    }
    alloc_assert (engine);

    //  Attach the engine to the session
    send_attach (_session, engine);

    //  Shut down the connecter
    terminate ();

    _socket_ptr->event_connected (endpoint_pair, fd_);
}

bool zmq::asio_tls_connecter_t::tune_socket (fd_t fd_)
{
    const int rc = tune_tcp_socket (fd_)
                   | tune_tcp_keepalives (fd_, options.tcp_keepalive,
                                          options.tcp_keepalive_cnt,
                                          options.tcp_keepalive_idle,
                                          options.tcp_keepalive_intvl)
                   | tune_tcp_maxrt (fd_, options.tcp_maxrt);
    return rc == 0;
}

void zmq::asio_tls_connecter_t::close ()
{
    TLS_CONNECTER_DBG ("close called");

    //  Close plain socket if open
    if (_socket.is_open ()) {
        fd_t fd = _socket.native_handle ();
        boost::system::error_code ec;
        _socket.close (ec);

        _socket_ptr->event_closed (
          make_unconnected_connect_endpoint_pair (_endpoint_str), fd);
    }
}

#endif  // ZMQ_IOTHREAD_POLLER_USE_ASIO && ZMQ_HAVE_ASIO_SSL
