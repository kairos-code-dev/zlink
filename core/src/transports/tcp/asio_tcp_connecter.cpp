/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"
#if defined ZLINK_IOTHREAD_POLLER_USE_ASIO

#include "transports/tcp/asio_tcp_connecter.hpp"
#include "engine/asio/asio_poller.hpp"
#include "engine/asio/asio_zmp_engine.hpp"
#include "engine/asio/asio_raw_engine.hpp"
#include "core/io_thread.hpp"
#include "core/session_base.hpp"
#include "core/address.hpp"
#include "transports/tcp/tcp_address.hpp"
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

// Debug logging for ASIO TCP connecter - set to 1 to enable
#define ASIO_CONNECTER_DEBUG 0

#if ASIO_CONNECTER_DEBUG
#include <cstdio>
#define CONNECTER_DBG(fmt, ...)                                                \
    fprintf (stderr, "[ASIO_TCP_CONNECTER] " fmt "\n", ##__VA_ARGS__)
#else
#define CONNECTER_DBG(fmt, ...)
#endif

zlink::asio_tcp_connecter_t::asio_tcp_connecter_t (io_thread_t *io_thread_,
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
    _connecting (false),
    _terminating (false),
    _linger (0),
    _current_reconnect_ivl (-1)
{
    zlink_assert (_addr);
    bool is_tcp_protocol = _addr->protocol == protocol_name::tcp;
#ifdef ZLINK_HAVE_TLS
    // TLS uses TCP address format
    is_tcp_protocol = is_tcp_protocol || _addr->protocol == protocol_name::tls;
#endif
    zlink_assert (is_tcp_protocol);
    _addr->to_string (_endpoint_str);

    CONNECTER_DBG ("Constructor called, endpoint=%s, this=%p",
                   _endpoint_str.c_str (), static_cast<void *> (this));
}

zlink::asio_tcp_connecter_t::~asio_tcp_connecter_t ()
{
    CONNECTER_DBG ("Destructor called, this=%p", static_cast<void *> (this));
    zlink_assert (!_reconnect_timer_started);
    zlink_assert (!_connect_timer_started);
}

void zlink::asio_tcp_connecter_t::process_plug ()
{
    CONNECTER_DBG ("process_plug called, delayed_start=%d", _delayed_start);

    if (_delayed_start)
        add_reconnect_timer ();
    else
        start_connecting ();
}

void zlink::asio_tcp_connecter_t::process_term (int linger_)
{
    CONNECTER_DBG ("process_term called, linger=%d, connecting=%d", linger_,
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

    //  Close the socket - this cancels any pending async_connect
    close ();

    //  Process any pending handlers (including the cancelled async_connect)
    //  to ensure the callback fires while the object is still alive.
    //  The _terminating flag ensures the callback is a no-op.
    if (_connecting) {
        _io_context.poll ();
    }

    //  Now it's safe to proceed with termination
    own_t::process_term (linger_);
}

void zlink::asio_tcp_connecter_t::timer_event (int id_)
{
    CONNECTER_DBG ("timer_event: id=%d", id_);

    if (id_ == reconnect_timer_id) {
        _reconnect_timer_started = false;
        start_connecting ();
    } else if (id_ == connect_timer_id) {
        _connect_timer_started = false;
        //  Connection timed out - cancel the async operation
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

void zlink::asio_tcp_connecter_t::start_connecting ()
{
    CONNECTER_DBG ("start_connecting: endpoint=%s", _endpoint_str.c_str ());

    //  Resolve the address if not already done
    if (_addr->resolved.tcp_addr != NULL) {
        LIBZLINK_DELETE (_addr->resolved.tcp_addr);
    }

    _addr->resolved.tcp_addr = new (std::nothrow) tcp_address_t ();
    alloc_assert (_addr->resolved.tcp_addr);

    int rc =
      _addr->resolved.tcp_addr->resolve (_addr->address.c_str (), false,
                                         options.ipv6);
    if (rc != 0) {
        CONNECTER_DBG ("start_connecting: resolve failed");
        LIBZLINK_DELETE (_addr->resolved.tcp_addr);
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
        CONNECTER_DBG ("start_connecting: socket open failed: %s",
                       ec.message ().c_str ());
        add_reconnect_timer ();
        return;
    }

    //  Bind to source address if specified
    if (tcp_addr->has_src_addr ()) {
        //  Allow reusing of the address
        _socket.set_option (boost::asio::socket_base::reuse_address (true), ec);
        if (ec) {
            CONNECTER_DBG ("start_connecting: set reuse_address failed: %s",
                           ec.message ().c_str ());
        }

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
            CONNECTER_DBG ("start_connecting: bind failed: %s",
                           ec.message ().c_str ());
            close ();
            add_reconnect_timer ();
            return;
        }
    }

    CONNECTER_DBG ("start_connecting: initiating async_connect to %s:%d",
                   _endpoint.address ().to_string ().c_str (),
                   _endpoint.port ());

    //  Start the connection
    _connecting = true;
    _socket.async_connect (
      _endpoint,
      [this] (const boost::system::error_code &ec) { on_connect (ec); });

    //  Add userspace connect timeout
    add_connect_timer ();

    _socket_ptr->event_connect_delayed (
      make_unconnected_connect_endpoint_pair (_endpoint_str), 0);
}

void zlink::asio_tcp_connecter_t::on_connect (const boost::system::error_code &ec)
{
    _connecting = false;
    CONNECTER_DBG ("on_connect: ec=%s, terminating=%d", ec.message ().c_str (),
                   _terminating);

    //  If terminating, just return - process_term already handled everything
    if (_terminating) {
        CONNECTER_DBG ("on_connect: terminating, ignoring callback");
        return;
    }

    //  Cancel connect timer if running
    if (_connect_timer_started) {
        cancel_timer (connect_timer_id);
        _connect_timer_started = false;
    }

    if (ec) {
        if (ec == boost::asio::error::operation_aborted) {
            CONNECTER_DBG ("on_connect: operation aborted");
            return;
        }

        //  Connection failed
        CONNECTER_DBG ("on_connect: connection failed: %s",
                       ec.message ().c_str ());
        close ();
        add_reconnect_timer ();
        return;
    }

    //  Get the native handle before any further operations
    fd_t fd = _socket.native_handle ();
    CONNECTER_DBG ("on_connect: connected, fd=%d", fd);

    //  Release socket from ASIO management (we'll manage it via stream_engine)
    _socket.release ();

    //  Tune the socket
    if (!tune_socket (fd)) {
        CONNECTER_DBG ("on_connect: tune_socket failed");
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

void zlink::asio_tcp_connecter_t::add_connect_timer ()
{
    if (options.connect_timeout > 0) {
        CONNECTER_DBG ("add_connect_timer: timeout=%d", options.connect_timeout);
        add_timer (options.connect_timeout, connect_timer_id);
        _connect_timer_started = true;
    }
}

void zlink::asio_tcp_connecter_t::add_reconnect_timer ()
{
    if (options.reconnect_ivl > 0) {
        const int interval = get_new_reconnect_ivl ();
        CONNECTER_DBG ("add_reconnect_timer: interval=%d", interval);
        add_timer (interval, reconnect_timer_id);
        _socket_ptr->event_connect_retried (
          make_unconnected_connect_endpoint_pair (_endpoint_str), interval);
        _reconnect_timer_started = true;
    }
}

int zlink::asio_tcp_connecter_t::get_new_reconnect_ivl ()
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
        //  The new interval is the base interval + random value.
        const int random_jitter = generate_random () % options.reconnect_ivl;
        const int interval =
          _current_reconnect_ivl
              < std::numeric_limits<int>::max () - random_jitter
            ? _current_reconnect_ivl + random_jitter
            : std::numeric_limits<int>::max ();
        return interval;
    }
}

void zlink::asio_tcp_connecter_t::create_engine (fd_t fd_,
                                               const std::string &local_address_)
{
    CONNECTER_DBG ("create_engine: fd=%d, local=%s", fd_,
                   local_address_.c_str ());

    const endpoint_uri_pair_t endpoint_pair (local_address_, _endpoint_str,
                                             endpoint_type_connect);

    //  Create the engine object for this connection using true proactor mode.
    i_engine *engine = NULL;
    if (options.type == ZLINK_STREAM)
        engine = new (std::nothrow) asio_raw_engine_t (fd_, options, endpoint_pair);
    else
        engine = new (std::nothrow) asio_zmp_engine_t (fd_, options, endpoint_pair);
    alloc_assert (engine);

    //  Attach the engine to the corresponding session object.
    send_attach (_session, engine);

    //  Shut the connecter down.
    terminate ();

    _socket_ptr->event_connected (endpoint_pair, fd_);
}

bool zlink::asio_tcp_connecter_t::tune_socket (fd_t fd_)
{
    const int rc = tune_tcp_socket (fd_)
                   | tune_tcp_keepalives (fd_, options.tcp_keepalive,
                                          options.tcp_keepalive_cnt,
                                          options.tcp_keepalive_idle,
                                          options.tcp_keepalive_intvl)
                   | tune_tcp_maxrt (fd_, options.tcp_maxrt);
    return rc == 0;
}

void zlink::asio_tcp_connecter_t::close ()
{
    CONNECTER_DBG ("close called");

    if (_socket.is_open ()) {
        fd_t fd = _socket.native_handle ();
        boost::system::error_code ec;
        _socket.close (ec);

        _socket_ptr->event_closed (
          make_unconnected_connect_endpoint_pair (_endpoint_str), fd);
    }
}

#endif  // ZLINK_IOTHREAD_POLLER_USE_ASIO
