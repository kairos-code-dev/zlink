/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/gateway/gateway.hpp"

#include "core/pipe.hpp"
#include "services/common/socket_monitor_bridge.hpp"
#include "services/gateway/gateway_access.hpp"
#include "services/gateway/gateway_runtime.hpp"
#include "services/gateway/routing_id_utils.hpp"

#include <cerrno>
#include <cstring>

namespace zlink
{
namespace
{
static void gateway_router_msg_handler (const zlink_routing_id_t *source_rid_,
                                        zlink_msg_t *parts_,
                                        size_t part_count_,
                                        void *)
{
    gateway_t *gateway = static_cast<gateway_t *> (
      socket_base_t::current_socket_msg_dispatch_subject ());
    if (!gateway) {
        for (size_t i = 0; i < part_count_; ++i)
            zlink_msg_close (&parts_[i]);
        return;
    }

    gateway_access_t::dispatch_message (gateway, source_rid_, parts_, part_count_);
}

static void gateway_send_ready_handler (void *subject_, void *)
{
    gateway_t *gateway = static_cast<gateway_t *> (subject_);
    if (!gateway)
        return;
    gateway_access_t::dispatch_send_ready (gateway);
}

static void ensure_gateway_routing_id (socket_base_t *socket_,
                                       const std::string *override_id_)
{
    if (!socket_)
        return;
    if (override_id_ && !override_id_->empty ()) {
        zlink::discovery::set_socket_routing_id (socket_, override_id_, NULL);
        return;
    }
    unsigned char buf[256];
    size_t size = sizeof (buf);
    if (socket_->getsockopt (ZLINK_INTERNAL_OPT_ROUTING_ID, buf, &size) != 0)
        return;
    if (size > 0)
        return;
    zlink::discovery::set_socket_routing_id (socket_, override_id_, NULL);
}

static int allocate_router (ctx_t *ctx_, socket_base_t **socket_)
{
    *socket_ = ctx_->create_socket (ZLINK_CORE_SOCKET_ROUTER);
    if (!*socket_)
        return -1;
    return 0;
}

static int apply_tls_client (socket_base_t *socket_,
                             const std::string &ca_cert_,
                             const std::string &hostname_,
                             int trust_system_)
{
    if (!socket_)
        return -1;
    if (ca_cert_.empty () || hostname_.empty ())
        return 0;
    if (socket_->setsockopt (ZLINK_INTERNAL_OPT_TLS_CA, ca_cert_.data (),
                             ca_cert_.size ())
        != 0)
        return -1;
    if (socket_->setsockopt (ZLINK_INTERNAL_OPT_TLS_HOSTNAME, hostname_.data (),
                             hostname_.size ())
        != 0)
        return -1;
    if (socket_->setsockopt (ZLINK_INTERNAL_OPT_TLS_TRUST_SYSTEM, &trust_system_,
                             sizeof (trust_system_))
        != 0)
        return -1;
    return 0;
}

static int monitor_event_mask ()
{
    return ZLINK_EVENT_CONNECTION_READY_CHANGED | ZLINK_EVENT_DISCONNECTED
           | ZLINK_EVENT_HANDSHAKE_FAILED_NO_DETAIL
           | ZLINK_EVENT_HANDSHAKE_FAILED_PROTOCOL
           | ZLINK_EVENT_HANDSHAKE_FAILED_AUTH;
}

static void rollback_gateway_runtime_socket_init (gateway_runtime_t *runtime_)
{
    if (!runtime_)
        return;

    if (runtime_->monitor_socket) {
        socket_base_t *monitor_socket =
          static_cast<socket_base_t *> (runtime_->monitor_socket);
        close_socket_monitor_bridge (runtime_->router_socket, monitor_socket);
        (void) runtime_->lifecycle.close_socket_and_wait (monitor_socket, 2000);
        runtime_->monitor_socket = NULL;
    }
    if (runtime_->router_socket)
        (void) runtime_->lifecycle.close_socket_and_wait (runtime_->router_socket,
                                                         2000);
    (void) runtime_->lifecycle.wait_drained (2000);
}
}

int gateway_t::init_router_socket ()
{
    if (_runtime->router_socket)
        return 0;
    if (allocate_router (_ctx, &_runtime->router_socket) != 0)
        return -1;
    _runtime->lifecycle.register_socket (_runtime->router_socket);
    ensure_gateway_routing_id (_runtime->router_socket, &_routing_id_override);
    if (_routing_id.size == 0) {
        size_t size = sizeof (_routing_id.data);
        if (_runtime->router_socket->getsockopt (ZLINK_INTERNAL_OPT_ROUTING_ID,
                                                 _routing_id.data, &size)
            == 0)
            _routing_id.size = static_cast<uint8_t> (size);
    }
    if (!_tls_server_cert.empty ()) {
        if (_runtime->router_socket->setsockopt (ZLINK_INTERNAL_OPT_TLS_CERT,
                                                 _tls_server_cert.data (),
                                                 _tls_server_cert.size ())
              != 0
            || _runtime->router_socket->setsockopt (ZLINK_INTERNAL_OPT_TLS_KEY,
                                                    _tls_server_key.data (),
                                                    _tls_server_key.size ())
                 != 0) {
            rollback_gateway_runtime_socket_init (_runtime);
            return -1;
        }
    }
    if (!_runtime->monitor_socket) {
        _runtime->monitor_socket =
          open_socket_monitor_bridge (_runtime->router_socket, monitor_event_mask ());
        _runtime->lifecycle.register_socket (
          static_cast<socket_base_t *> (_runtime->monitor_socket));
    }
    if (apply_tls_client (_runtime->router_socket, _tls_ca, _tls_hostname,
                          _tls_trust_system)
        != 0) {
        rollback_gateway_runtime_socket_init (_runtime);
        return -1;
    }
    int mandatory = 1;
    _runtime->router_socket->setsockopt (ZLINK_INTERNAL_OPT_ROUTER_MANDATORY,
                                         &mandatory, sizeof (mandatory));
    int linger = 0;
    _runtime->router_socket->setsockopt (ZLINK_INTERNAL_OPT_LINGER, &linger,
                                         sizeof (linger));
    int handover = 1;
    _runtime->router_socket->setsockopt (ZLINK_INTERNAL_OPT_ROUTER_HANDOVER,
                                         &handover, sizeof (handover));
    if (_handler.load (std::memory_order_acquire) != NULL) {
        if (_runtime->router_socket->socket_set_msg_handler_ex (
              &gateway_router_msg_handler, this)
            != 0) {
            rollback_gateway_runtime_socket_init (_runtime);
            return -1;
        }
    }
    if (_send_ready_handler.load (std::memory_order_acquire) != NULL) {
        if (_runtime->router_socket->socket_set_send_ready_handler_ex (
              &gateway_send_ready_handler, this)
            != 0) {
            rollback_gateway_runtime_socket_init (_runtime);
            return -1;
        }
    }
    return 0;
}

int gateway_t::set_routing_id (const void *data_, size_t size_)
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return -1;

    if (!data_ || size_ == 0 || size_ > sizeof (_routing_id.data)) {
        errno = EINVAL;
        return -1;
    }

    scoped_lock_t lock (_sync);
    if (!_runtime->pools.empty () || _routing_id_locked) {
        errno = EFSM;
        return -1;
    }

    _routing_id_override.assign (static_cast<const char *> (data_), size_);
    memcpy (_routing_id.data, data_, size_);
    _routing_id.size = static_cast<uint8_t> (size_);
    if (_runtime->router_socket
        && _runtime->router_socket->setsockopt (ZLINK_INTERNAL_OPT_ROUTING_ID,
                                                data_, size_)
             != 0)
        return -1;
    return 0;
}

int gateway_t::routing_id (zlink_routing_id_t *out_)
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return -1;

    if (!out_) {
        errno = EINVAL;
        return -1;
    }

    scoped_lock_t lock (_sync);
    if (_routing_id.size == 0) {
        if (ensure_router_socket () != 0)
            return -1;
        size_t size = sizeof (_routing_id.data);
        if (_runtime->router_socket->getsockopt (ZLINK_INTERNAL_OPT_ROUTING_ID,
                                                 _routing_id.data, &size)
            != 0)
            return -1;
        _routing_id.size = static_cast<uint8_t> (size);
    }
    *out_ = _routing_id;
    return 0;
}

int gateway_t::last_endpoint (char *endpoint_out_, size_t *size_out_) const
{
    service_public_api_scope_t admission (
      const_cast<service_public_api_guard_t &> (_public_api));
    if (!admission.acquired ())
        return -1;

    if (!endpoint_out_ || !size_out_) {
        errno = EINVAL;
        return -1;
    }

    scoped_lock_t lock (const_cast<mutex_t &> (_sync));
    if (!_runtime->router_socket) {
        errno = ENOTSUP;
        return -1;
    }
    if (_runtime->router_socket->getsockopt (ZLINK_INTERNAL_OPT_LAST_ENDPOINT,
                                             endpoint_out_, size_out_)
        == 0) {
        return 0;
    }

    if (_bind_endpoint.empty ())
        return -1;

    const size_t required = _bind_endpoint.size () + 1;
    if (*size_out_ < required) {
        *size_out_ = required;
        errno = EINVAL;
        return -1;
    }

    memcpy (endpoint_out_, _bind_endpoint.c_str (), required);
    *size_out_ = required;
    return 0;
}

int gateway_t::set_socket_option (int option_,
                                  const void *optval_,
                                  size_t optvallen_)
{
    if (!optval_ || optvallen_ == 0) {
        errno = EINVAL;
        return -1;
    }

    scoped_lock_t lock (_sync);
    if (ensure_facade_mode () != 0)
        return -1;
    if (!_runtime->router_socket) {
        errno = ENOTSUP;
        return -1;
    }
    return _runtime->router_socket->setsockopt (option_, optval_, optvallen_);
}

int gateway_t::get_socket_option (int option_,
                                  void *optval_,
                                  size_t *optvallen_)
{
    if (!optval_ || !optvallen_) {
        errno = EINVAL;
        return -1;
    }

    scoped_lock_t lock (_sync);
    if (ensure_facade_mode () != 0)
        return -1;
    if (!_runtime->router_socket) {
        errno = ENOTSUP;
        return -1;
    }
    return _runtime->router_socket->getsockopt (option_, optval_, optvallen_);
}

void *gateway_t::router ()
{
    scoped_lock_t lock (_sync);
    lock_routing_id ();
    if (ensure_router_socket () != 0)
        return NULL;
    return static_cast<void *> (_runtime->router_socket);
}

void gateway_t::lock_routing_id ()
{
    _routing_id_locked = true;
}

int gateway_t::set_handler (zlink_socket_msg_handler_fn handler_,
                            void *userdata_)
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return -1;

    if (!handler_) {
        errno = EINVAL;
        return -1;
    }

    scoped_lock_t lock (_sync);
    if (ensure_facade_mode () != 0)
        return -1;
    _handler_userdata.store (userdata_, std::memory_order_release);
    _handler.store (handler_, std::memory_order_release);
    if (_runtime->router_socket) {
        if (_runtime->router_socket->socket_set_msg_handler_ex (
              &gateway_router_msg_handler, this)
            != 0) {
            return -1;
        }
    }
    return 0;
}

int gateway_t::set_send_ready_handler (zlink_send_ready_handler_fn handler_,
                                       void *userdata_)
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return -1;

    if (!handler_) {
        errno = EINVAL;
        return -1;
    }

    if (_runtime->router_socket) {
        if (_runtime->router_socket->socket_set_send_ready_handler_ex (
              &gateway_send_ready_handler, this)
            != 0) {
            return -1;
        }
    }
    _send_ready_handler_userdata.store (userdata_, std::memory_order_release);
    _send_ready_handler.store (handler_, std::memory_order_release);
    return 0;
}

int gateway_t::set_tls_client (const char *ca_cert_,
                               const char *hostname_,
                               int trust_system_)
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return -1;

    if (!ca_cert_ || !hostname_) {
        errno = EINVAL;
        return -1;
    }

    scoped_lock_t lock (_sync);
    if (ensure_facade_mode () != 0)
        return -1;
    _tls_ca.assign (ca_cert_);
    _tls_hostname.assign (hostname_);
    _tls_trust_system = trust_system_;

    if (ensure_router_socket () != 0)
        return -1;
    if (_runtime->router_socket
        && apply_tls_client (_runtime->router_socket, _tls_ca, _tls_hostname,
                             _tls_trust_system)
             != 0)
        return -1;
    return 0;
}

void gateway_t::dispatch_message (const zlink_routing_id_t *source_rid_,
                                  zlink_msg_t *parts_,
                                  size_t part_count_)
{
    zlink_socket_msg_handler_fn handler =
      _handler.load (std::memory_order_acquire);
    if (!handler) {
        for (size_t i = 0; i < part_count_; ++i)
            zlink_msg_close (&parts_[i]);
        return;
    }

    handler (source_rid_, parts_, part_count_,
             _handler_userdata.load (std::memory_order_acquire));
}

void gateway_t::dispatch_send_ready ()
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return;
    zlink_send_ready_handler_fn handler =
      _send_ready_handler.load (std::memory_order_acquire);
    if (handler)
        handler (this,
                 _send_ready_handler_userdata.load (std::memory_order_acquire));
}
} // namespace zlink
