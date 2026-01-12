/* SPDX-License-Identifier: MPL-2.0 */

#include "../precompiled.hpp"
#if defined ZMQ_IOTHREAD_POLLER_USE_ASIO && defined ZMQ_HAVE_WS

#include "asio_ws_connecter.hpp"
#include "asio_ws_engine.hpp"
#include "asio_poller.hpp"
#include "../io_thread.hpp"
#include "../session_base.hpp"
#include "../address.hpp"
#include "../ws_address.hpp"
#include "../random.hpp"
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

#include <limits>

//  Debug logging for ASIO WS connecter - set to 1 to enable
#define ASIO_WS_CONNECTER_DEBUG 0

#if ASIO_WS_CONNECTER_DEBUG
#include <cstdio>
#define WS_CONNECTER_DBG(fmt, ...)                                             \
    fprintf (stderr, "[ASIO_WS_CONNECTER] " fmt "\n", ##__VA_ARGS__)
#else
#define WS_CONNECTER_DBG(fmt, ...)
#endif

zmq::asio_ws_connecter_t::asio_ws_connecter_t (io_thread_t *io_thread_,
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
    _delayed_start (delayed_start_),
    _reconnect_timer_started (false),
    _connect_timer_started (false),
    _connecting (false),
    _terminating (false),
    _linger (0),
    _current_reconnect_ivl (-1)
{
    zmq_assert (_addr);
    zmq_assert (_addr->protocol == protocol_name::ws);
    _addr->to_string (_endpoint_str);

    WS_CONNECTER_DBG ("Constructor called, endpoint=%s, this=%p",
                      _endpoint_str.c_str (), static_cast<void *> (this));
}

zmq::asio_ws_connecter_t::~asio_ws_connecter_t ()
{
    WS_CONNECTER_DBG ("Destructor called, this=%p", static_cast<void *> (this));
    zmq_assert (!_reconnect_timer_started);
    zmq_assert (!_connect_timer_started);
}

void zmq::asio_ws_connecter_t::process_plug ()
{
    WS_CONNECTER_DBG ("process_plug called, delayed_start=%d", _delayed_start);

    if (_delayed_start)
        add_reconnect_timer ();
    else
        start_connecting ();
}

void zmq::asio_ws_connecter_t::process_term (int linger_)
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

void zmq::asio_ws_connecter_t::timer_event (int id_)
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
        zmq_assert (false);
    }
}

void zmq::asio_ws_connecter_t::start_connecting ()
{
    WS_CONNECTER_DBG ("start_connecting: endpoint=%s", _endpoint_str.c_str ());

    //  Resolve the WebSocket address if not already done
    if (_addr->resolved.ws_addr != NULL) {
        LIBZMQ_DELETE (_addr->resolved.ws_addr);
    }

    _addr->resolved.ws_addr = new (std::nothrow) ws_address_t ();
    alloc_assert (_addr->resolved.ws_addr);

    int rc =
      _addr->resolved.ws_addr->resolve (_addr->address.c_str (), false,
                                        options.ipv6);
    if (rc != 0) {
        WS_CONNECTER_DBG ("start_connecting: resolve failed");
        LIBZMQ_DELETE (_addr->resolved.ws_addr);
        add_reconnect_timer ();
        return;
    }

    const ws_address_t *ws_addr = _addr->resolved.ws_addr;

    //  Store WebSocket-specific data for engine creation
    _host = ws_addr->host () + ":" + std::to_string (ws_addr->port ());
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

void zmq::asio_ws_connecter_t::on_connect (const boost::system::error_code &ec)
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

    //  Create the engine
    create_engine (fd, local_address);
}

void zmq::asio_ws_connecter_t::add_connect_timer ()
{
    if (options.connect_timeout > 0) {
        WS_CONNECTER_DBG ("add_connect_timer: timeout=%d",
                          options.connect_timeout);
        add_timer (options.connect_timeout, connect_timer_id);
        _connect_timer_started = true;
    }
}

void zmq::asio_ws_connecter_t::add_reconnect_timer ()
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

int zmq::asio_ws_connecter_t::get_new_reconnect_ivl ()
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

void zmq::asio_ws_connecter_t::create_engine (
  fd_t fd_, const std::string &local_address_)
{
    WS_CONNECTER_DBG ("create_engine: fd=%d, local=%s", fd_,
                      local_address_.c_str ());

    const endpoint_uri_pair_t endpoint_pair ("ws://" + local_address_ + _path,
                                             _endpoint_str,
                                             endpoint_type_connect);

    //  Create WebSocket engine (client-side: is_client = true)
    i_engine *engine = new (std::nothrow)
      asio_ws_engine_t (fd_, options, endpoint_pair, _host, _path, true);
    alloc_assert (engine);

    //  Attach the engine to the session
    send_attach (_session, engine);

    //  Shut down the connecter
    terminate ();

    _socket_ptr->event_connected (endpoint_pair, fd_);
}

bool zmq::asio_ws_connecter_t::tune_socket (fd_t fd_)
{
    const int rc = tune_tcp_socket (fd_)
                   | tune_tcp_keepalives (fd_, options.tcp_keepalive,
                                          options.tcp_keepalive_cnt,
                                          options.tcp_keepalive_idle,
                                          options.tcp_keepalive_intvl)
                   | tune_tcp_maxrt (fd_, options.tcp_maxrt);
    return rc == 0;
}

void zmq::asio_ws_connecter_t::close ()
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

#endif  // ZMQ_IOTHREAD_POLLER_USE_ASIO && ZMQ_HAVE_WS
