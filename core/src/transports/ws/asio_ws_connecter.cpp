/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"
#if defined ZLINK_IOTHREAD_POLLER_USE_ASIO && defined ZLINK_HAVE_WS

#include "transports/ws/asio_ws_connecter.hpp"
#include "engine/asio/asio_zmp_engine.hpp"
#include "engine/asio/asio_raw_engine.hpp"
#include "engine/asio/asio_poller.hpp"
#include "transports/tls/ssl_context_helper.hpp"
#include "transports/ws/ws_transport.hpp"
#if defined ZLINK_HAVE_WSS
#include "transports/tls/wss_transport.hpp"
#include "transports/tls/wss_address.hpp"
#endif
#include "core/io_thread.hpp"
#include "core/session_base.hpp"
#include "core/address.hpp"
#include "transports/ws/ws_address.hpp"
#include "utils/random.hpp"
#include "utils/err.hpp"
#include "utils/ip.hpp"
#include "transports/tcp/tcp.hpp"

#ifndef ZLINK_HAVE_WINDOWS
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <netinet/in.h>
#include <netdb.h>
#include <fcntl.h>
#endif

#include <limits>
#include <cerrno>
#include <cstdlib>

//  Debug logging for ASIO WS connecter - set to 1 to enable
#define ASIO_WS_CONNECTER_DEBUG 0

#if ASIO_WS_CONNECTER_DEBUG
#include <cstdio>
#define WS_CONNECTER_DBG(fmt, ...)                                             \
    fprintf (stderr, "[ASIO_WS_CONNECTER] " fmt "\n", ##__VA_ARGS__)
#else
#define WS_CONNECTER_DBG(fmt, ...)
#endif

#if defined ZLINK_HAVE_WSS
namespace
{
std::unique_ptr<boost::asio::ssl::context>
create_wss_client_context (const zlink::options_t &options_,
                           const std::string &hostname_)
{
    const bool verify_peer = options_.tls_verify != 0;
    const bool trust_system = options_.tls_trust_system != 0;

    if (verify_peer && options_.tls_ca.empty () && !trust_system) {
        return std::unique_ptr<boost::asio::ssl::context> ();
    }

    const bool has_client_cert =
      !options_.tls_cert.empty () && !options_.tls_key.empty ();

    const zlink::ssl_context_helper_t::verification_mode verify_mode =
      verify_peer ? zlink::ssl_context_helper_t::verify_peer
                  : zlink::ssl_context_helper_t::verify_none;

    std::unique_ptr<boost::asio::ssl::context> ssl_context;
    if (has_client_cert) {
        ssl_context = zlink::ssl_context_helper_t::create_client_context_with_cert (
          options_.tls_ca, options_.tls_cert, options_.tls_key,
          options_.tls_password, trust_system, verify_mode);
    } else {
        ssl_context = zlink::ssl_context_helper_t::create_client_context (
          options_.tls_ca, trust_system, verify_mode);
    }

    if (!ssl_context)
        return std::unique_ptr<boost::asio::ssl::context> ();

    if (verify_peer && !hostname_.empty ()) {
        if (!zlink::ssl_context_helper_t::set_hostname_verification (
              *ssl_context, hostname_)) {
            return std::unique_ptr<boost::asio::ssl::context> ();
        }
    }

    return ssl_context;
}
}
#endif

namespace
{
size_t parse_size_env (const char *name_)
{
    const char *env = std::getenv (name_);
    if (!env || !*env)
        return 0;
    errno = 0;
    char *end = NULL;
    const unsigned long long value = std::strtoull (env, &end, 10);
    if (errno != 0 || end == env || value == 0)
        return 0;
    return static_cast<size_t> (value);
}

zlink::options_t adjust_ws_options (const zlink::options_t &options_)
{
    zlink::options_t adjusted = options_;
    const size_t ws_out = parse_size_env ("ZLINK_WS_OUT_BATCH_SIZE");
    const size_t ws_in = parse_size_env ("ZLINK_WS_IN_BATCH_SIZE");
    const int default_ws_batch = 65536;

    if (ws_out > 0)
        adjusted.out_batch_size = static_cast<int> (ws_out);
    else if (adjusted.out_batch_size < default_ws_batch)
        adjusted.out_batch_size = default_ws_batch;

    if (ws_in > 0)
        adjusted.in_batch_size = static_cast<int> (ws_in);
    else if (adjusted.in_batch_size < default_ws_batch)
        adjusted.in_batch_size = default_ws_batch;

    return adjusted;
}
}

zlink::asio_ws_connecter_t::asio_ws_connecter_t (io_thread_t *io_thread_,
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
    _path ("/"),
#if defined ZLINK_HAVE_WSS
    _secure (addr_ && addr_->protocol == protocol_name::wss),
#else
    _secure (false),
#endif
    _delayed_start (delayed_start_),
    _reconnect_timer_started (false),
    _connect_timer_started (false),
    _connecting (false),
    _terminating (false),
    _linger (0),
    _current_reconnect_ivl (-1)
{
    zlink_assert (_addr);
    bool is_ws_protocol = _addr->protocol == protocol_name::ws;
#if defined ZLINK_HAVE_WSS
    is_ws_protocol = is_ws_protocol || _addr->protocol == protocol_name::wss;
#endif
    zlink_assert (is_ws_protocol);
    _addr->to_string (_endpoint_str);

    WS_CONNECTER_DBG ("Constructor called, endpoint=%s, this=%p",
                      _endpoint_str.c_str (), static_cast<void *> (this));
}

zlink::asio_ws_connecter_t::~asio_ws_connecter_t ()
{
    WS_CONNECTER_DBG ("Destructor called, this=%p", static_cast<void *> (this));
    zlink_assert (!_reconnect_timer_started);
    zlink_assert (!_connect_timer_started);
}

void zlink::asio_ws_connecter_t::process_plug ()
{
    WS_CONNECTER_DBG ("process_plug called, delayed_start=%d", _delayed_start);

    if (_delayed_start)
        add_reconnect_timer ();
    else
        start_connecting ();
}

void zlink::asio_ws_connecter_t::process_term (int linger_)
{
    WS_CONNECTER_DBG ("process_term called, linger=%d, connecting=%d", linger_,
                      _connecting);

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

    //  Close socket - cancels pending async_connect
    close ();

    //  Process pending handlers
    if (_connecting) {
        _io_context.poll ();
    }

    own_t::process_term (linger_);
}

void zlink::asio_ws_connecter_t::timer_event (int id_)
{
    WS_CONNECTER_DBG ("timer_event: id=%d", id_);

    if (id_ == reconnect_timer_id) {
        _reconnect_timer_started = false;
        start_connecting ();
    } else if (id_ == connect_timer_id) {
        _connect_timer_started = false;
        //  Connection timed out
        if (_connecting) {
            boost::system::error_code ec;
            _socket.cancel (ec);
            _connecting = false;
        }
        close ();
        add_reconnect_timer ();
    } else {
        zlink_assert (false);
    }
}

void zlink::asio_ws_connecter_t::start_connecting ()
{
    WS_CONNECTER_DBG ("start_connecting: endpoint=%s", _endpoint_str.c_str ());

    //  Resolve the WebSocket address if not already done
#if defined ZLINK_HAVE_WSS
    if (_secure) {
        if (_addr->resolved.wss_addr != NULL) {
            LIBZLINK_DELETE (_addr->resolved.wss_addr);
        }
        _addr->resolved.wss_addr = new (std::nothrow) wss_address_t ();
        alloc_assert (_addr->resolved.wss_addr);
    } else {
        if (_addr->resolved.ws_addr != NULL) {
            LIBZLINK_DELETE (_addr->resolved.ws_addr);
        }
        _addr->resolved.ws_addr = new (std::nothrow) ws_address_t ();
        alloc_assert (_addr->resolved.ws_addr);
    }

    int rc = 0;
    if (_secure) {
        rc = _addr->resolved.wss_addr->resolve (_addr->address.c_str (), false,
                                                options.ipv6);
    } else {
        rc = _addr->resolved.ws_addr->resolve (_addr->address.c_str (), false,
                                               options.ipv6);
    }
    if (rc != 0) {
        WS_CONNECTER_DBG ("start_connecting: resolve failed");
        if (_secure) {
            LIBZLINK_DELETE (_addr->resolved.wss_addr);
        } else {
            LIBZLINK_DELETE (_addr->resolved.ws_addr);
        }
        add_reconnect_timer ();
        return;
    }

    const ws_address_t *ws_addr =
      _secure ? static_cast<const ws_address_t *> (_addr->resolved.wss_addr)
              : _addr->resolved.ws_addr;
#else
    (void) _secure;
    if (_addr->resolved.ws_addr != NULL) {
        LIBZLINK_DELETE (_addr->resolved.ws_addr);
    }
    _addr->resolved.ws_addr = new (std::nothrow) ws_address_t ();
    alloc_assert (_addr->resolved.ws_addr);

    int rc = _addr->resolved.ws_addr->resolve (_addr->address.c_str (), false,
                                               options.ipv6);
    if (rc != 0) {
        WS_CONNECTER_DBG ("start_connecting: resolve failed");
        LIBZLINK_DELETE (_addr->resolved.ws_addr);
        add_reconnect_timer ();
        return;
    }

    const ws_address_t *ws_addr = _addr->resolved.ws_addr;
#endif

    //  Store WebSocket-specific data for engine creation
    const std::string host = ws_addr->host ();
    _tls_hostname =
      !options.tls_hostname.empty () ? options.tls_hostname : host;
    if (host.find (':') != std::string::npos)
        _host = "[" + host + "]:" + std::to_string (ws_addr->port ());
    else
        _host = host + ":" + std::to_string (ws_addr->port ());
    _path = ws_addr->path ();

    //  Create endpoint from resolved address
    const struct sockaddr *sa = ws_addr->addr ();
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
        WS_CONNECTER_DBG ("start_connecting: socket open failed: %s",
                          ec.message ().c_str ());
        add_reconnect_timer ();
        return;
    }

    WS_CONNECTER_DBG ("start_connecting: initiating async_connect to %s:%d",
                      _endpoint.address ().to_string ().c_str (),
                      _endpoint.port ());

    //  Start the connection
    _connecting = true;
    _socket.async_connect (
      _endpoint,
      [this] (const boost::system::error_code &ec) { on_connect (ec); });

    //  Add connect timeout
    add_connect_timer ();

    _socket_ptr->event_connect_delayed (
      make_unconnected_connect_endpoint_pair (_endpoint_str), 0);
}

void zlink::asio_ws_connecter_t::on_connect (const boost::system::error_code &ec)
{
    _connecting = false;
    WS_CONNECTER_DBG ("on_connect: ec=%s, terminating=%d", ec.message ().c_str (),
                      _terminating);

    if (_terminating) {
        WS_CONNECTER_DBG ("on_connect: terminating, ignoring callback");
        return;
    }

    //  Cancel connect timer
    if (_connect_timer_started) {
        cancel_timer (connect_timer_id);
        _connect_timer_started = false;
    }

    if (ec) {
        if (ec == boost::asio::error::operation_aborted) {
            WS_CONNECTER_DBG ("on_connect: operation aborted");
            return;
        }

        WS_CONNECTER_DBG ("on_connect: connection failed: %s",
                          ec.message ().c_str ());
        close ();
        add_reconnect_timer ();
        return;
    }

    //  Get the native handle
    fd_t fd = _socket.native_handle ();
    WS_CONNECTER_DBG ("on_connect: connected, fd=%d", fd);

    //  Release socket from ASIO management
    _socket.release ();

    //  Tune the socket
    if (!tune_socket (fd)) {
        WS_CONNECTER_DBG ("on_connect: tune_socket failed");
#ifdef ZLINK_HAVE_WINDOWS
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

    //  Create the engine
    create_engine (fd, local_address);
}

void zlink::asio_ws_connecter_t::add_connect_timer ()
{
    if (options.connect_timeout > 0) {
        WS_CONNECTER_DBG ("add_connect_timer: timeout=%d",
                          options.connect_timeout);
        add_timer (options.connect_timeout, connect_timer_id);
        _connect_timer_started = true;
    }
}

void zlink::asio_ws_connecter_t::add_reconnect_timer ()
{
    if (options.reconnect_ivl > 0) {
        const int interval = get_new_reconnect_ivl ();
        WS_CONNECTER_DBG ("add_reconnect_timer: interval=%d", interval);
        add_timer (interval, reconnect_timer_id);
        _socket_ptr->event_connect_retried (
          make_unconnected_connect_endpoint_pair (_endpoint_str), interval);
        _reconnect_timer_started = true;
    }
}

int zlink::asio_ws_connecter_t::get_new_reconnect_ivl ()
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

void zlink::asio_ws_connecter_t::create_engine (
  fd_t fd_, const std::string &local_address_)
{
    WS_CONNECTER_DBG ("create_engine: fd=%d, local=%s", fd_,
                      local_address_.c_str ());

    const std::string prefix = _secure ? "wss://" : "ws://";

    std::string local_endpoint = local_address_;
    if (local_endpoint.compare (0, 6, "tcp://") == 0)
        local_endpoint = local_endpoint.substr (6);
    local_endpoint = prefix + local_endpoint + _path;

    std::string remote_endpoint = _endpoint_str;
    if (_secure && remote_endpoint.compare (0, 5, "ws://") == 0)
        remote_endpoint.replace (0, 5, "wss://");
    else if (!_secure && remote_endpoint.compare (0, 6, "wss://") == 0)
        remote_endpoint.replace (0, 6, "ws://");

    const endpoint_uri_pair_t endpoint_pair (
      local_endpoint, remote_endpoint, endpoint_type_connect);

    std::unique_ptr<i_asio_transport> transport;
#if defined ZLINK_HAVE_WSS
    std::unique_ptr<boost::asio::ssl::context> ssl_context;
    if (_secure) {
        ssl_context = create_wss_client_context (options, _tls_hostname);
        if (!ssl_context) {
            WS_CONNECTER_DBG ("create_engine: failed to create SSL context");
#ifdef ZLINK_HAVE_WINDOWS
            closesocket (fd_);
#else
            ::close (fd_);
#endif
            add_reconnect_timer ();
            return;
        }
        std::unique_ptr<wss_transport_t> wss_transport (
          new (std::nothrow)
            wss_transport_t (*ssl_context, _path, _host));
        alloc_assert (wss_transport);
        if (!_tls_hostname.empty ())
            wss_transport->set_tls_hostname (_tls_hostname);
        transport.reset (wss_transport.release ());
    } else
#endif
    {
        std::unique_ptr<ws_transport_t> ws_transport (
          new (std::nothrow) ws_transport_t (_path, _host));
        alloc_assert (ws_transport);
        transport.reset (ws_transport.release ());
    }

    const bool is_stream = options.type == ZLINK_STREAM;
    const options_t engine_options = is_stream ? options : adjust_ws_options (options);
    i_engine *engine = NULL;
#if defined ZLINK_HAVE_WSS
    if (_secure) {
        if (is_stream)
            engine = new (std::nothrow) asio_raw_engine_t (
              fd_, engine_options, endpoint_pair, std::move (transport),
              std::move (ssl_context));
        else
            engine = new (std::nothrow) asio_zmp_engine_t (
              fd_, engine_options, endpoint_pair, std::move (transport),
              std::move (ssl_context));
    } else
#endif
    {
        if (is_stream)
            engine = new (std::nothrow) asio_raw_engine_t (
              fd_, engine_options, endpoint_pair, std::move (transport));
        else
            engine = new (std::nothrow) asio_zmp_engine_t (
              fd_, engine_options, endpoint_pair, std::move (transport));
    }
    alloc_assert (engine);

    //  Attach the engine to the session
    send_attach (_session, engine);

    //  Shut down the connecter
    terminate ();

    _socket_ptr->event_connected (endpoint_pair, fd_);
}

bool zlink::asio_ws_connecter_t::tune_socket (fd_t fd_)
{
    const int rc = tune_tcp_socket (fd_)
                   | tune_tcp_keepalives (fd_, options.tcp_keepalive,
                                          options.tcp_keepalive_cnt,
                                          options.tcp_keepalive_idle,
                                          options.tcp_keepalive_intvl)
                   | tune_tcp_maxrt (fd_, options.tcp_maxrt);
    return rc == 0;
}

void zlink::asio_ws_connecter_t::close ()
{
    WS_CONNECTER_DBG ("close called");

    if (_socket.is_open ()) {
        fd_t fd = _socket.native_handle ();
        boost::system::error_code ec;
        _socket.close (ec);
        _socket_ptr->event_closed (
          make_unconnected_connect_endpoint_pair (_endpoint_str), fd);
    }
}

#endif  // ZLINK_IOTHREAD_POLLER_USE_ASIO && ZLINK_HAVE_WS
