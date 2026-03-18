/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "core/recv_internal.hpp"
#include "services/discovery/registry.hpp"
#include "services/discovery/discovery_protocol.hpp"
#include "services/control/service_control_runtime.hpp"
#include "sockets/socket_base.hpp"

#include "utils/err.hpp"
#include "utils/random.hpp"

#include <algorithm>
#include <set>
#include <vector>
#include <cstdio>

namespace zlink
{
static const uint32_t registry_tag_value = 0x1e6700d5;

static void registry_debug (const char *msg_)
{
    if (std::getenv ("ZLINK_REGISTRY_DEBUG"))
        std::fprintf (stderr, "[registry] %s\n", msg_ ? msg_ : "");
}

static void registry_debug_rid (const char *label_,
                                const zlink_routing_id_t &rid_)
{
    if (!std::getenv ("ZLINK_REGISTRY_DEBUG"))
        return;
    std::fprintf (stderr, "[registry] %s rid(size=%u):", label_,
                  static_cast<unsigned int> (rid_.size));
    for (uint8_t i = 0; i < rid_.size; ++i)
        std::fprintf (stderr, " %02x",
                      static_cast<unsigned int> (rid_.data[i]));
    std::fprintf (stderr, "\n");
}

static std::string topology_routing_key (const zlink_routing_id_t &rid_)
{
    if (rid_.size == 0)
        return std::string ();
    return std::string (reinterpret_cast<const char *> (rid_.data), rid_.size);
}

static bool is_supported_registry_transport (const char *endpoint_)
{
    if (!endpoint_ || endpoint_[0] == '\0')
        return false;

    const char *scheme_end = strstr (endpoint_, "://");
    if (!scheme_end)
        return false;

    const size_t scheme_len = static_cast<size_t> (scheme_end - endpoint_);
    return (scheme_len == 3 && strncmp (endpoint_, "tcp", 3) == 0)
           || (scheme_len == 2 && strncmp (endpoint_, "ws", 2) == 0)
           || (scheme_len == 3 && strncmp (endpoint_, "wss", 3) == 0)
           || (scheme_len == 3 && strncmp (endpoint_, "tls", 3) == 0);
}

static bool topology_filter_match (
  const zlink_registry_topology_entry_t &entry_,
  const zlink_registry_topology_filter_t *filter_)
{
    if (!filter_)
        return true;
    if (filter_->service_kind != 0 && filter_->service_kind != entry_.service_kind)
        return false;
    if (filter_->state != 0 && filter_->state != entry_.state)
        return false;
    if (filter_->source != 0 && filter_->source != entry_.source)
        return false;
    if (filter_->service_name[0] != '\0'
        && strcmp (filter_->service_name, entry_.service_name) != 0)
        return false;
    if (filter_->routing_id.size > 0) {
        if (filter_->routing_id.size != entry_.routing_id.size)
            return false;
        if (memcmp (filter_->routing_id.data, entry_.routing_id.data,
                    filter_->routing_id.size)
            != 0)
            return false;
    }
    return true;
}

static bool gateway_peer_filter_match (
  const zlink_registry_gateway_peer_entry_t &entry_,
  const zlink_registry_gateway_peer_filter_t *filter_)
{
    if (!filter_)
        return true;
    if (filter_->state != 0 && filter_->state != entry_.state)
        return false;
    if (filter_->service_name[0] != '\0'
        && strcmp (filter_->service_name, entry_.service_name) != 0) {
        return false;
    }
    if (filter_->gateway_routing_id.size > 0) {
        if (filter_->gateway_routing_id.size != entry_.gateway_routing_id.size)
            return false;
        if (memcmp (filter_->gateway_routing_id.data,
                    entry_.gateway_routing_id.data,
                    filter_->gateway_routing_id.size)
            != 0) {
            return false;
        }
    }
    if (filter_->peer_routing_id.size > 0) {
        if (filter_->peer_routing_id.size != entry_.peer_routing_id.size)
            return false;
        if (memcmp (filter_->peer_routing_id.data, entry_.peer_routing_id.data,
                    filter_->peer_routing_id.size)
            != 0) {
            return false;
        }
    }
    return true;
}

registry_t::registry_t (ctx_t *ctx_) :
    _ctx (ctx_),
    _tag (registry_tag_value),
    _lifecycle (ctx_),
    _registry_id (0),
    _registry_id_set (false),
    _list_seq (0),
    _heartbeat_interval_ms (5000),
    _heartbeat_timeout_ms (15000),
    _broadcast_interval_ms (30000),
    _stop (0),
    _task_id (0),
    _pub_socket (NULL),
    _router_socket (NULL),
    _peer_sub_socket (NULL),
    _next_broadcast_ms (0),
    _last_sent_seq (0),
    _started (false),
    _next_socket_retry_ms (0)
{
    zlink_assert (_ctx);

    // Default to failing undeliverable registry replies instead of
    // silently dropping them on the ROUTER socket.
    socket_opt_t mandatory_opt;
    mandatory_opt.option = ZLINK_ROUTER_MANDATORY;
    mandatory_opt.value.resize (sizeof (int));
    const int mandatory = 1;
    memcpy (&mandatory_opt.value[0], &mandatory, sizeof (mandatory));
    _router_opts.push_back (mandatory_opt);
}

registry_t::~registry_t ()
{
    _tag = 0xdeadbeef;
}

bool registry_t::check_tag () const
{
    return _tag == registry_tag_value;
}

int registry_t::bind (const char *pub_endpoint_,
                      const char *router_endpoint_)
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return -1;
    if (!pub_endpoint_ || !router_endpoint_ || pub_endpoint_[0] == '\0'
        || router_endpoint_[0] == '\0') {
        errno = EINVAL;
        return -1;
    }

    {
        scoped_lock_t lock (_sync);
        if (_started) {
            errno = EBUSY;
            return -1;
        }
        _pub_endpoint = pub_endpoint_;
        _router_endpoint = router_endpoint_;
    }

    return start ();
}

int registry_t::set_id (uint32_t registry_id_)
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return -1;
    scoped_lock_t lock (_sync);
    _registry_id = registry_id_;
    _registry_id_set = true;
    return 0;
}

int registry_t::add_peer (const char *peer_pub_endpoint_)
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return -1;
    if (!peer_pub_endpoint_) {
        errno = EINVAL;
        return -1;
    }
    if (!is_supported_registry_transport (peer_pub_endpoint_)) {
        errno = EPROTONOSUPPORT;
        return -1;
    }
    scoped_lock_t lock (_sync);
    _peer_pubs.push_back (peer_pub_endpoint_);
    return 0;
}

int registry_t::set_heartbeat (uint32_t interval_ms_, uint32_t timeout_ms_)
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return -1;
    if (interval_ms_ == 0 || timeout_ms_ == 0) {
        errno = EINVAL;
        return -1;
    }
    scoped_lock_t lock (_sync);
    _heartbeat_interval_ms = interval_ms_;
    _heartbeat_timeout_ms = timeout_ms_;
    return 0;
}

int registry_t::set_broadcast_interval (uint32_t interval_ms_)
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return -1;
    if (interval_ms_ == 0) {
        errno = EINVAL;
        return -1;
    }
    scoped_lock_t lock (_sync);
    _broadcast_interval_ms = interval_ms_;
    return 0;
}

int registry_t::set_socket_option (int socket_role_,
                                   int option_,
                                   const void *optval_,
                                   size_t optvallen_)
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return -1;
    if (!optval_ || optvallen_ == 0) {
        errno = EINVAL;
        return -1;
    }

    scoped_lock_t lock (_sync);
    std::vector<socket_opt_t> *opts = NULL;
    void *existing_socket = NULL;
    switch (socket_role_) {
        case ZLINK_REGISTRY_SOCKET_PUB:
            opts = &_pub_opts;
            existing_socket = _pub_socket;
            break;
        case ZLINK_REGISTRY_SOCKET_ROUTER:
            opts = &_router_opts;
            existing_socket = _router_socket;
            break;
        case ZLINK_REGISTRY_SOCKET_PEER_SUB:
            opts = &_peer_sub_opts;
            existing_socket = _peer_sub_socket;
            break;
        default:
            errno = EINVAL;
            return -1;
    }
    for (size_t i = 0; i < opts->size (); ++i) {
        if ((*opts)[i].option == option_) {
            (*opts)[i].value.assign (
              static_cast<const unsigned char *> (optval_),
              static_cast<const unsigned char *> (optval_) + optvallen_);
            if (existing_socket)
                zlink_setsockopt (
                  existing_socket, static_cast<zlink_socket_option_t> (option_),
                  optval_, optvallen_);
            return 0;
        }
    }
    socket_opt_t opt;
    opt.option = option_;
    opt.value.assign (static_cast<const unsigned char *> (optval_),
                      static_cast<const unsigned char *> (optval_)
                        + optvallen_);
    opts->push_back (opt);
    if (existing_socket)
        zlink_setsockopt (
          existing_socket, static_cast<zlink_socket_option_t> (option_),
          optval_, optvallen_);
    return 0;
}

int registry_t::topology_snapshot (zlink_registry_topology_entry_t *entries_,
                                   size_t *count_)
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return -1;
    return topology_query (NULL, entries_, count_);
}

int registry_t::topology_query (const zlink_registry_topology_filter_t *filter_,
                                zlink_registry_topology_entry_t *entries_,
                                size_t *count_)
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return -1;
    if (!count_) {
        errno = EINVAL;
        return -1;
    }

    std::vector<zlink_registry_topology_entry_t> matched;
    {
        scoped_lock_t lock (_sync);
        for (std::map<topology_key_t, topology_entry_t>::const_iterator it =
               _topology.begin ();
             it != _topology.end (); ++it) {
            if (topology_filter_match (it->second.entry, filter_))
                matched.push_back (it->second.entry);
        }
    }

    if (!entries_) {
        *count_ = matched.size ();
        return 0;
    }
    if (*count_ < matched.size ()) {
        *count_ = matched.size ();
        errno = ENOBUFS;
        return -1;
    }
    for (size_t i = 0; i < matched.size (); ++i)
        entries_[i] = matched[i];
    *count_ = matched.size ();
    return 0;
}

int registry_t::gateway_peers_snapshot (
  zlink_registry_gateway_peer_entry_t *entries_,
  size_t *count_)
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return -1;
    return gateway_peers_query (NULL, entries_, count_);
}

int registry_t::gateway_peers_query (
  const zlink_registry_gateway_peer_filter_t *filter_,
  zlink_registry_gateway_peer_entry_t *entries_,
  size_t *count_)
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return -1;
    if (!count_) {
        errno = EINVAL;
        return -1;
    }

    std::vector<zlink_registry_gateway_peer_entry_t> matched;
    {
        scoped_lock_t lock (_sync);
        for (std::map<gateway_peer_key_t, gateway_peer_entry_t>::const_iterator it =
               _gateway_peers.begin ();
             it != _gateway_peers.end (); ++it) {
            if (gateway_peer_filter_match (it->second.entry, filter_))
                matched.push_back (it->second.entry);
        }
    }

    if (!entries_) {
        *count_ = matched.size ();
        return 0;
    }
    if (*count_ < matched.size ()) {
        *count_ = matched.size ();
        errno = ENOBUFS;
        return -1;
    }
    for (size_t i = 0; i < matched.size (); ++i)
        entries_[i] = matched[i];
    *count_ = matched.size ();
    return 0;
}

int registry_t::start ()
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return -1;
    {
        scoped_lock_t lock (_sync);
        if (_pub_endpoint.empty () || _router_endpoint.empty ()) {
            errno = EINVAL;
            return -1;
        }
        if (_started) {
            errno = EBUSY;
            return -1;
        }
        _stop.set (0);
        _started = true;
        _next_broadcast_ms = 0;
        _last_sent_seq = _list_seq;
    }

    // Ensure bind succeeds before reporting start success. With async-only
    // startup this could return 0 even when endpoints are busy, which leaves
    // clients blocked in register-ack waits.
    if (ensure_sockets () != 0) {
        scoped_lock_t lock (_sync);
        _started = false;
        return -1;
    }

    service_control_runtime_t *runtime = _ctx->service_control_runtime ();
    if (!runtime) {
        errno = ENOTSUP;
        close_sockets ();
        scoped_lock_t lock (_sync);
        _started = false;
        return -1;
    }

    _task_id = runtime->add_periodic_task (control_task, this, 1, true);
    if (_task_id == 0) {
        close_sockets ();
        scoped_lock_t lock (_sync);
        _started = false;
        return -1;
    }
    return 0;
}

int registry_t::destroy ()
{
    if (!_public_api.begin_close_or_fail_busy ())
        return -1;
    _stop.set (1);
    service_control_runtime_t *runtime = _ctx->service_control_runtime ();
    if (runtime && _task_id != 0)
        runtime->remove_task (_task_id);
    _task_id = 0;
    close_sockets ();
    scoped_lock_t lock (_sync);
    _started = false;
    return 0;
}

void registry_t::control_task (void *arg_)
{
    registry_t *self = static_cast<registry_t *> (arg_);
    self->tick ();
}

int registry_t::ensure_sockets ()
{
    zlink::clock_t clock;
    const uint64_t now = clock.now_ms ();
    socket_base_t *old_pub = NULL;
    socket_base_t *old_router = NULL;

    {
        scoped_lock_t lock (_sync);
        if (!_started || _stop.get () != 0)
            return -1;
        if (_pub_socket && _router_socket)
            return 0;
        if (now < _next_socket_retry_ms)
            return -1;
        if (_pub_endpoint.empty () || _router_endpoint.empty ()) {
            errno = EINVAL;
            return -1;
        }
    }

    socket_base_t *pub = _ctx->create_socket (ZLINK_XPUB);
    socket_base_t *router = _ctx->create_socket (ZLINK_ROUTER);
    if (!pub || !router) {
        (void) _ctx->close_socket_and_wait (pub, 1000);
        (void) _ctx->close_socket_and_wait (router, 1000);
        scoped_lock_t lock (_sync);
        _next_socket_retry_ms = now + 100;
        return -1;
    }

    {
        scoped_lock_t lock (_sync);
        for (size_t i = 0; i < _pub_opts.size (); ++i) {
            if (!_pub_opts[i].value.empty ())
                zlink_setsockopt (
                  pub, static_cast<zlink_socket_option_t> (_pub_opts[i].option),
                  &_pub_opts[i].value[0], _pub_opts[i].value.size ());
        }
        for (size_t i = 0; i < _router_opts.size (); ++i) {
            if (!_router_opts[i].value.empty ())
                zlink_setsockopt (
                  router,
                  static_cast<zlink_socket_option_t> (_router_opts[i].option),
                  &_router_opts[i].value[0], _router_opts[i].value.size ());
        }
    }

    int verbose = 1;
    zlink_setsockopt (pub, ZLINK_XPUB_VERBOSE, &verbose, sizeof (verbose));

    if (pub->bind (_pub_endpoint.c_str ()) != 0
        || router->bind (_router_endpoint.c_str ()) != 0) {
        (void) _ctx->close_socket_and_wait (router, 1000);
        (void) _ctx->close_socket_and_wait (pub, 1000);
        scoped_lock_t lock (_sync);
        _next_socket_retry_ms = now + 100;
        return -1;
    }

    {
        scoped_lock_t lock (_sync);
        old_pub = static_cast<socket_base_t *> (_pub_socket);
        old_router = static_cast<socket_base_t *> (_router_socket);
        _pub_socket = pub;
        _router_socket = router;
        _lifecycle.register_socket (pub);
        _lifecycle.register_socket (router);

        if (!_registry_id_set) {
            _registry_id = zlink::generate_random ();
            if (_registry_id == 0)
                _registry_id = 1;
            _registry_id_set = true;
        }

        _next_broadcast_ms = now + _broadcast_interval_ms;
        _last_sent_seq = _list_seq;
        _next_socket_retry_ms = 0;
    }
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
        pub = _pub_socket;
        router = _router_socket;
        peer_sub = _peer_sub_socket;
        pub_endpoint = _pub_endpoint;
        router_endpoint = _router_endpoint;
        peer_pubs = _peer_pubs;
        _pub_socket = NULL;
        _router_socket = NULL;
        _peer_sub_socket = NULL;
        _peer_connected.clear ();
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

void registry_t::tick ()
{
    if (_stop.get () != 0)
        return;

    {
        scoped_lock_t lock (_sync);
        if (!_started)
            return;
    }

    if (ensure_sockets () != 0)
        return;

    void *pub = NULL;
    void *router = NULL;
    void *peer_sub = NULL;
    std::vector<std::string> peer_pubs;
    std::vector<socket_opt_t> peer_sub_opts;
    uint32_t broadcast_interval_ms = 0;
    {
        scoped_lock_t lock (_sync);
        pub = _pub_socket;
        router = _router_socket;
        peer_sub = _peer_sub_socket;
        peer_pubs = _peer_pubs;
        peer_sub_opts = _peer_sub_opts;
        broadcast_interval_ms = _broadcast_interval_ms;
    }
    if (!pub || !router)
        return;

    if (!peer_pubs.empty () && !peer_sub) {
        socket_base_t *peer_sub_socket = _ctx->create_socket (ZLINK_SUB);
        peer_sub = static_cast<void *> (peer_sub_socket);
        if (peer_sub) {
            for (size_t i = 0; i < peer_sub_opts.size (); ++i) {
                if (!peer_sub_opts[i].value.empty ())
                    zlink_setsockopt (
                      peer_sub,
                      static_cast<zlink_socket_option_t> (
                        peer_sub_opts[i].option),
                      &peer_sub_opts[i].value[0],
                      peer_sub_opts[i].value.size ());
            }
            zlink_setsockopt (peer_sub, ZLINK_SUBSCRIBE, "", 0);
            scoped_lock_t lock (_sync);
            if (_peer_sub_socket == NULL)
                _peer_sub_socket = peer_sub;
            else {
                (void) _ctx->close_socket_and_wait (peer_sub_socket, 1000);
                peer_sub = _peer_sub_socket;
            }
            if (_peer_sub_socket == peer_sub)
                _lifecycle.register_socket (static_cast<socket_base_t *> (peer_sub));
        }
    } else if (peer_sub == NULL && !peer_pubs.empty ()) {
        scoped_lock_t lock (_sync);
        peer_sub = _peer_sub_socket;
    }

    if (peer_sub) {
        for (size_t i = 0; i < peer_pubs.size (); ++i) {
            const std::string &endpoint = peer_pubs[i];
            scoped_lock_t lock (_sync);
            if (_peer_connected.find (endpoint) == _peer_connected.end ()) {
                zlink_connect (peer_sub, endpoint.c_str ());
                _peer_connected.insert (endpoint);
            }
        }
    }

    for (int drain = 0; drain < 64; ++drain) {
        bool handled = false;
        if (zlink::wait_socket_events_internal (router, ZLINK_POLLIN, 0) > 0) {
            handle_router (router);
            handled = true;
        }
        if (zlink::wait_socket_events_internal (pub, ZLINK_POLLIN, 0) > 0) {
            handled = true;
            while (true) {
                zlink_msg_t submsg;
                zlink_msg_init (&submsg);
                if (recv_msg_internal (pub, &submsg, ZLINK_DONTWAIT) == -1) {
                    zlink_msg_close (&submsg);
                    break;
                }
                if (zlink_msg_size (&submsg) > 0) {
                    unsigned char *data = static_cast<unsigned char *> (
                      zlink_msg_data (&submsg));
                    if (data && data[0] == 1)
                        send_service_list (pub);
                }
                zlink_msg_close (&submsg);
            }
        }
        if (peer_sub
            && zlink::wait_socket_events_internal (peer_sub, ZLINK_POLLIN, 0)
                 > 0) {
            handle_peer (peer_sub);
            handled = true;
        }

        if (!handled)
            break;
    }

    zlink::clock_t clock;
    const uint64_t now = clock.now_ms ();
    remove_expired (now);

    bool send_list = false;
    {
        scoped_lock_t lock (_sync);
        if (_list_seq != _last_sent_seq) {
            send_list = true;
            _last_sent_seq = _list_seq;
            _next_broadcast_ms = now + _broadcast_interval_ms;
        } else if (_next_broadcast_ms == 0 || now >= _next_broadcast_ms) {
            send_list = true;
            _next_broadcast_ms = now + _broadcast_interval_ms;
        }
        if (_broadcast_interval_ms == 0)
            _broadcast_interval_ms = 30000;
    }

    if (send_list)
        send_service_list (pub);

    if (broadcast_interval_ms == 0) {
        scoped_lock_t lock (_sync);
        _broadcast_interval_ms = 30000;
    }
}

void registry_t::handle_router (void *router_)
{
    zlink_msg_t msg;
    zlink_msg_init (&msg);
    if (recv_msg_internal (router_, &msg, ZLINK_DONTWAIT) == -1) {
        zlink_msg_close (&msg);
        return;
    }

    zlink_routing_id_t sender;
    sender.size = 0;
    discovery_protocol::read_routing_id (msg, &sender);
    zlink_msg_close (&msg);
    std::vector<zlink_msg_t> frames;
    while (true) {
        zlink_msg_t frame;
        zlink_msg_init (&frame);
        if (recv_msg_internal (router_, &frame, ZLINK_DONTWAIT) == -1) {
            zlink_msg_close (&frame);
            break;
        }
        frames.push_back (frame);
        if (!zlink_msg_more (&frame))
            break;
    }

    if (frames.empty ()) {
        for (size_t i = 0; i < frames.size (); ++i)
            zlink_msg_close (&frames[i]);
        return;
    }

    uint16_t msg_id = 0;
    if (zlink_msg_size (&frames[0])
          == sizeof (discovery_protocol::bootstrap_req_t)) {
        discovery_protocol::bootstrap_req_t req;
        memcpy (&req, zlink_msg_data (&frames[0]), sizeof (req));
        msg_id = req.msg_id;
    } else if (!discovery_protocol::read_u16 (frames[0], &msg_id)) {
        for (size_t i = 0; i < frames.size (); ++i)
            zlink_msg_close (&frames[i]);
        return;
    }

    if (std::getenv ("ZLINK_REGISTRY_DEBUG")) {
        std::fprintf (stderr, "[registry] msg_id=0x%04x frames=%zu\n",
                      msg_id, frames.size ());
    }

    switch (msg_id) {
        case discovery_protocol::msg_register:
            handle_register (router_, &frames[0], frames.size (), sender);
            break;
        case discovery_protocol::msg_unregister:
            handle_unregister (router_, &frames[0], frames.size (), sender);
            break;
        case discovery_protocol::msg_heartbeat:
            handle_heartbeat (&frames[0], frames.size ());
            break;
        case discovery_protocol::msg_bootstrap_req:
            handle_bootstrap (router_, sender);
            break;
        case discovery_protocol::msg_topology_report:
            handle_topology_report (&frames[0], frames.size ());
            break;
        case discovery_protocol::msg_topology_query:
            handle_topology_query (router_, &frames[0], frames.size (), sender);
            break;
        case discovery_protocol::msg_gateway_peer_report:
            handle_gateway_peer_report (&frames[0], frames.size ());
            break;
        case discovery_protocol::msg_gateway_peer_query:
            handle_gateway_peer_query (router_, &frames[0], frames.size (), sender);
            break;
        case discovery_protocol::msg_update_weight:
            handle_update_weight (router_, &frames[0], frames.size (), sender);
            break;
        default:
            break;
    }

    for (size_t i = 0; i < frames.size (); ++i)
        zlink_msg_close (&frames[i]);
}

void registry_t::handle_bootstrap (void *router_,
                                   const zlink_routing_id_t &sender_id_)
{
    send_bootstrap_reply (router_, sender_id_);
}

void registry_t::handle_peer (void *sub_)
{
    std::vector<zlink_msg_t> frames;
    while (true) {
        zlink_msg_t frame;
        zlink_msg_init (&frame);
        if (recv_msg_internal (sub_, &frame, ZLINK_DONTWAIT) == -1) {
            zlink_msg_close (&frame);
            break;
        }
        frames.push_back (frame);
        if (!zlink_msg_more (&frame))
            break;
    }

    if (frames.empty ())
        return;

    uint16_t msg_id = 0;
    if (zlink_msg_size (&frames[0])
          == sizeof (discovery_protocol::bootstrap_req_t)) {
        discovery_protocol::bootstrap_req_t req;
        memcpy (&req, zlink_msg_data (&frames[0]), sizeof (req));
        msg_id = req.msg_id;
    } else if (!discovery_protocol::read_u16 (frames[0], &msg_id)) {
        for (size_t i = 0; i < frames.size (); ++i)
            zlink_msg_close (&frames[i]);
        return;
    }

    if (msg_id != discovery_protocol::msg_service_list
        && msg_id != discovery_protocol::msg_registry_sync) {
        for (size_t i = 0; i < frames.size (); ++i)
            zlink_msg_close (&frames[i]);
        return;
    }

    if (frames.size () < 4) {
        for (size_t i = 0; i < frames.size (); ++i)
            zlink_msg_close (&frames[i]);
        return;
    }

    uint32_t peer_registry_id = 0;
    uint64_t list_seq = 0;
    uint32_t service_count = 0;
    if (!discovery_protocol::read_u32 (frames[1], &peer_registry_id)
        || !discovery_protocol::read_u64 (frames[2], &list_seq)
        || !discovery_protocol::read_u32 (frames[3], &service_count)) {
        for (size_t i = 0; i < frames.size (); ++i)
            zlink_msg_close (&frames[i]);
        return;
    }

    service_map_t incoming;
    zlink::clock_t clock;
    const uint64_t now = clock.now_ms ();

    uint32_t local_registry_id = 0;
    {
        scoped_lock_t lock (_sync);
        local_registry_id = _registry_id;
        if (local_registry_id == 0)
            local_registry_id = 1;

        if (peer_registry_id == local_registry_id) {
            for (size_t i = 0; i < frames.size (); ++i)
                zlink_msg_close (&frames[i]);
            return;
        }

        _peer_last_seen[peer_registry_id] = now;
        std::map<uint32_t, uint64_t>::iterator it =
          _peer_seq.find (peer_registry_id);
        if (it != _peer_seq.end () && list_seq <= it->second) {
            for (size_t i = 0; i < frames.size (); ++i)
                zlink_msg_close (&frames[i]);
            return;
        }
    }

    size_t index = 4;
    for (uint32_t i = 0; i < service_count && index < frames.size (); ++i) {
        if (index + 2 >= frames.size ())
            break;
        uint16_t service_type = 0;
        if (!discovery_protocol::read_u16 (frames[index++], &service_type))
            break;
        const std::string service_name =
          discovery_protocol::read_string (frames[index++]);
        uint32_t provider_count = 0;
        if (!discovery_protocol::read_u32 (frames[index++], &provider_count))
            break;

        service_key_t service_key;
        service_key.service_type = service_type;
        service_key.service_name = service_name;
        service_entry_t &service = incoming[service_key];
        for (uint32_t p = 0; p < provider_count && index + 2 < frames.size ();
             ++p) {
            provider_entry_t entry;
            entry.endpoint = discovery_protocol::read_string (frames[index++]);
            discovery_protocol::read_routing_id (frames[index++],
                                                 &entry.routing_id);
            uint32_t weight = 0;
            discovery_protocol::read_u32 (frames[index++], &weight);
            entry.weight = weight;
            entry.registered_at = now;
            entry.last_heartbeat = now;
            entry.source_registry = peer_registry_id;
            if (!entry.endpoint.empty ())
                service.providers[entry.endpoint] = entry;
        }
    }

    bool changed = false;
    {
        scoped_lock_t lock (_sync);
        std::map<uint32_t, uint64_t>::iterator it =
          _peer_seq.find (peer_registry_id);
        if (peer_registry_id == local_registry_id
            || (it != _peer_seq.end () && list_seq <= it->second)) {
            for (size_t i = 0; i < frames.size (); ++i)
                zlink_msg_close (&frames[i]);
            return;
        }

        for (service_map_t::const_iterator sit = incoming.begin ();
             sit != incoming.end (); ++sit) {
            const service_key_t &service_key = sit->first;
            const provider_map_t &providers = sit->second.providers;
            service_map_t::const_iterator existing_service =
              _services.find (service_key);
            for (provider_map_t::const_iterator pit = providers.begin ();
                 pit != providers.end (); ++pit) {
                bool match = false;
                if (existing_service != _services.end ()) {
                    provider_map_t::const_iterator ep =
                      existing_service->second.providers.find (pit->first);
                    if (ep != existing_service->second.providers.end ()
                        && ep->second.source_registry == peer_registry_id) {
                        const provider_entry_t &cur = ep->second;
                        const provider_entry_t &incoming_entry = pit->second;
                        match =
                          cur.weight == incoming_entry.weight
                          && cur.routing_id.size
                               == incoming_entry.routing_id.size
                          && (cur.routing_id.size == 0
                              || memcmp (cur.routing_id.data,
                                         incoming_entry.routing_id.data,
                                         cur.routing_id.size)
                                   == 0);
                    } else if (ep != existing_service->second.providers.end ()
                               && ep->second.source_registry
                                    != peer_registry_id) {
                        match = true;
                    }
                }
                if (!match) {
                    changed = true;
                    break;
                }
            }
            if (changed)
                break;
        }

        if (!changed) {
            for (service_map_t::const_iterator sit = _services.begin ();
                 sit != _services.end (); ++sit) {
                const provider_map_t &providers = sit->second.providers;
                for (provider_map_t::const_iterator pit = providers.begin ();
                     pit != providers.end (); ++pit) {
                    if (pit->second.source_registry != peer_registry_id)
                        continue;
                    service_map_t::const_iterator incoming_service =
                      incoming.find (sit->first);
                    if (incoming_service == incoming.end ()
                        || incoming_service->second.providers.find (pit->first)
                             == incoming_service->second.providers.end ()) {
                        changed = true;
                        break;
                    }
                }
                if (changed)
                    break;
            }
        }

        if (!changed) {
            _peer_seq[peer_registry_id] = list_seq;
            for (size_t i = 0; i < frames.size (); ++i)
                zlink_msg_close (&frames[i]);
            return;
        }

        for (service_map_t::iterator sit = _services.begin ();
             sit != _services.end ();) {
            provider_map_t &providers = sit->second.providers;
            for (provider_map_t::iterator pit = providers.begin ();
                 pit != providers.end ();) {
                if (pit->second.source_registry == peer_registry_id) {
                    pit = providers.erase (pit);
                    continue;
                }
                ++pit;
            }
            if (providers.empty ()) {
                sit = _services.erase (sit);
                continue;
            }
            ++sit;
        }

        for (service_map_t::const_iterator sit = incoming.begin ();
             sit != incoming.end (); ++sit) {
            const service_key_t &service_key = sit->first;
            const provider_map_t &providers = sit->second.providers;
            service_entry_t &service = _services[service_key];
            for (provider_map_t::const_iterator pit = providers.begin ();
                 pit != providers.end (); ++pit) {
                provider_map_t::iterator existing =
                  service.providers.find (pit->first);
                if (existing != service.providers.end ()
                    && existing->second.source_registry != peer_registry_id) {
                    continue;
                }
                service.providers[pit->first] = pit->second;
            }
        }

        _peer_seq[peer_registry_id] = list_seq;
        _list_seq++;
    }

    for (size_t i = 0; i < frames.size (); ++i)
        zlink_msg_close (&frames[i]);
}

void registry_t::handle_register (void *router_, const zlink_msg_t *frames_,
                                  size_t frame_count_,
                                  const zlink_routing_id_t &sender_id_)
{
    if (frame_count_ < 4) {
        send_register_ack (router_, sender_id_, 0xFF, std::string (),
                           "invalid register");
        return;
    }

    uint16_t service_type = 0;
    if (!discovery_protocol::read_u16 (frames_[1], &service_type)
        || (service_type != discovery_protocol::service_type_gateway_receiver
            && service_type != discovery_protocol::service_type_spot_node)) {
        send_register_ack (router_, sender_id_, 0xFF, std::string (),
                           "invalid type");
        return;
    }
    const std::string service_name =
      discovery_protocol::read_string (frames_[2]);
    const std::string endpoint =
      discovery_protocol::read_string (frames_[3]);

    if (service_name.empty () || endpoint.empty ()) {
        send_register_ack (router_, sender_id_, 0x02, endpoint,
                           "invalid endpoint");
        return;
    }

    uint32_t weight = 0;
    if (frame_count_ >= 5)
        discovery_protocol::read_u32 (frames_[4], &weight);

    zlink::clock_t clock;
    const uint64_t now = clock.now_ms ();

    service_key_t service_key;
    service_key.service_type = service_type;
    service_key.service_name = service_name;
    service_entry_t &service = _services[service_key];
    provider_entry_t &entry = service.providers[endpoint];
    entry.endpoint = endpoint;
    entry.routing_id = sender_id_;
    entry.weight = weight;
    entry.registered_at = now;
    entry.last_heartbeat = now;
    entry.source_registry = _registry_id;

    _list_seq++;
    send_register_ack (router_, sender_id_, 0x00, endpoint, std::string ());
}

void registry_t::handle_unregister (void *router_,
                                    const zlink_msg_t *frames_,
                                    size_t frame_count_,
                                    const zlink_routing_id_t &sender_id_)
{
    if (frame_count_ < 4) {
        send_unregister_ack (router_, sender_id_, 0xFF, "invalid unregister");
        return;
    }

    uint16_t service_type = 0;
    if (!discovery_protocol::read_u16 (frames_[1], &service_type)) {
        send_unregister_ack (router_, sender_id_, 0xFF, "invalid type");
        return;
    }
    const std::string service_name =
      discovery_protocol::read_string (frames_[2]);
    const std::string endpoint =
      discovery_protocol::read_string (frames_[3]);

    service_key_t service_key;
    service_key.service_type = service_type;
    service_key.service_name = service_name;

    service_map_t::iterator sit = _services.find (service_key);
    if (sit == _services.end ()) {
        send_unregister_ack (router_, sender_id_, 0x01, "service not found");
        return;
    }

    provider_map_t::iterator pit = sit->second.providers.find (endpoint);
    if (pit == sit->second.providers.end ()) {
        send_unregister_ack (router_, sender_id_, 0x01, "endpoint not found");
        return;
    }
    if (pit->second.source_registry != _registry_id) {
        send_unregister_ack (router_, sender_id_, 0x01, "foreign provider");
        return;
    }

    sit->second.providers.erase (pit);
    if (sit->second.providers.empty ())
        _services.erase (sit);

    _list_seq++;
    send_unregister_ack (router_, sender_id_, 0x00, std::string ());
}


void registry_t::handle_heartbeat (const zlink_msg_t *frames_,
                                   size_t frame_count_)
{
    if (frame_count_ < 4)
        return;

    uint16_t service_type = 0;
    if (!discovery_protocol::read_u16 (frames_[1], &service_type))
        return;
    const std::string service_name =
      discovery_protocol::read_string (frames_[2]);
    const std::string endpoint =
      discovery_protocol::read_string (frames_[3]);

    service_key_t service_key;
    service_key.service_type = service_type;
    service_key.service_name = service_name;

    service_map_t::iterator sit = _services.find (service_key);
    if (sit == _services.end ())
        return;

    provider_map_t::iterator pit = sit->second.providers.find (endpoint);
    if (pit == sit->second.providers.end ())
        return;

    zlink::clock_t clock;
    pit->second.last_heartbeat = clock.now_ms ();

}

void registry_t::handle_topology_report (const zlink_msg_t *frames_,
                                         size_t frame_count_)
{
    if (frame_count_ < 2)
        return;
    if (zlink_msg_size (&frames_[1]) != sizeof (zlink_registry_topology_entry_t))
        return;

    zlink_registry_topology_entry_t entry;
    memcpy (&entry,
            zlink_msg_data (const_cast<zlink_msg_t *> (&frames_[1])),
            sizeof (entry));
    upsert_topology_entry (entry, zlink::clock_t ().now_ms ());
}

void registry_t::handle_gateway_peer_report (const zlink_msg_t *frames_,
                                             size_t frame_count_)
{
    if (frame_count_ < 2)
        return;
    if (zlink_msg_size (&frames_[1])
        != sizeof (zlink_registry_gateway_peer_entry_t)) {
        return;
    }

    zlink_registry_gateway_peer_entry_t entry;
    memcpy (&entry,
            zlink_msg_data (const_cast<zlink_msg_t *> (&frames_[1])),
            sizeof (entry));
    upsert_gateway_peer_entry (entry, zlink::clock_t ().now_ms ());
}

void registry_t::handle_topology_query (void *router_,
                                        const zlink_msg_t *frames_,
                                        size_t frame_count_,
                                        const zlink_routing_id_t &sender_id_)
{
    zlink_registry_topology_filter_t filter;
    memset (&filter, 0, sizeof (filter));
    const zlink_registry_topology_filter_t *filter_ptr = NULL;

    if (frame_count_ >= 2 && zlink_msg_size (&frames_[1]) == sizeof (filter)) {
        memcpy (&filter,
                zlink_msg_data (const_cast<zlink_msg_t *> (&frames_[1])),
                sizeof (filter));
        filter_ptr = &filter;
    }

    std::vector<zlink_registry_topology_entry_t> entries;
    {
        scoped_lock_t lock (_sync);
        for (std::map<topology_key_t, topology_entry_t>::const_iterator it =
               _topology.begin ();
             it != _topology.end (); ++it) {
            if (topology_filter_match (it->second.entry, filter_ptr))
                entries.push_back (it->second.entry);
        }
    }
    send_topology_reply (router_, sender_id_, entries);
}

void registry_t::handle_gateway_peer_query (
  void *router_,
  const zlink_msg_t *frames_,
  size_t frame_count_,
  const zlink_routing_id_t &sender_id_)
{
    zlink_registry_gateway_peer_filter_t filter;
    memset (&filter, 0, sizeof (filter));
    const zlink_registry_gateway_peer_filter_t *filter_ptr = NULL;

    if (frame_count_ >= 2 && zlink_msg_size (&frames_[1]) == sizeof (filter)) {
        memcpy (&filter,
                zlink_msg_data (const_cast<zlink_msg_t *> (&frames_[1])),
                sizeof (filter));
        filter_ptr = &filter;
    }

    std::vector<zlink_registry_gateway_peer_entry_t> entries;
    {
        scoped_lock_t lock (_sync);
        for (std::map<gateway_peer_key_t, gateway_peer_entry_t>::const_iterator it =
               _gateway_peers.begin ();
             it != _gateway_peers.end (); ++it) {
            if (gateway_peer_filter_match (it->second.entry, filter_ptr))
                entries.push_back (it->second.entry);
        }
    }
    send_gateway_peer_reply (router_, sender_id_, entries);
}

void registry_t::handle_update_weight (void *router_, const zlink_msg_t *frames_,
                                       size_t frame_count_,
                                       const zlink_routing_id_t &sender_id_)
{
    if (frame_count_ < 5) {
        send_register_ack (router_, sender_id_, 0xFF, std::string (),
                           "invalid update");
        return;
    }

    uint16_t service_type = 0;
    if (!discovery_protocol::read_u16 (frames_[1], &service_type)
        || (service_type != discovery_protocol::service_type_gateway_receiver
            && service_type != discovery_protocol::service_type_spot_node)) {
        send_register_ack (router_, sender_id_, 0xFF, std::string (),
                           "invalid type");
        return;
    }
    const std::string service_name =
      discovery_protocol::read_string (frames_[2]);
    const std::string endpoint =
      discovery_protocol::read_string (frames_[3]);
    uint32_t weight = 0;
    discovery_protocol::read_u32 (frames_[4], &weight);

    service_key_t service_key;
    service_key.service_type = service_type;
    service_key.service_name = service_name;
    service_map_t::iterator sit = _services.find (service_key);
    if (sit == _services.end ()) {
        send_register_ack (router_, sender_id_, 0x01, endpoint,
                           "service not found");
        return;
    }

    provider_map_t::iterator pit = sit->second.providers.find (endpoint);
    if (pit == sit->second.providers.end ()) {
        send_register_ack (router_, sender_id_, 0x01, endpoint,
                           "provider not found");
        return;
    }
    if (pit->second.source_registry != _registry_id) {
        send_register_ack (router_, sender_id_, 0x01, endpoint,
                           "provider not local");
        return;
    }

    pit->second.weight = weight;
    _list_seq++;
    send_register_ack (router_, sender_id_, 0x00, endpoint, std::string ());
}

void registry_t::send_register_ack (void *router_,
                                    const zlink_routing_id_t &sender_id_,
                                    uint8_t status_,
                                    const std::string &endpoint_,
                                    const std::string &error_)
{
    registry_debug ("send_register_ack");
    registry_debug_rid ("ack target", sender_id_);
    auto log_rc = [] (const char *label_, int rc_) {
        if (!std::getenv ("ZLINK_REGISTRY_DEBUG"))
            return;
        if (rc_ == -1) {
            std::fprintf (stderr, "[registry] %s failed errno=%d (%s)\n",
                          label_, errno, std::strerror (errno));
        }
    };
    zlink_msg_t id_frame;
    zlink_msg_init_size (&id_frame, sender_id_.size);
    if (sender_id_.size > 0)
        memcpy (zlink_msg_data (&id_frame), sender_id_.data, sender_id_.size);

    const int rc_id = zlink_compat_msg_send (&id_frame, router_, ZLINK_SNDMORE);
    log_rc ("send ack id", rc_id);
    if (rc_id == -1) {
        zlink_msg_close (&id_frame);
        return;
    }

    log_rc ("send ack msg_id",
            discovery_protocol::send_u16 (
              router_, discovery_protocol::msg_register_ack, ZLINK_SNDMORE));
    log_rc ("send ack status",
            discovery_protocol::send_frame (router_, &status_,
                                            sizeof (status_), ZLINK_SNDMORE));
    log_rc ("send ack endpoint",
            discovery_protocol::send_string (router_, endpoint_,
                                             ZLINK_SNDMORE));
    log_rc ("send ack error",
            discovery_protocol::send_string (router_, error_, 0));
}

void registry_t::send_unregister_ack (void *router_,
                                      const zlink_routing_id_t &sender_id_,
                                      uint8_t status_,
                                      const std::string &error_)
{
    registry_debug ("send_unregister_ack");
    registry_debug_rid ("ack target", sender_id_);
    auto log_rc = [] (const char *label_, int rc_) {
        if (!std::getenv ("ZLINK_REGISTRY_DEBUG"))
            return;
        if (rc_ == -1) {
            std::fprintf (stderr, "[registry] %s failed errno=%d (%s)\n",
                          label_, errno, std::strerror (errno));
        }
    };
    zlink_msg_t id_frame;
    zlink_msg_init_size (&id_frame, sender_id_.size);
    if (sender_id_.size > 0)
        memcpy (zlink_msg_data (&id_frame), sender_id_.data, sender_id_.size);

    const int rc_id = zlink_compat_msg_send (&id_frame, router_, ZLINK_SNDMORE);
    log_rc ("send unreg ack id", rc_id);
    if (rc_id == -1) {
        zlink_msg_close (&id_frame);
        return;
    }

    log_rc ("send unreg ack msg_id",
            discovery_protocol::send_u16 (
              router_, discovery_protocol::msg_unregister_ack, ZLINK_SNDMORE));
    log_rc ("send unreg ack status",
            discovery_protocol::send_frame (router_, &status_,
                                            sizeof (status_),
                                            error_.empty () ? 0 : ZLINK_SNDMORE));
    if (!error_.empty ())
        log_rc ("send unreg ack error",
                discovery_protocol::send_string (router_, error_, 0));
}

void registry_t::send_topology_reply (
  void *router_,
  const zlink_routing_id_t &sender_id_,
  const std::vector<zlink_registry_topology_entry_t> &entries_)
{
    zlink_msg_t id_frame;
    zlink_msg_init_size (&id_frame, sender_id_.size);
    if (sender_id_.size > 0)
        memcpy (zlink_msg_data (&id_frame), sender_id_.data, sender_id_.size);
    if (zlink_compat_msg_send (&id_frame, router_, ZLINK_SNDMORE) == -1) {
        zlink_msg_close (&id_frame);
        return;
    }

    discovery_protocol::send_u16 (router_, discovery_protocol::msg_topology_reply,
                                  ZLINK_SNDMORE);
    discovery_protocol::send_u32 (router_,
                                  static_cast<uint32_t> (entries_.size ()),
                                  entries_.empty () ? 0 : ZLINK_SNDMORE);
    for (size_t i = 0; i < entries_.size (); ++i) {
        discovery_protocol::send_frame (
          router_, &entries_[i], sizeof (entries_[i]),
          (i + 1 == entries_.size ()) ? 0 : ZLINK_SNDMORE);
    }
}

void registry_t::send_gateway_peer_reply (
  void *router_,
  const zlink_routing_id_t &sender_id_,
  const std::vector<zlink_registry_gateway_peer_entry_t> &entries_)
{
    zlink_msg_t id_frame;
    zlink_msg_init_size (&id_frame, sender_id_.size);
    if (sender_id_.size > 0)
        memcpy (zlink_msg_data (&id_frame), sender_id_.data, sender_id_.size);
    if (zlink_compat_msg_send (&id_frame, router_, ZLINK_SNDMORE) == -1) {
        zlink_msg_close (&id_frame);
        return;
    }

    discovery_protocol::send_u16 (
      router_, discovery_protocol::msg_gateway_peer_reply, ZLINK_SNDMORE);
    discovery_protocol::send_u32 (router_,
                                  static_cast<uint32_t> (entries_.size ()),
                                  entries_.empty () ? 0 : ZLINK_SNDMORE);
    for (size_t i = 0; i < entries_.size (); ++i) {
        discovery_protocol::send_frame (
          router_, &entries_[i], sizeof (entries_[i]),
          (i + 1 == entries_.size ()) ? 0 : ZLINK_SNDMORE);
    }
}

void registry_t::send_bootstrap_reply (void *router_,
                                       const zlink_routing_id_t &sender_id_)
{
    zlink_msg_t id_frame;
    zlink_msg_init_size (&id_frame, sender_id_.size);
    if (sender_id_.size > 0)
        memcpy (zlink_msg_data (&id_frame), sender_id_.data, sender_id_.size);
    const int rc_id = zlink_compat_msg_send (&id_frame, router_, ZLINK_SNDMORE);
    if (std::getenv ("ZLINK_REGISTRY_DEBUG")) {
        std::fprintf (stderr,
                      "[registry] bootstrap reply id rc=%d rid_size=%u errno=%d\n",
                      rc_id, static_cast<unsigned int> (sender_id_.size),
                      errno);
    }
    if (rc_id == -1) {
        zlink_msg_close (&id_frame);
        return;
    }

    uint32_t registry_id = 0;
    uint32_t heartbeat_interval_ms = 0;
    std::string pub_endpoint;
    std::string uplink_endpoint;
    {
        scoped_lock_t lock (_sync);
        registry_id = _registry_id == 0 ? 1 : _registry_id;
        heartbeat_interval_ms = _heartbeat_interval_ms;
        pub_endpoint = _pub_endpoint;
        uplink_endpoint = _router_endpoint;
    }

    discovery_protocol::bootstrap_rep_t rep;
    memset (&rep, 0, sizeof (rep));
    rep.msg_id = discovery_protocol::msg_bootstrap_rep;
    rep.heartbeat_interval_ms = heartbeat_interval_ms;
    rep.registry_id = registry_id;
    strncpy (rep.pub_endpoint, pub_endpoint.c_str (),
             sizeof (rep.pub_endpoint) - 1);
    strncpy (rep.uplink_endpoint, uplink_endpoint.c_str (),
             sizeof (rep.uplink_endpoint) - 1);
    const int rc_rep =
      discovery_protocol::send_frame (router_, &rep, sizeof (rep), 0);
    if (std::getenv ("ZLINK_REGISTRY_DEBUG")) {
        std::fprintf (stderr,
                      "[registry] bootstrap reply body rc=%d errno=%d pub=%s uplink=%s\n",
                      rc_rep, errno, pub_endpoint.c_str (),
                      uplink_endpoint.c_str ());
    }
}

void registry_t::upsert_topology_entry (
  const zlink_registry_topology_entry_t &entry_,
  uint64_t now_ms_)
{
    topology_key_t key;
    key.service_kind = entry_.service_kind;
    key.routing_id_key = topology_routing_key (entry_.routing_id);
    key.service_name = entry_.service_name;

    topology_entry_t &stored = _topology[key];
    stored.entry = entry_;
    stored.entry.last_reported_ms = now_ms_;
}

void registry_t::upsert_gateway_peer_entry (
  const zlink_registry_gateway_peer_entry_t &entry_,
  uint64_t now_ms_)
{
    gateway_peer_key_t key;
    key.gateway_routing_id_key = topology_routing_key (entry_.gateway_routing_id);
    key.service_name = entry_.service_name;
    key.peer_routing_id_key = topology_routing_key (entry_.peer_routing_id);

    gateway_peer_entry_t &stored = _gateway_peers[key];
    stored.entry = entry_;
    stored.entry.last_reported_ms = now_ms_;
}

void registry_t::send_service_list (void *pub_)
{
    uint32_t registry_id = 0;
    {
        scoped_lock_t lock (_sync);
        registry_id = _registry_id;
        if (registry_id == 0)
            registry_id = 1;
    }

    discovery_protocol::send_u16 (pub_, discovery_protocol::msg_service_list,
                                  ZLINK_SNDMORE);
    discovery_protocol::send_u32 (pub_, registry_id, ZLINK_SNDMORE);
    discovery_protocol::send_u64 (pub_, _list_seq, ZLINK_SNDMORE);

    uint32_t service_count = 0;
    for (service_map_t::const_iterator it = _services.begin ();
         it != _services.end (); ++it) {
        if (!it->second.providers.empty ())
            service_count++;
    }

    discovery_protocol::send_u32 (pub_, service_count,
                                  service_count == 0 ? 0 : ZLINK_SNDMORE);

    if (service_count == 0)
        return;

    uint32_t emitted = 0;
    for (service_map_t::const_iterator it = _services.begin ();
         it != _services.end (); ++it) {
        if (it->second.providers.empty ())
            continue;

        const service_key_t &service_key = it->first;
        const provider_map_t &providers = it->second.providers;
        const uint32_t provider_count =
          static_cast<uint32_t> (providers.size ());

        discovery_protocol::send_u16 (pub_, service_key.service_type,
                                      ZLINK_SNDMORE);
        discovery_protocol::send_string (pub_, service_key.service_name,
                                         ZLINK_SNDMORE);
        discovery_protocol::send_u32 (pub_, provider_count,
                                      ZLINK_SNDMORE);

        uint32_t provider_index = 0;
        for (provider_map_t::const_iterator pit = providers.begin ();
             pit != providers.end (); ++pit, ++provider_index) {
            const provider_entry_t &entry = pit->second;
            const bool last_provider =
              (provider_index + 1) == provider_count
              && (emitted + 1) == service_count;

            discovery_protocol::send_string (pub_, entry.endpoint,
                                             ZLINK_SNDMORE);
            discovery_protocol::send_routing_id (pub_, entry.routing_id,
                                                 ZLINK_SNDMORE);
            discovery_protocol::send_u32 (pub_, entry.weight,
                                          last_provider ? 0 : ZLINK_SNDMORE);
        }

        emitted++;
    }
}

void registry_t::remove_expired (uint64_t now_ms_)
{
    const uint32_t local_registry_id = _registry_id;
    bool changed = false;
    for (service_map_t::iterator sit = _services.begin ();
         sit != _services.end ();) {
        provider_map_t &providers = sit->second.providers;
        for (provider_map_t::iterator pit = providers.begin ();
             pit != providers.end ();) {
            if (pit->second.source_registry != local_registry_id) {
                ++pit;
                continue;
            }
            if (now_ms_ > pit->second.last_heartbeat
                && now_ms_ - pit->second.last_heartbeat
                     > _heartbeat_timeout_ms) {
                pit = providers.erase (pit);
                changed = true;
                continue;
            }
            ++pit;
        }
        if (providers.empty ())
            sit = _services.erase (sit);
        else
            ++sit;
    }

    uint64_t peer_timeout_ms = _broadcast_interval_ms;
    if (peer_timeout_ms == 0)
        peer_timeout_ms = 30000;
    peer_timeout_ms *= 3;

    for (std::map<uint32_t, uint64_t>::iterator pit = _peer_last_seen.begin ();
         pit != _peer_last_seen.end ();) {
        const uint32_t peer_id = pit->first;
        if (now_ms_ > pit->second && now_ms_ - pit->second > peer_timeout_ms) {
            for (service_map_t::iterator sit = _services.begin ();
                 sit != _services.end ();) {
                provider_map_t &providers = sit->second.providers;
                for (provider_map_t::iterator eit = providers.begin ();
                     eit != providers.end ();) {
                    if (eit->second.source_registry == peer_id) {
                        eit = providers.erase (eit);
                        changed = true;
                        continue;
                    }
                    ++eit;
                }
                if (providers.empty ()) {
                    sit = _services.erase (sit);
                    continue;
                }
                ++sit;
            }
            _peer_seq.erase (peer_id);
            pit = _peer_last_seen.erase (pit);
            continue;
        }
        ++pit;
    }

    const uint64_t report_timeout_ms = _heartbeat_timeout_ms;
    const uint64_t stale_gc_timeout_ms = report_timeout_ms * 2;
    const uint64_t stopped_gc_timeout_ms = 1000;

    for (std::map<topology_key_t, topology_entry_t>::iterator it =
           _topology.begin ();
         it != _topology.end ();) {
        zlink_registry_topology_entry_t &entry = it->second.entry;
        const uint64_t age =
          now_ms_ > entry.last_reported_ms ? now_ms_ - entry.last_reported_ms : 0;
        if (entry.state == ZLINK_TOPOLOGY_STATE_STOPPED) {
            if (age > stopped_gc_timeout_ms) {
                it = _topology.erase (it);
                continue;
            }
        } else if (age > stale_gc_timeout_ms) {
            it = _topology.erase (it);
            continue;
        } else if (age > report_timeout_ms && entry.state == ZLINK_TOPOLOGY_STATE_READY) {
            entry.state = ZLINK_TOPOLOGY_STATE_LOST;
        }
        ++it;
    }

    for (std::map<gateway_peer_key_t, gateway_peer_entry_t>::iterator it =
           _gateway_peers.begin ();
         it != _gateway_peers.end ();) {
        zlink_registry_gateway_peer_entry_t &entry = it->second.entry;
        const uint64_t age =
          now_ms_ > entry.last_reported_ms ? now_ms_ - entry.last_reported_ms : 0;
        if (entry.state == ZLINK_TOPOLOGY_STATE_STOPPED) {
            if (age > stopped_gc_timeout_ms) {
                it = _gateway_peers.erase (it);
                continue;
            }
        } else if (age > stale_gc_timeout_ms) {
            it = _gateway_peers.erase (it);
            continue;
        } else if (age > report_timeout_ms && entry.state == ZLINK_TOPOLOGY_STATE_READY) {
            entry.state = ZLINK_TOPOLOGY_STATE_LOST;
        }
        ++it;
    }

    if (changed)
        _list_seq++;
}
}
