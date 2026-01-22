/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"
#if defined ZMQ_IOTHREAD_POLLER_USE_ASIO && defined ZMQ_HAVE_IPC

#include "transports/ipc/asio_ipc_connecter.hpp"
#include "engine/asio/asio_poller.hpp"
#include "engine/asio/asio_zmp_engine.hpp"
#include "transports/ipc/ipc_transport.hpp"
#include "core/address.hpp"
#include "utils/err.hpp"
#include "core/io_thread.hpp"
#include "transports/ipc/ipc_address.hpp"
#include "utils/ip.hpp"
#include "utils/random.hpp"
#include "core/session_base.hpp"

#ifndef ZMQ_HAVE_WINDOWS
#include <unistd.h>
#include <sys/un.h>
#include <stddef.h>
#endif

#include <string.h>

#include <limits>
#include <memory>

//  Debug logging for ASIO IPC connecter - set to 1 to enable
#define ASIO_IPC_CONNECTER_DEBUG 0

#if ASIO_IPC_CONNECTER_DEBUG
#include <cstdio>
#define IPC_CONNECTER_DBG(fmt, ...)                                            \
    fprintf (stderr, "[ASIO_IPC_CONNECTER] " fmt "\n", ##__VA_ARGS__)
#else
#define IPC_CONNECTER_DBG(fmt, ...)
#endif

namespace
{
boost::asio::local::stream_protocol::endpoint
make_ipc_endpoint (const zmq::ipc_address_t &addr_)
{
    boost::asio::local::stream_protocol::endpoint endpoint;
    memcpy (endpoint.data (), addr_.addr (), addr_.addrlen ());
    endpoint.resize (addr_.addrlen ());
    return endpoint;
}
}

zmq::asio_ipc_connecter_t::asio_ipc_connecter_t (
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
    _connecting (false),
    _terminating (false),
    _linger (0),
    _current_reconnect_ivl (-1)
{
    zmq_assert (_addr);
    zmq_assert (_addr->protocol == protocol_name::ipc);
    _addr->to_string (_endpoint_str);

    IPC_CONNECTER_DBG ("Constructor called, endpoint=%s, this=%p",
                       _endpoint_str.c_str (), static_cast<void *> (this));
}

zmq::asio_ipc_connecter_t::~asio_ipc_connecter_t ()
{
    IPC_CONNECTER_DBG ("Destructor called, this=%p", static_cast<void *> (this));
    zmq_assert (!_reconnect_timer_started);
    zmq_assert (!_connect_timer_started);
}

void zmq::asio_ipc_connecter_t::process_plug ()
{
    IPC_CONNECTER_DBG ("process_plug called, delayed_start=%d", _delayed_start);

    if (_delayed_start)
        add_reconnect_timer ();
    else
        start_connecting ();
}

void zmq::asio_ipc_connecter_t::process_term (int linger_)
{
    IPC_CONNECTER_DBG ("process_term called, linger=%d, connecting=%d", linger_,
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

    close ();

    if (_connecting) {
        _io_context.poll ();
    }

    own_t::process_term (linger_);
}

void zmq::asio_ipc_connecter_t::timer_event (int id_)
{
    IPC_CONNECTER_DBG ("timer_event: id=%d", id_);

    if (id_ == reconnect_timer_id) {
        _reconnect_timer_started = false;
        start_connecting ();
    } else if (id_ == connect_timer_id) {
        _connect_timer_started = false;
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

void zmq::asio_ipc_connecter_t::start_connecting ()
{
    IPC_CONNECTER_DBG ("start_connecting: endpoint=%s", _endpoint_str.c_str ());

    if (_addr->resolved.ipc_addr != NULL) {
        LIBZMQ_DELETE (_addr->resolved.ipc_addr);
    }

    _addr->resolved.ipc_addr = new (std::nothrow) ipc_address_t ();
    alloc_assert (_addr->resolved.ipc_addr);

    int rc =
      _addr->resolved.ipc_addr->resolve (_addr->address.c_str ());
    if (rc != 0) {
        IPC_CONNECTER_DBG ("start_connecting: resolve failed");
        LIBZMQ_DELETE (_addr->resolved.ipc_addr);
        add_reconnect_timer ();
        return;
    }

    _endpoint = make_ipc_endpoint (*_addr->resolved.ipc_addr);

    boost::system::error_code ec;
    _socket.open (_endpoint.protocol (), ec);
    if (ec) {
        IPC_CONNECTER_DBG ("start_connecting: socket open failed: %s",
                           ec.message ().c_str ());
        add_reconnect_timer ();
        return;
    }

    _connecting = true;
    _socket.async_connect (_endpoint,
                           [this] (const boost::system::error_code &ec) {
                               on_connect (ec);
                           });

    add_connect_timer ();

    _socket_ptr->event_connect_delayed (
      make_unconnected_connect_endpoint_pair (_endpoint_str), 0);
}

void zmq::asio_ipc_connecter_t::on_connect (
  const boost::system::error_code &ec)
{
    _connecting = false;
    IPC_CONNECTER_DBG ("on_connect: ec=%s, terminating=%d",
                       ec.message ().c_str (), _terminating);

    if (_terminating)
        return;

    if (_connect_timer_started) {
        cancel_timer (connect_timer_id);
        _connect_timer_started = false;
    }

    if (ec) {
        if (ec == boost::asio::error::operation_aborted) {
            IPC_CONNECTER_DBG ("on_connect: operation aborted");
            return;
        }

        IPC_CONNECTER_DBG ("on_connect: connection failed: %s",
                           ec.message ().c_str ());
        close ();
        add_reconnect_timer ();
        return;
    }

    fd_t fd = _socket.native_handle ();
    IPC_CONNECTER_DBG ("on_connect: connected, fd=%d", fd);

    _socket.release ();

    std::string local_address =
      get_socket_name<ipc_address_t> (fd, socket_end_local);

    create_engine (fd, local_address);
}

void zmq::asio_ipc_connecter_t::add_connect_timer ()
{
    if (options.connect_timeout > 0) {
        IPC_CONNECTER_DBG ("add_connect_timer: timeout=%d",
                           options.connect_timeout);
        add_timer (options.connect_timeout, connect_timer_id);
        _connect_timer_started = true;
    }
}

void zmq::asio_ipc_connecter_t::add_reconnect_timer ()
{
    if (options.reconnect_ivl > 0) {
        const int interval = get_new_reconnect_ivl ();
        IPC_CONNECTER_DBG ("add_reconnect_timer: interval=%d", interval);
        add_timer (interval, reconnect_timer_id);
        _socket_ptr->event_connect_retried (
          make_unconnected_connect_endpoint_pair (_endpoint_str), interval);
        _reconnect_timer_started = true;
    }
}

int zmq::asio_ipc_connecter_t::get_new_reconnect_ivl ()
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

void zmq::asio_ipc_connecter_t::create_engine (fd_t fd_,
                                               const std::string &local_address_)
{
    IPC_CONNECTER_DBG ("create_engine: fd=%d, local=%s", fd_,
                       local_address_.c_str ());

    const endpoint_uri_pair_t endpoint_pair (local_address_, _endpoint_str,
                                             endpoint_type_connect);

    std::unique_ptr<i_asio_transport> transport (
      new (std::nothrow) ipc_transport_t ());
    alloc_assert (transport.get ());

    i_engine *engine =
      new (std::nothrow) asio_zmp_engine_t (fd_, options, endpoint_pair,
                                            std::move (transport));
    alloc_assert (engine);

    send_attach (_session, engine);

    terminate ();

    _socket_ptr->event_connected (endpoint_pair, fd_);
}

void zmq::asio_ipc_connecter_t::close ()
{
    IPC_CONNECTER_DBG ("close called");

    if (_socket.is_open ()) {
        fd_t fd = _socket.native_handle ();
        boost::system::error_code ec;
        _socket.close (ec);

        _socket_ptr->event_closed (
          make_unconnected_connect_endpoint_pair (_endpoint_str), fd);
    }
}

#endif  // ZMQ_IOTHREAD_POLLER_USE_ASIO && ZMQ_HAVE_IPC
