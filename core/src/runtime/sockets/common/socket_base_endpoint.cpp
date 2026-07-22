/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include <ctype.h>
#include <new>
#include <string>

#include "core/address.hpp"
#include "core/ctx.hpp"
#include "core/ctx_inproc_registry.hpp"
#include "core/io_thread.hpp"
#include "core/pipe.hpp"
#include "core/session_base.hpp"
#include "sockets/common/socket_base.hpp"
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

int zlink::socket_base_t::parse_uri (const char *uri_, std::string &scheme_, std::string &path_)
{
    zlink_assert (uri_ != NULL);

    const std::string uri (uri_);
    const std::string::size_type pos = uri.find ("://");
    if (pos == std::string::npos) {
        errno = EINVAL;
        return -1;
    }
    scheme_ = uri.substr (0, pos);
    path_ = uri.substr (pos + 3);

    if (scheme_.empty () || path_.empty ()) {
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
        && protocol_ != protocol_name::pgm && protocol_ != protocol_name::epgm
#endif
    ) {
        errno = EPROTONOSUPPORT;
        return -1;
    }

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
    if (parse_uri (endpoint_uri_, protocol, address) || check_protocol (protocol)) {
        return -1;
    }

    if (protocol == protocol_name::inproc)
        return bind_inproc_endpoint (endpoint_uri_);

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

    return bind_transport_listener (protocol, address, io_thread);
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
    if (parse_uri (endpoint_uri_, protocol, address) || check_protocol (protocol)) {
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
            new_pipes[0]->set_hwms_boost (peer.options.sndhwm, peer.options.rcvhwm);
            new_pipes[1]->set_hwms_boost (options.sndhwm, options.rcvhwm);
        }

        errno_assert (rc == 0);

        bool connected_inproc_now = false;

        if (!peer.socket) {
            send_routing_id (new_pipes[0], options);

            const endpoint_t endpoint (this, options);
            connected_inproc_now =
              pend_connection (std::string (endpoint_uri_), endpoint, new_pipes);
        } else {
            if (peer.options.recv_routing_id)
                send_routing_id (new_pipes[0], options);

            if (options.recv_routing_id)
                send_routing_id (new_pipes[1], peer.options);

            new_pipes[0]->set_peer_routing_id (peer.options.routing_id,
                                               peer.options.routing_id_size);
            new_pipes[1]->set_peer_routing_id (options.routing_id, options.routing_id_size);

            send_bind (peer.socket, new_pipes[1], false);
            peer.socket->emit_inproc_connection_ready (new_pipes[1]);
            connected_inproc_now = true;
        }

        attach_pipe (new_pipes[0], false, true);
        if (connected_inproc_now)
            emit_inproc_connection_ready (new_pipes[0]);

        endpoint_runtime ().set_last_endpoint (endpoint_uri_);
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

    address_t *paddr = new (std::nothrow) address_t (protocol, address, this->get_ctx ());
    alloc_assert (paddr);

    if (resolve_connect_address (protocol, address, paddr) != 0) {
        LIBZLINK_DELETE (paddr);
        return -1;
    }

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

    session_base_t *session = session_base_t::create (io_thread, true, this, options, paddr);
    errno_assert (session);

#ifdef ZLINK_HAVE_OPENPGM
    const bool subscribe_to_all = protocol == protocol_name::pgm || protocol == protocol_name::epgm;
#else
    const bool subscribe_to_all = false;
#endif
    pipe_t *newpipe = NULL;

    if (options.immediate != 1 || subscribe_to_all) {
        object_t *parents[2] = {this, session};
        pipe_t *new_pipes[2] = {NULL, NULL};

        const bool conflate = get_effective_conflate_option (options);
        int hwms[2] = {conflate ? -1 : options.sndhwm, conflate ? -1 : options.rcvhwm};
        bool conflates[2] = {conflate, conflate};
        //  Socket<->session pipes back one transport connection; use the
        //  small per-connection chunk granularity.
        rc = pipepair (parents, new_pipes, hwms, conflates, true);
        errno_assert (rc == 0);

        attach_pipe (new_pipes[0], subscribe_to_all, true);
        newpipe = new_pipes[0];
        session->attach_pipe (new_pipes[1]);
    }

    std::string last_endpoint;
    paddr->to_string (last_endpoint);
    endpoint_runtime ().set_last_endpoint (last_endpoint);
    add_endpoint (make_unconnected_connect_endpoint_pair (endpoint_uri_),
                  static_cast<own_t *> (session), newpipe);
    return 0;
}

void zlink::socket_base_t::socket_bound_endpoints (std::set<std::string> *out_) const
{
    if (!out_)
        return;

    out_->clear ();
    for (endpoints_t::const_iterator it = endpoint_runtime ().endpoints.begin (),
                                     end = endpoint_runtime ().endpoints.end ();
         it != end; ++it) {
        if (it->second.local_type == endpoint_type_bind)
            out_->insert (it->first);
    }
}

bool zlink::socket_base_t::socket_has_manual_connect_endpoints () const
{
    for (endpoints_t::const_iterator it = endpoint_runtime ().endpoints.begin (),
                                     end = endpoint_runtime ().endpoints.end ();
         it != end; ++it) {
        if (it->second.local_type == endpoint_type_connect)
            return true;
    }
    return false;
}

bool zlink::socket_base_t::socket_has_attached_pipes () const
{
    return has_attached_pipes ();
}

std::string zlink::socket_base_t::resolve_tcp_addr (std::string endpoint_uri_pair_,
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
      endpoint_pair_.identifier (), endpoint_pipe_t (endpoint_, pipe_, endpoint_pair_.local_type));

    if (pipe_ != NULL) {
        endpoint_uri_pair_t pipe_endpoint_pair = endpoint_pair_;
        //  add_endpoint receives the placeholder made before a connect
        //  attempt has a physical transport. Keep endpoint bookkeeping on
        //  the pipe, but leave its shared transport identity unbound until
        //  session_base_t installs the engine endpoint.
        pipe_endpoint_pair.connection_id = 0;
        pipe_->set_endpoint_pair (ZLINK_MOVE (pipe_endpoint_pair));
    }
}

int zlink::socket_base_t::term_endpoint_internal (const char *endpoint_uri_)
{
    if (unlikely (_ctx_terminated)) {
        errno = ETERM;
        return -1;
    }

    if (unlikely (!endpoint_uri_)) {
        errno = EINVAL;
        return -1;
    }

    const int rc = process_commands (0, false);
    if (unlikely (rc != 0))
        return -1;

    std::string uri_protocol;
    std::string uri_path;
    if (parse_uri (endpoint_uri_, uri_protocol, uri_path) || check_protocol (uri_protocol)) {
        return -1;
    }

    const std::string endpoint_uri_str = std::string (endpoint_uri_);
    if (uri_protocol == protocol_name::inproc) {
        //  A connect that is still pending (no binder yet) parks its peer
        //  pipe in the context registry where no socket ever runs the pipe
        //  termination handshake. Materialize it into a short-lived PAIR
        //  socket first so both pipe halves can terminate and this socket
        //  can be reaped before ctx term. Best effort: if the context cannot
        //  supply the helper socket (socket limit reached or termination
        //  already started), keep the previous disconnect semantics and fall
        //  through to the local cleanup.
        (void) get_ctx ()->materialize_pending_inproc (endpoint_uri_str, this);
        const int inproc_rc = unregister_endpoint (endpoint_uri_str, this) == 0
                                ? 0
                                : endpoint_runtime ().inprocs.erase_pipes (endpoint_uri_str);
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
        return -1;
    }

    for (endpoints_t::iterator it = range.first; it != range.second; ++it) {
        if (it->second.pipe != NULL)
            it->second.pipe->terminate (false);
        term_child (it->second.endpoint);
    }

    for (size_t i = 0, size = endpoint_runtime ().attached_pipe_count (); i != size; ++i) {
        pipe_t *const pipe = endpoint_runtime ().attached_pipe (i);
        if (!pipe)
            continue;
        if (pipe->get_endpoint_pair ().identifier () == resolved_endpoint_uri)
            pipe->terminate (false);
    }
    endpoint_runtime ().endpoints.erase (range.first, range.second);
    return 0;
}

int zlink::socket_base_t::term_endpoint (const char *endpoint_uri_)
{
    socket_public_api_scope_t admission (lifecycle_coordinator ());
    if (!admission.acquired ())
        return -1;
    return term_endpoint_internal (endpoint_uri_);
}

int zlink::socket_base_t::term_peer_rid (const zlink_routing_id_t *peer_rid_)
{
    if (!peer_rid_ || peer_rid_->size == 0) {
        errno = EINVAL;
        return -1;
    }

    socket_public_api_scope_t admission (lifecycle_coordinator ());
    if (!admission.acquired ())
        return -1;

    if (unlikely (_ctx_terminated)) {
        errno = ETERM;
        return -1;
    }

    const int rc = process_commands (0, false);
    if (unlikely (rc != 0))
        return -1;

    return xterm_peer_rid (peer_rid_);
}
