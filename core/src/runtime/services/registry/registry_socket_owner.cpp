/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/registry/registry.hpp"

#include "utils/random.hpp"
#include "sockets/common/socket_base.hpp"

namespace zlink
{
void registry_t::apply_socket_opts (socket_base_t *socket_,
                                    const std::vector<socket_opt_t> &opts_)
{
    if (!socket_)
        return;

    for (size_t i = 0; i < opts_.size (); ++i) {
        if (!opts_[i].value.empty ()) {
            socket_->setsockopt (opts_[i].option, &opts_[i].value[0],
                                 opts_[i].value.size ());
        }
    }
}

void registry_t::promote_runtime_sockets (socket_base_t *pub_,
                                          socket_base_t *router_,
                                          uint64_t now_ms_,
                                          socket_base_t **old_pub_out_,
                                          socket_base_t **old_router_out_)
{
    scoped_lock_t lock (_sync);
    if (old_pub_out_)
        *old_pub_out_ = static_cast<socket_base_t *> (_runtime_socket_state.pub_socket);
    if (old_router_out_)
        *old_router_out_ = static_cast<socket_base_t *> (_runtime_socket_state.router_socket);

    _runtime_socket_state.pub_socket = pub_;
    _runtime_socket_state.router_socket = router_;
    _lifecycle.register_socket (pub_);
    _lifecycle.register_socket (router_);

    if (!_coordination_state.registry_id_set) {
        _coordination_state.registry_id = zlink::generate_random ();
        if (_coordination_state.registry_id == 0)
            _coordination_state.registry_id = 1;
        _coordination_state.registry_id_set = true;
    }

    _runtime_socket_state.next_broadcast_ms = now_ms_ + _coordination_state.broadcast_interval_ms;
    _runtime_socket_state.last_sent_seq = _coordination_state.list_seq;
    _runtime_socket_state.next_socket_retry_ms = 0;
}

int registry_t::ensure_sockets ()
{
    zlink::clock_t clock;
    const uint64_t now = clock.now_ms ();
    socket_base_t *old_pub = NULL;
    socket_base_t *old_router = NULL;

    {
        scoped_lock_t lock (_sync);
        if (!_runtime_socket_state.started || _runtime_socket_state.stop.get () != 0)
            return -1;
        if (_runtime_socket_state.pub_socket && _runtime_socket_state.router_socket)
            return 0;
        if (now < _runtime_socket_state.next_socket_retry_ms)
            return -1;
        if (_endpoint_config.pub_endpoint.empty () || _endpoint_config.router_endpoint.empty ()) {
            errno = EINVAL;
            return -1;
        }
    }

    socket_base_t *pub = _ctx->create_socket (ZLINK_CORE_SOCKET_XPUB);
    socket_base_t *router = _ctx->create_socket (ZLINK_CORE_SOCKET_ROUTER);
    if (!pub || !router) {
        (void) _ctx->close_socket_and_wait (pub, 1000);
        (void) _ctx->close_socket_and_wait (router, 1000);
        scoped_lock_t lock (_sync);
        _runtime_socket_state.next_socket_retry_ms = now + 100;
        return -1;
    }

    {
        scoped_lock_t lock (_sync);
        apply_socket_opts (pub, _socket_option_state.pub_opts);
        apply_socket_opts (router, _socket_option_state.router_opts);
    }

    int verbose = 1;
    pub->setsockopt (ZLINK_INTERNAL_OPT_XPUB_VERBOSE, &verbose, sizeof (verbose));

    if (pub->bind (_endpoint_config.pub_endpoint.c_str ()) != 0
        || router->bind (_endpoint_config.router_endpoint.c_str ()) != 0) {
        (void) _ctx->close_socket_and_wait (router, 1000);
        (void) _ctx->close_socket_and_wait (pub, 1000);
        scoped_lock_t lock (_sync);
        _runtime_socket_state.next_socket_retry_ms = now + 100;
        return -1;
    }

    promote_runtime_sockets (pub, router, now, &old_pub, &old_router);
    (void) _lifecycle.close_socket_and_wait (old_pub, 1000);
    (void) _lifecycle.close_socket_and_wait (old_router, 1000);
    return 0;
}

void registry_t::close_sockets ()
{
    void *pub = NULL;
    void *router = NULL;
    void *peer_sub = NULL;
    std::string pub_endpoint;
    std::string router_endpoint;
    std::vector<std::string> peer_pubs;
    {
        scoped_lock_t lock (_sync);
        pub = _runtime_socket_state.pub_socket;
        router = _runtime_socket_state.router_socket;
        peer_sub = _runtime_socket_state.peer_sub_socket;
        pub_endpoint = _endpoint_config.pub_endpoint;
        router_endpoint = _endpoint_config.router_endpoint;
        peer_pubs = _endpoint_config.peer_pubs;
        _runtime_socket_state.pub_socket = NULL;
        _runtime_socket_state.router_socket = NULL;
        _runtime_socket_state.peer_sub_socket = NULL;
        _runtime_socket_state.peer_connected.clear ();
    }

    if (peer_sub) {
        for (size_t i = 0; i < peer_pubs.size (); ++i)
            zlink_disconnect (peer_sub, peer_pubs[i].c_str ());
        socket_base_t *peer_sub_socket = static_cast<socket_base_t *> (peer_sub);
        (void) _lifecycle.close_socket (peer_sub_socket);
    }
    if (router) {
        if (!router_endpoint.empty ())
            static_cast<socket_base_t *> (router)->term_endpoint (
              router_endpoint.c_str ());
        socket_base_t *router_socket = static_cast<socket_base_t *> (router);
        (void) _lifecycle.close_socket (router_socket);
    }
    if (pub) {
        if (!pub_endpoint.empty ())
            static_cast<socket_base_t *> (pub)->term_endpoint (
              pub_endpoint.c_str ());
        socket_base_t *pub_socket = static_cast<socket_base_t *> (pub);
        (void) _lifecycle.close_socket (pub_socket);
    }
    (void) _lifecycle.wait_drained (10000);
}

int registry_t::ensure_peer_sub_socket ()
{
    std::vector<socket_opt_t> peer_sub_opts;
    {
        scoped_lock_t lock (_sync);
        if (_runtime_socket_state.peer_sub_socket || _endpoint_config.peer_pubs.empty ())
            return 0;
        peer_sub_opts = _socket_option_state.peer_sub_opts;
    }

    socket_base_t *peer_sub_socket = _ctx->create_socket (ZLINK_CORE_SOCKET_SUB);
    if (!peer_sub_socket)
        return -1;

    apply_socket_opts (peer_sub_socket, peer_sub_opts);
    peer_sub_socket->setsockopt (ZLINK_INTERNAL_OPT_SUBSCRIBE, "", 0);

    {
        scoped_lock_t lock (_sync);
        if (_runtime_socket_state.peer_sub_socket == NULL) {
            _runtime_socket_state.peer_sub_socket = peer_sub_socket;
            _lifecycle.register_socket (peer_sub_socket);
            return 0;
        }
    }

    (void) _ctx->close_socket_and_wait (peer_sub_socket, 1000);
    return 0;
}

void registry_t::connect_peer_sub_endpoints (
  void *peer_sub_,
  const std::vector<std::string> &peer_pubs_)
{
    if (!peer_sub_)
        return;

    for (size_t i = 0; i < peer_pubs_.size (); ++i) {
        const std::string &endpoint = peer_pubs_[i];
        scoped_lock_t lock (_sync);
        if (_runtime_socket_state.peer_connected.find (endpoint) == _runtime_socket_state.peer_connected.end ()) {
            zlink_connect (peer_sub_, endpoint.c_str ());
            _runtime_socket_state.peer_connected.insert (endpoint);
        }
    }
}
}
