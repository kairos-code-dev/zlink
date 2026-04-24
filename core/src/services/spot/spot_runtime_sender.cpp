/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "services/spot/spot_data_plane.hpp"
#include "services/spot/spot_runtime_internal.hpp"
#include "services/spot/spot_node.hpp"

#include "utils/clock.hpp"
#include "utils/sleep.hpp"

namespace zlink
{
namespace
{
bool socket_route_sender_ready_local (socket_base_t *socket_,
                                      const std::string &endpoint_)
{
    if (!socket_ || endpoint_.empty ())
        return false;

    if (!socket_->socket_has_attached_pipes ())
        return false;

    std::vector<std::string> remote_endpoints;
    socket_->socket_peer_remote_endpoints (&remote_endpoints);
    for (size_t i = 0; i < remote_endpoints.size (); ++i) {
        if (remote_endpoints[i] == endpoint_)
            return true;
    }
    return remote_endpoints.empty ();
}

socket_base_t *&sender_socket_slot_local (spot_runtime_t *runtime_,
                                          spot_runtime_sender_kind_t kind_)
{
    return kind_ == spot_runtime_sender_node_router ? runtime_->node_router_tx
                                                    : runtime_->route_ingress_tx;
}

std::string &sender_endpoint_cache_local (spot_runtime_t *runtime_,
                                          spot_runtime_sender_kind_t kind_)
{
    return kind_ == spot_runtime_sender_node_router
             ? runtime_->node_router_sender_endpoint
             : runtime_->route_ingress_sender_endpoint;
}

const std::string &sender_target_endpoint_local (
  const spot_runtime_t *runtime_,
  spot_runtime_sender_kind_t kind_)
{
    return kind_ == spot_runtime_sender_node_router
             ? runtime_->node_router_endpoint
             : runtime_->route_ingress_endpoint;
}
}

int spot_runtime_t::ensure_sender_socket (spot_runtime_sender_kind_t kind_,
                                          socket_base_t **out_)
{
    if (!owner || !owner->_ctx || !out_) {
        errno = EFAULT;
        return -1;
    }

    scoped_lock_t lock (owner->_sync);
    if (stop.get () != 0 || faulted
        || shutdown_phase_value != spot_shutdown_phase_running) {
        errno = EFSM;
        return -1;
    }

    socket_base_t *&slot = sender_socket_slot_local (this, kind_);
    std::string &connected_endpoint = sender_endpoint_cache_local (this, kind_);
    const std::string &target_endpoint =
      sender_target_endpoint_local (this, kind_);
    if (target_endpoint.empty ()) {
        errno = EFAULT;
        return -1;
    }

    if (slot && connected_endpoint == target_endpoint) {
        *out_ = slot;
        return 0;
    }

    if (slot) {
        close_socket_ptr_local (&slot);
        connected_endpoint.clear ();
    }

    socket_base_t *socket = owner->_ctx->create_socket (ZLINK_CORE_SOCKET_DEALER);
    if (!socket)
        return -1;

    const int linger = 0;
    socket->setsockopt (ZLINK_INTERNAL_OPT_LINGER, &linger, sizeof (linger));
    if (socket->connect (target_endpoint.c_str ()) != 0) {
        const int saved_errno = errno != 0 ? errno : EIO;
        close_socket_ptr_local (&socket);
        errno = saved_errno;
        return -1;
    }

    slot = socket;
    connected_endpoint = target_endpoint;
    *out_ = slot;
    return 0;
}

int spot_runtime_t::ensure_peer_route_sender_socket (
  const std::string &target_endpoint_,
  socket_base_t **out_)
{
    if (!owner || !owner->_ctx || !out_ || target_endpoint_.empty ()) {
        errno = EFAULT;
        return -1;
    }

    scoped_lock_t lock (owner->_sync);
    if (stop.get () != 0 || faulted
        || shutdown_phase_value != spot_shutdown_phase_running) {
        errno = EFSM;
        return -1;
    }

    if (peer_route_tx && peer_route_sender_endpoint == target_endpoint_) {
        *out_ = peer_route_tx;
        return 0;
    }

    if (peer_route_tx) {
        close_socket_ptr_local (&peer_route_tx);
        peer_route_sender_endpoint.clear ();
        peer_route_sender_ready_after_ms = 0;
    }

    socket_base_t *socket =
      owner->_ctx->create_socket (ZLINK_CORE_SOCKET_DEALER);
    if (!socket)
        return -1;

    const int linger = 0;
    const int immediate = 1;
    socket->setsockopt (ZLINK_INTERNAL_OPT_LINGER, &linger, sizeof (linger));
    socket->setsockopt (ZLINK_INTERNAL_OPT_IMMEDIATE, &immediate,
                        sizeof (immediate));
    if (spot_node_t::apply_tls_client (socket, owner->_tls_ca,
                                       owner->_tls_hostname,
                                       owner->_tls_trust_system)
          != 0
        || socket->connect (target_endpoint_.c_str ()) != 0) {
        const int saved_errno = errno != 0 ? errno : EIO;
        close_socket_ptr_local (&socket);
        errno = saved_errno;
        return -1;
    }

    const uint64_t deadline_ms = zlink::clock_t ().now_ms () + 1000;
    while (zlink::clock_t ().now_ms () < deadline_ms) {
        spot_data_plane_forwarder_t::pump_socket_commands (socket);
        socket->set_all_pipes_nodelay ();
        if (socket_route_sender_ready_local (socket, target_endpoint_))
            break;
        zlink::sleep_ms (1);
    }
    if (!socket_route_sender_ready_local (socket, target_endpoint_)) {
        close_socket_ptr_local (&socket);
        errno = ETIMEDOUT;
        return -1;
    }

    peer_route_tx = socket;
    peer_route_sender_endpoint = target_endpoint_;
    peer_route_sender_ready_after_ms = 0;
    *out_ = peer_route_tx;
    return 0;
}

int spot_runtime_t::close_sender_cache (spot_runtime_sender_kind_t kind_,
                                        int timeout_ms_)
{
    socket_base_t *socket = NULL;
    std::string endpoint;
    {
        scoped_lock_t lock (owner->_sync);
        socket = sender_socket_slot_local (this, kind_);
        endpoint = sender_endpoint_cache_local (this, kind_);
        sender_socket_slot_local (this, kind_) = NULL;
        sender_endpoint_cache_local (this, kind_).clear ();
    }

    if (!socket)
        return 0;
    LIBZLINK_UNUSED (endpoint);
    LIBZLINK_UNUSED (timeout_ms_);
    close_socket_ptr_local (&socket);
    return 0;
}

int spot_runtime_t::close_sender_caches (int timeout_ms_)
{
    int first_error = 0;
    preserve_first_error_local (
      close_sender_cache (spot_runtime_sender_route_ingress, timeout_ms_),
      &first_error);
    preserve_first_error_local (
      close_sender_cache (spot_runtime_sender_node_router, timeout_ms_),
      &first_error);
    {
        socket_base_t *socket = NULL;
        {
            scoped_lock_t lock (owner->_sync);
            socket = peer_route_tx;
            peer_route_tx = NULL;
            peer_route_sender_endpoint.clear ();
            peer_route_sender_ready_after_ms = 0;
        }
        if (socket)
            close_socket_ptr_local (&socket);
    }
    if (first_error != 0) {
        errno = first_error;
        return -1;
    }
    return 0;
}
}
