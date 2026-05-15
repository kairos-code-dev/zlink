/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "core/c_api_copy_internal.hpp"
#include "services/registry/registry.hpp"
#include "sockets/common/socket_base.hpp"

namespace zlink
{
namespace
{
bool is_supported_registry_transport (const char *endpoint_)
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
}

int registry_t::bind (const char *pub_endpoint_, const char *router_endpoint_)
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
        if (_runtime_socket_state.started) {
            errno = EBUSY;
            return -1;
        }
        _endpoint_config.pub_endpoint = pub_endpoint_;
        _endpoint_config.router_endpoint = router_endpoint_;
        _coordination_state.summary_last_changed_ms = zlink::clock_t ().now_ms ();
    }

    return start ();
}

int registry_t::set_id (uint32_t registry_id_)
{
    return set_option (ZLINK_REGISTRY_OPT_ID, registry_id_);
}

int registry_t::set_option (zlink_registry_option_t option_, uint32_t value_)
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return -1;
    if (value_ == 0) {
        errno = EINVAL;
        return -1;
    }

    scoped_lock_t lock (_sync);
    switch (option_) {
        case ZLINK_REGISTRY_OPT_ID:
            _coordination_state.registry_id = value_;
            _coordination_state.registry_id_set = true;
            _coordination_state.summary_last_changed_ms =
              zlink::clock_t ().now_ms ();
            return 0;
        case ZLINK_REGISTRY_OPT_HEARTBEAT_INTERVAL_MS:
            _coordination_state.heartbeat_interval_ms = value_;
            return 0;
        case ZLINK_REGISTRY_OPT_HEARTBEAT_TIMEOUT_MS:
            _coordination_state.heartbeat_timeout_ms = value_;
            return 0;
        case ZLINK_REGISTRY_OPT_BROADCAST_INTERVAL_MS:
            _coordination_state.broadcast_interval_ms = value_;
            return 0;
        default:
            errno = ENOTSUP;
            return -1;
    }
}

int registry_t::get_option (zlink_registry_option_t option_, uint32_t *value_out_)
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return -1;
    if (!value_out_) {
        errno = EINVAL;
        return -1;
    }

    scoped_lock_t lock (_sync);
    switch (option_) {
        case ZLINK_REGISTRY_OPT_ID:
            *value_out_ = _coordination_state.registry_id;
            return 0;
        case ZLINK_REGISTRY_OPT_HEARTBEAT_INTERVAL_MS:
            *value_out_ = _coordination_state.heartbeat_interval_ms;
            return 0;
        case ZLINK_REGISTRY_OPT_HEARTBEAT_TIMEOUT_MS:
            *value_out_ = _coordination_state.heartbeat_timeout_ms;
            return 0;
        case ZLINK_REGISTRY_OPT_BROADCAST_INTERVAL_MS:
            *value_out_ = _coordination_state.broadcast_interval_ms;
            return 0;
        default:
            errno = ENOTSUP;
            return -1;
    }
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
    _endpoint_config.peer_pubs.push_back (peer_pub_endpoint_);
    _coordination_state.summary_last_changed_ms = zlink::clock_t ().now_ms ();
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
    _coordination_state.heartbeat_interval_ms = interval_ms_;
    _coordination_state.heartbeat_timeout_ms = timeout_ms_;
    return 0;
}

int registry_t::set_broadcast_interval (uint32_t interval_ms_)
{
    return set_option (ZLINK_REGISTRY_OPT_BROADCAST_INTERVAL_MS,
                       interval_ms_);
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
            opts = &_socket_option_state.pub_opts;
            existing_socket = _runtime_socket_state.pub_socket;
            break;
        case ZLINK_REGISTRY_SOCKET_ROUTER:
            opts = &_socket_option_state.router_opts;
            existing_socket = _runtime_socket_state.router_socket;
            break;
        case ZLINK_REGISTRY_SOCKET_PEER_SUB:
            opts = &_socket_option_state.peer_sub_opts;
            existing_socket = _runtime_socket_state.peer_sub_socket;
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
    if (set_socket_option (ZLINK_REGISTRY_SOCKET_PUB,
                           ZLINK_INTERNAL_OPT_TLS_CERT, cert_,
                           strlen (cert_) + 1)
          != 0
        || set_socket_option (ZLINK_REGISTRY_SOCKET_PUB,
                              ZLINK_INTERNAL_OPT_TLS_KEY, key_,
                              strlen (key_) + 1)
             != 0
        || set_socket_option (ZLINK_REGISTRY_SOCKET_PUB,
                              ZLINK_INTERNAL_OPT_TLS_REQUIRE_CLIENT_CERT,
                              &require_client_cert,
                              sizeof (require_client_cert))
             != 0
        || set_socket_option (ZLINK_REGISTRY_SOCKET_ROUTER,
                              ZLINK_INTERNAL_OPT_TLS_CERT, cert_,
                              strlen (cert_) + 1)
             != 0
        || set_socket_option (ZLINK_REGISTRY_SOCKET_ROUTER,
                              ZLINK_INTERNAL_OPT_TLS_KEY, key_,
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
    if (set_socket_option (ZLINK_REGISTRY_SOCKET_PEER_SUB,
                           ZLINK_INTERNAL_OPT_TLS_CA, ca_cert_,
                           strlen (ca_cert_) + 1)
          != 0
        || set_socket_option (ZLINK_REGISTRY_SOCKET_PEER_SUB,
                              ZLINK_INTERNAL_OPT_TLS_HOSTNAME, hostname_,
                              strlen (hostname_) + 1)
             != 0
        || set_socket_option (ZLINK_REGISTRY_SOCKET_PEER_SUB,
                              ZLINK_INTERNAL_OPT_TLS_TRUST_SYSTEM,
                              &trust_system_, sizeof (trust_system_))
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
    out_->registry_id = _coordination_state.registry_id;
    if (!_endpoint_config.router_endpoint.empty ()) {
        copy_fixed_c_string_from_cstr (out_->bind_endpoint,
                                       sizeof (out_->bind_endpoint),
                                       _endpoint_config.router_endpoint.c_str ());
    }
    out_->topology_entry_count = static_cast<uint32_t> (_projection_state.topology.size ());
    out_->peer_registry_count = static_cast<uint32_t> (_endpoint_config.peer_pubs.size ());
    out_->connected_peer_registry_count =
      static_cast<uint32_t> (_projection_state.peer_last_seen.size ());
    out_->list_seq = _coordination_state.list_seq;
    out_->last_error = _coordination_state.last_summary_error;
    out_->last_changed_ms = _coordination_state.summary_last_changed_ms;

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
}
