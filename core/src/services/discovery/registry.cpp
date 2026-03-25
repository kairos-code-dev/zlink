/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "core/recv_internal.hpp"
#include "core/send_internal.hpp"
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
static bool registry_frame_has_more (const zlink_msg_t &frame_)
{
    return (reinterpret_cast<const msg_t *> (&frame_)->flags () & msg_t::more)
           != 0;
}

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


registry_t::registry_t (ctx_t *ctx_) :
    _ctx (ctx_),
    _tag (registry_tag_value),
    _lifecycle (ctx_),
    _registry_id (0),
    _registry_id_set (false),
    _list_seq (0),
    _last_summary_error (0),
    _summary_last_changed_ms (0),
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
    mandatory_opt.option = ZLINK_INTERNAL_OPT_ROUTER_MANDATORY;
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
        _summary_last_changed_ms = zlink::clock_t ().now_ms ();
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
    _summary_last_changed_ms = zlink::clock_t ().now_ms ();
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
    _summary_last_changed_ms = zlink::clock_t ().now_ms ();
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
                static_cast<socket_base_t *> (existing_socket)
                  ->setsockopt (option_, optval_, optvallen_);
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
        static_cast<socket_base_t *> (existing_socket)
          ->setsockopt (option_, optval_, optvallen_);
    return 0;
}

int registry_t::set_tls_server (const char *cert_,
                                const char *key_,
                                int require_client_cert_)
{
    const int require_client_cert = require_client_cert_ ? 1 : 0;
    if (set_socket_option (ZLINK_REGISTRY_SOCKET_PUB, ZLINK_INTERNAL_OPT_TLS_CERT, cert_,
                           strlen (cert_) + 1)
          != 0
        || set_socket_option (ZLINK_REGISTRY_SOCKET_PUB, ZLINK_INTERNAL_OPT_TLS_KEY, key_,
                              strlen (key_) + 1)
             != 0
        || set_socket_option (ZLINK_REGISTRY_SOCKET_PUB,
                              ZLINK_INTERNAL_OPT_TLS_REQUIRE_CLIENT_CERT,
                              &require_client_cert,
                              sizeof (require_client_cert))
             != 0
        || set_socket_option (ZLINK_REGISTRY_SOCKET_ROUTER, ZLINK_INTERNAL_OPT_TLS_CERT,
                              cert_, strlen (cert_) + 1)
             != 0
        || set_socket_option (ZLINK_REGISTRY_SOCKET_ROUTER, ZLINK_INTERNAL_OPT_TLS_KEY, key_,
                              strlen (key_) + 1)
             != 0
        || set_socket_option (ZLINK_REGISTRY_SOCKET_ROUTER,
                              ZLINK_INTERNAL_OPT_TLS_REQUIRE_CLIENT_CERT,
                              &require_client_cert,
                              sizeof (require_client_cert))
             != 0)
        return -1;
    return 0;
}

int registry_t::set_tls_client (const char *ca_cert_,
                                const char *hostname_,
                                int trust_system_)
{
    if (set_socket_option (ZLINK_REGISTRY_SOCKET_PEER_SUB, ZLINK_INTERNAL_OPT_TLS_CA,
                           ca_cert_, strlen (ca_cert_) + 1)
          != 0
        || set_socket_option (ZLINK_REGISTRY_SOCKET_PEER_SUB,
                              ZLINK_INTERNAL_OPT_TLS_HOSTNAME,
                              hostname_,
                              strlen (hostname_) + 1)
             != 0
        || set_socket_option (ZLINK_REGISTRY_SOCKET_PEER_SUB,
                              ZLINK_INTERNAL_OPT_TLS_TRUST_SYSTEM,
                              &trust_system_,
                              sizeof (trust_system_))
             != 0)
        return -1;
    return 0;
}

int registry_t::status_snapshot (zlink_registry_status_t *out_)
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return -1;
    if (!out_) {
        errno = EINVAL;
        return -1;
    }

    memset (out_, 0, sizeof (*out_));

    scoped_lock_t lock (_sync);
    out_->registry_id = _registry_id;
    if (!_router_endpoint.empty ()) {
        strncpy (out_->bind_endpoint, _router_endpoint.c_str (),
                 sizeof (out_->bind_endpoint) - 1);
    }
    out_->topology_entry_count = static_cast<uint32_t> (_topology.size ());
    out_->peer_registry_count = static_cast<uint32_t> (_peer_pubs.size ());
    out_->connected_peer_registry_count =
      static_cast<uint32_t> (_peer_last_seen.size ());
    out_->list_seq = _list_seq;
    out_->last_error = _last_summary_error;
    out_->last_changed_ms = _summary_last_changed_ms;

    if (out_->last_error != 0)
        out_->state = ZLINK_REGISTRY_STATE_ERROR;
    else if (out_->bind_endpoint[0] == '\0' || out_->registry_id == 0)
        out_->state = ZLINK_REGISTRY_STATE_IDLE;
    else if (out_->peer_registry_count == 0)
        out_->state = ZLINK_REGISTRY_STATE_ACTIVE;
    else if (out_->connected_peer_registry_count < out_->peer_registry_count)
        out_->state = ZLINK_REGISTRY_STATE_DEGRADED;
    else
        out_->state = ZLINK_REGISTRY_STATE_ACTIVE;

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
        if (!registry_frame_has_more (frame))
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

    const int rc_id = zlink::send_msg_internal (router_, &id_frame, ZLINK_SNDMORE);
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

    const int rc_id = zlink::send_msg_internal (router_, &id_frame, ZLINK_SNDMORE);
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

void registry_t::send_bootstrap_reply (void *router_,
                                       const zlink_routing_id_t &sender_id_)
{
    zlink_msg_t id_frame;
    zlink_msg_init_size (&id_frame, sender_id_.size);
    if (sender_id_.size > 0)
        memcpy (zlink_msg_data (&id_frame), sender_id_.data, sender_id_.size);
    const int rc_id = zlink::send_msg_internal (router_, &id_frame, ZLINK_SNDMORE);
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

}
