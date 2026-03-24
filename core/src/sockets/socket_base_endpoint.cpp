/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include <ctype.h>
#include <new>
#include <string>

#include "core/address.hpp"
#include "core/io_thread.hpp"
#include "core/pipe.hpp"
#include "core/session_base.hpp"
#include "sockets/socket_base.hpp"
#include "transports/ipc/ipc_address.hpp"
#include "transports/tcp/tcp_address.hpp"

// ASIO-only build: transport listeners are always included.
#include "transports/tcp/asio_tcp_listener.hpp"
#if defined ZLINK_HAVE_IPC
#include "transports/ipc/asio_ipc_listener.hpp"
#endif
#if defined ZLINK_HAVE_ASIO_SSL
#include "transports/tls/asio_tls_listener.hpp"
#endif

#if defined ZLINK_HAVE_WS
#include "transports/ws/asio_ws_listener.hpp"
#include "transports/ws/ws_address.hpp"
#endif
#ifdef ZLINK_HAVE_WSS
#include "transports/tls/wss_address.hpp"
#endif

#ifdef ZLINK_HAVE_OPENPGM
#include "transports/pgm/pgm_socket.hpp"
#endif

// TODO consider renaming protocol_ to scheme_ in conformance with RFC 3986
// terminology, but this requires extensive changes to be consistent
int zlink::socket_base_t::parse_uri (const char *uri_,
                                     std::string &protocol_,
                                     std::string &path_)
{
    zlink_assert (uri_ != NULL);

    const std::string uri (uri_);
    const std::string::size_type pos = uri.find ("://");
    if (pos == std::string::npos) {
        errno = EINVAL;
        return -1;
    }
    protocol_ = uri.substr (0, pos);
    path_ = uri.substr (pos + 3);

    if (protocol_.empty () || path_.empty ()) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int zlink::socket_base_t::check_protocol (const std::string &protocol_) const
{
    //  First check out whether the protocol is something we are aware of.
    if (protocol_ != protocol_name::inproc
#if defined ZLINK_HAVE_IPC
        && protocol_ != protocol_name::ipc
#endif
        && protocol_ != protocol_name::tcp
#ifdef ZLINK_HAVE_WS
        && protocol_ != protocol_name::ws
#endif
#ifdef ZLINK_HAVE_WSS
        && protocol_ != protocol_name::wss
#endif
#ifdef ZLINK_HAVE_TLS
        && protocol_ != protocol_name::tls
#endif
#ifdef ZLINK_HAVE_OPENPGM
        && protocol_ != protocol_name::pgm
        && protocol_ != protocol_name::epgm
#endif
    ) {
        errno = EPROTONOSUPPORT;
        return -1;
    }

#ifdef ZLINK_HAVE_OPENPGM
    //  PGM/EPGM is temporarily disabled for PUB/SUB family in zlink.
    if (protocol_ == protocol_name::pgm || protocol_ == protocol_name::epgm) {
        errno = EPROTONOSUPPORT;
        return -1;
    }
#endif

    return 0;
}

int zlink::socket_base_t::bind (const char *endpoint_uri_)
{
    if (unlikely (_ctx_terminated)) {
        errno = ETERM;
        return -1;
    }

    int rc = process_commands (0, false);
    if (unlikely (rc != 0))
        return -1;

    std::string protocol;
    std::string address;
    if (parse_uri (endpoint_uri_, protocol, address)
        || check_protocol (protocol)) {
        return -1;
    }

    if (protocol == protocol_name::inproc) {
        const endpoint_t endpoint = {this, options};
        rc = register_endpoint (endpoint_uri_, endpoint);
        if (rc == 0) {
            connect_pending (endpoint_uri_, this);
            _last_endpoint.assign (endpoint_uri_);
            options.connected = true;
        }
        return rc;
    }

#ifdef ZLINK_HAVE_OPENPGM
    if (protocol == protocol_name::pgm || protocol == protocol_name::epgm) {
        rc = connect (endpoint_uri_);
        if (rc != -1)
            options.connected = true;
        return rc;
    }
#endif

    io_thread_t *io_thread = choose_io_thread (options.affinity);
    if (!io_thread) {
        errno = EMTHREAD;
        return -1;
    }

    if (protocol == protocol_name::tcp) {
        asio_tcp_listener_t *listener =
          new (std::nothrow) asio_tcp_listener_t (io_thread, this, options);
        alloc_assert (listener);
        rc = listener->set_local_address (address.c_str ());
        if (rc != 0) {
            LIBZLINK_DELETE (listener);
            event_bind_failed (make_unconnected_bind_endpoint_pair (address),
                               zlink_errno ());
            return -1;
        }

        listener->get_local_address (_last_endpoint);
        add_endpoint (make_unconnected_bind_endpoint_pair (_last_endpoint),
                      static_cast<own_t *> (listener), NULL);
        options.connected = true;
        return 0;
    }

#if defined ZLINK_HAVE_TLS && defined ZLINK_HAVE_ASIO_SSL
    if (protocol == protocol_name::tls) {
        asio_tls_listener_t *listener =
          new (std::nothrow) asio_tls_listener_t (io_thread, this, options);
        alloc_assert (listener);
        rc = listener->set_local_address (address.c_str ());
        if (rc != 0) {
            LIBZLINK_DELETE (listener);
            event_bind_failed (make_unconnected_bind_endpoint_pair (address),
                               zlink_errno ());
            return -1;
        }

        listener->get_local_address (_last_endpoint);
        add_endpoint (make_unconnected_bind_endpoint_pair (_last_endpoint),
                      static_cast<own_t *> (listener), NULL);
        options.connected = true;
        return 0;
    }
#endif

#if defined ZLINK_HAVE_IPC
    if (protocol == protocol_name::ipc) {
        asio_ipc_listener_t *listener =
          new (std::nothrow) asio_ipc_listener_t (io_thread, this, options);
        alloc_assert (listener);
        rc = listener->set_local_address (address.c_str ());
        if (rc != 0) {
            LIBZLINK_DELETE (listener);
            event_bind_failed (make_unconnected_bind_endpoint_pair (address),
                               zlink_errno ());
            return -1;
        }

        listener->get_local_address (_last_endpoint);
        add_endpoint (make_unconnected_bind_endpoint_pair (_last_endpoint),
                      static_cast<own_t *> (listener), NULL);
        options.connected = true;
        return 0;
    }
#endif

#if defined ZLINK_HAVE_WS
    if (protocol == protocol_name::ws
#if defined ZLINK_HAVE_WSS
        || protocol == protocol_name::wss
#endif
    ) {
        const bool secure =
#if defined ZLINK_HAVE_WSS
          protocol == protocol_name::wss;
#else
          false;
#endif

        ws_address_t *ws_addr =
#if defined ZLINK_HAVE_WSS
          secure ? static_cast<ws_address_t *> (new (std::nothrow) wss_address_t ())
                 :
#endif
                 new (std::nothrow) ws_address_t ();
        alloc_assert (ws_addr);
        rc = ws_addr->resolve (address.c_str (), true, options.ipv6);
        if (rc != 0) {
            LIBZLINK_DELETE (ws_addr);
            event_bind_failed (make_unconnected_bind_endpoint_pair (address),
                               zlink_errno ());
            return -1;
        }

        asio_ws_listener_t *listener =
          new (std::nothrow) asio_ws_listener_t (io_thread, this, options);
        alloc_assert (listener);
        rc = listener->set_local_address (ws_addr, secure);
        LIBZLINK_DELETE (ws_addr);
        if (rc != 0) {
            LIBZLINK_DELETE (listener);
            event_bind_failed (make_unconnected_bind_endpoint_pair (address),
                               zlink_errno ());
            return -1;
        }

        listener->get_local_address (_last_endpoint);
        add_endpoint (make_unconnected_bind_endpoint_pair (_last_endpoint),
                      static_cast<own_t *> (listener), NULL);
        options.connected = true;
        return 0;
    }
#endif

    zlink_assert (false);
    return -1;
}

int zlink::socket_base_t::connect (const char *endpoint_uri_)
{
    return connect_internal (endpoint_uri_);
}

int zlink::socket_base_t::connect_internal (const char *endpoint_uri_)
{
    if (unlikely (_ctx_terminated)) {
        errno = ETERM;
        return -1;
    }

    int rc = process_commands (0, false);
    if (unlikely (rc != 0))
        return -1;

    std::string protocol;
    std::string address;
    if (parse_uri (endpoint_uri_, protocol, address)
        || check_protocol (protocol)) {
        return -1;
    }

    if (options.type == ZLINK_CORE_SOCKET_STREAM) {
        errno = EOPNOTSUPP;
        return -1;
    }

    if (protocol == protocol_name::inproc) {
        const endpoint_t peer = find_endpoint (endpoint_uri_);

        const int sndhwm = peer.socket == NULL ? options.sndhwm
                           : options.sndhwm != 0 && peer.options.rcvhwm != 0
                             ? options.sndhwm + peer.options.rcvhwm
                             : 0;
        const int rcvhwm = peer.socket == NULL ? options.rcvhwm
                           : options.rcvhwm != 0 && peer.options.sndhwm != 0
                             ? options.rcvhwm + peer.options.sndhwm
                             : 0;

        object_t *parents[2] = {this, peer.socket == NULL ? this : peer.socket};
        pipe_t *new_pipes[2] = {NULL, NULL};

        const bool conflate = get_effective_conflate_option (options);

        int hwms[2] = {conflate ? -1 : sndhwm, conflate ? -1 : rcvhwm};
        bool conflates[2] = {conflate, conflate};
        rc = pipepair (parents, new_pipes, hwms, conflates);
        if (!conflate) {
            new_pipes[0]->set_hwms_boost (peer.options.sndhwm,
                                          peer.options.rcvhwm);
            new_pipes[1]->set_hwms_boost (options.sndhwm, options.rcvhwm);
        }

        errno_assert (rc == 0);

        bool connected_inproc_now = false;

        if (!peer.socket) {
            send_routing_id (new_pipes[0], options);

            const endpoint_t endpoint = {this, options};
            connected_inproc_now =
              pend_connection (std::string (endpoint_uri_), endpoint, new_pipes);
        } else {
            if (peer.options.recv_routing_id)
                send_routing_id (new_pipes[0], options);

            if (options.recv_routing_id)
                send_routing_id (new_pipes[1], peer.options);

            new_pipes[0]->set_peer_routing_id (peer.options.routing_id,
                                               peer.options.routing_id_size);
            new_pipes[1]->set_peer_routing_id (options.routing_id,
                                               options.routing_id_size);

            send_bind (peer.socket, new_pipes[1], false);
            peer.socket->emit_inproc_connection_ready (new_pipes[1]);
            connected_inproc_now = true;
        }

        attach_pipe (new_pipes[0], false, true);
        if (connected_inproc_now)
            emit_inproc_connection_ready (new_pipes[0]);

        _last_endpoint.assign (endpoint_uri_);
        endpoint_runtime ().inprocs.emplace (endpoint_uri_, new_pipes[0]);
        options.connected = true;
        return 0;
    }
    if (unlikely (0 != endpoint_runtime ().endpoints.count (endpoint_uri_)))
        return 0;

    io_thread_t *io_thread = choose_io_thread (options.affinity);
    if (!io_thread) {
        errno = EMTHREAD;
        return -1;
    }

    address_t *paddr =
      new (std::nothrow) address_t (protocol, address, this->get_ctx ());
    alloc_assert (paddr);

    if (protocol == protocol_name::tcp
#ifdef ZLINK_HAVE_TLS
        || protocol == protocol_name::tls
#endif
    ) {
        const char *check = address.c_str ();
        if (isalnum (*check) || isxdigit (*check) || *check == '['
            || *check == ':') {
            check++;
            while (isalnum (*check) || isxdigit (*check) || *check == '.'
                   || *check == '-' || *check == ':' || *check == '%'
                   || *check == ';' || *check == '[' || *check == ']'
                   || *check == '_' || *check == '*') {
                check++;
            }
        }
        rc = -1;
        if (*check == 0) {
            check = strrchr (address.c_str (), ':');
            if (check) {
                check++;
                if (*check && isdigit (*check))
                    rc = 0;
            }
        }
        if (rc == -1) {
            errno = EINVAL;
            LIBZLINK_DELETE (paddr);
            return -1;
        }
        paddr->resolved.tcp_addr = NULL;
    }
#ifdef ZLINK_HAVE_WS
#ifdef ZLINK_HAVE_WSS
    else if (protocol == protocol_name::ws || protocol == protocol_name::wss) {
        if (protocol == protocol_name::wss) {
            paddr->resolved.wss_addr = new (std::nothrow) wss_address_t ();
            alloc_assert (paddr->resolved.wss_addr);
            rc = paddr->resolved.wss_addr->resolve (address.c_str (), false,
                                                    options.ipv6);
        } else
#else
    else if (protocol == protocol_name::ws) {
#endif
        {
            paddr->resolved.ws_addr = new (std::nothrow) ws_address_t ();
            alloc_assert (paddr->resolved.ws_addr);
            rc = paddr->resolved.ws_addr->resolve (address.c_str (), false,
                                                   options.ipv6);
        }

        if (rc != 0) {
            LIBZLINK_DELETE (paddr);
            return -1;
        }
    }
#endif

#if defined ZLINK_HAVE_IPC
    else if (protocol == protocol_name::ipc) {
        paddr->resolved.ipc_addr = new (std::nothrow) ipc_address_t ();
        alloc_assert (paddr->resolved.ipc_addr);
        rc = paddr->resolved.ipc_addr->resolve (address.c_str ());
        if (rc != 0) {
            LIBZLINK_DELETE (paddr);
            return -1;
        }
    }
#endif

#ifdef ZLINK_HAVE_OPENPGM
    if (protocol == protocol_name::pgm || protocol == protocol_name::epgm) {
        struct pgm_addrinfo_t *res = NULL;
        uint16_t port_number = 0;
        rc = pgm_socket_t::init_address (address.c_str (), &res, &port_number);
        if (res != NULL)
            pgm_freeaddrinfo (res);
        if (rc != 0 || port_number == 0) {
            LIBZLINK_DELETE (paddr);
            return -1;
        }
    }
#endif

    session_base_t *session =
      session_base_t::create (io_thread, true, this, options, paddr);
    errno_assert (session);

#ifdef ZLINK_HAVE_OPENPGM
    const bool subscribe_to_all = protocol == protocol_name::pgm
                                  || protocol == protocol_name::epgm;
#else
    const bool subscribe_to_all = false;
#endif
    pipe_t *newpipe = NULL;

    if (options.immediate != 1 || subscribe_to_all) {
        object_t *parents[2] = {this, session};
        pipe_t *new_pipes[2] = {NULL, NULL};

        const bool conflate = get_effective_conflate_option (options);
        int hwms[2] = {conflate ? -1 : options.sndhwm,
                       conflate ? -1 : options.rcvhwm};
        bool conflates[2] = {conflate, conflate};
        rc = pipepair (parents, new_pipes, hwms, conflates);
        errno_assert (rc == 0);

        attach_pipe (new_pipes[0], subscribe_to_all, true);
        newpipe = new_pipes[0];
        session->attach_pipe (new_pipes[1]);
    }

    paddr->to_string (_last_endpoint);
    add_endpoint (make_unconnected_connect_endpoint_pair (endpoint_uri_),
                  static_cast<own_t *> (session), newpipe);
    return 0;
}

std::string zlink::socket_base_t::resolve_tcp_addr (
  std::string endpoint_uri_pair_,
  const char *tcp_address_)
{
    if (endpoint_runtime ().endpoints.find (endpoint_uri_pair_)
        == endpoint_runtime ().endpoints.end ()) {
        tcp_address_t *tcp_addr = new (std::nothrow) tcp_address_t ();
        alloc_assert (tcp_addr);
        int rc = tcp_addr->resolve (tcp_address_, false, options.ipv6);

        if (rc == 0) {
            tcp_addr->to_string (endpoint_uri_pair_);
            if (endpoint_runtime ().endpoints.find (endpoint_uri_pair_)
                == endpoint_runtime ().endpoints.end ()) {
                rc = tcp_addr->resolve (tcp_address_, true, options.ipv6);
                if (rc == 0)
                    tcp_addr->to_string (endpoint_uri_pair_);
            }
        }
        LIBZLINK_DELETE (tcp_addr);
    }
    return endpoint_uri_pair_;
}

void zlink::socket_base_t::add_endpoint (const endpoint_uri_pair_t &endpoint_pair_,
                                         own_t *endpoint_,
                                         pipe_t *pipe_)
{
    launch_child (endpoint_);
    endpoint_runtime ().endpoints.ZLINK_MAP_INSERT_OR_EMPLACE (
      endpoint_pair_.identifier (), endpoint_pipe_t (endpoint_, pipe_));

    if (pipe_ != NULL)
        pipe_->set_endpoint_pair (endpoint_pair_);
}

int zlink::socket_base_t::term_endpoint (const char *endpoint_uri_)
{
    if (!enter_public_api ())
        return -1;

    if (unlikely (_ctx_terminated)) {
        errno = ETERM;
        leave_public_api ();
        return -1;
    }

    if (unlikely (!endpoint_uri_)) {
        errno = EINVAL;
        leave_public_api ();
        return -1;
    }

    const int rc = process_commands (0, false);
    if (unlikely (rc != 0)) {
        leave_public_api ();
        return -1;
    }

    std::string uri_protocol;
    std::string uri_path;
    if (parse_uri (endpoint_uri_, uri_protocol, uri_path)
        || check_protocol (uri_protocol)) {
        leave_public_api ();
        return -1;
    }

    const std::string endpoint_uri_str = std::string (endpoint_uri_);
    if (uri_protocol == protocol_name::inproc) {
        const int inproc_rc = unregister_endpoint (endpoint_uri_str, this) == 0
                                ? 0
                                : endpoint_runtime ().inprocs.erase_pipes (
                                    endpoint_uri_str);
        leave_public_api ();
        return inproc_rc;
    }

    const std::string resolved_endpoint_uri =
      (uri_protocol == protocol_name::tcp
#ifdef ZLINK_HAVE_TLS
       || uri_protocol == protocol_name::tls
#endif
      )
        ? resolve_tcp_addr (endpoint_uri_str, uri_path.c_str ())
        : endpoint_uri_str;

    const std::pair<endpoints_t::iterator, endpoints_t::iterator> range =
      endpoint_runtime ().endpoints.equal_range (resolved_endpoint_uri);
    if (range.first == range.second) {
        errno = ENOENT;
        leave_public_api ();
        return -1;
    }

    for (endpoints_t::iterator it = range.first; it != range.second; ++it) {
        if (it->second.second != NULL)
            it->second.second->terminate (false);
        term_child (it->second.first);
    }

    for (pipes_t::size_type i = 0; i < _pipes.size (); ++i) {
        pipe_t *const pipe = _pipes[i];
        if (!pipe)
            continue;
        if (pipe->get_endpoint_pair ().identifier () == resolved_endpoint_uri)
            pipe->terminate (false);
    }
    endpoint_runtime ().endpoints.erase (range.first, range.second);
    leave_public_api ();
    return 0;
}
