/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "services/spot/spot_auto_hwm_internal.hpp"
#include "services/spot/spot_runtime_internal.hpp"
#include "services/spot/spot_node.hpp"

namespace zlink
{
int spot_runtime_t::ensure_sender_socket (spot_runtime_sender_kind_t kind_,
                                          socket_base_t **out_)
{
    if (!owner || !owner->_ctx || !out_) {
        errno = EFAULT;
        return -1;
    }
    if (kind_ != spot_runtime_sender_internal_router
        && kind_ != spot_runtime_sender_pub_ingress) {
        errno = EINVAL;
        return -1;
    }
    if (kind_ == spot_runtime_sender_internal_router
        && !owner->routed_enabled ()) {
        errno = ENOTSUP;
        return -1;
    }
    if (kind_ == spot_runtime_sender_pub_ingress && !owner->pubsub_enabled ()) {
        errno = ENOTSUP;
        return -1;
    }

    scoped_lock_t lock (owner->_sync);
    if (stop.get () != 0 || faulted
        || shutdown_phase_value != spot_shutdown_phase_running) {
        errno = EFSM;
        return -1;
    }

    socket_base_t *&slot =
      kind_ == spot_runtime_sender_internal_router ? internal_router_tx
                                                   : pub_ingress_tx;
    std::string &connected_endpoint =
      kind_ == spot_runtime_sender_internal_router
        ? internal_router_sender_endpoint
        : pub_ingress_sender_endpoint;
    const std::string &target_endpoint =
      kind_ == spot_runtime_sender_internal_router ? internal_router_endpoint
                                                   : pub_ingress_endpoint;
    if (target_endpoint.empty ()) {
        errno = EFAULT;
        return -1;
    }

    if (slot && connected_endpoint == target_endpoint) {
        *out_ = slot;
        return 0;
    }

    if (slot) {
        close_socket_ptr (&slot);
        connected_endpoint.clear ();
    }

    socket_base_t *socket = owner->_ctx->create_socket (
      kind_ == spot_runtime_sender_internal_router ? ZLINK_CORE_SOCKET_DEALER
                                                   : ZLINK_CORE_SOCKET_PUB);
    if (!socket)
        return -1;
    socket->set_auto_hwm_policy_enabled (false);
    size_t local_pub_count = 0;
    size_t local_sub_count = 0;
    size_t connected_peer_count = 0;
    size_t active_peer_count = 0;
    snapshot_auto_hwm_inputs (&local_pub_count, &local_sub_count,
                              &connected_peer_count, &active_peer_count);
    apply_spot_internal_auto_hwm (
      owner->_ctx, socket,
      spot_internal_auto_hwm_policy_t{kind_ == spot_runtime_sender_internal_router
                                        ? auto_hwm_role_routed
                                        : auto_hwm_role_spot_data,
                                      kind_ == spot_runtime_sender_internal_router
                                        ? ZLINK_CORE_SOCKET_DEALER
                                        : ZLINK_CORE_SOCKET_PUB,
                                      connected_peer_count,
                                      active_peer_count,
                                      0, 0, true, true, true, true});

    const spot_node_hwm_config_t hwm_config = hwm_config_snapshot ();
    const int zero = 0;
    const int linger = 0;
    if (kind_ == spot_runtime_sender_internal_router
        && spot_node_router_hwm_overridden (hwm_config)) {
        const int admission_hwm = spot_node_router_admission_hwm (hwm_config);
        socket->setsockopt (ZLINK_INTERNAL_OPT_SNDHWM, &admission_hwm,
                            sizeof (admission_hwm));
    }
    if (kind_ == spot_runtime_sender_pub_ingress
        && spot_node_pubsub_hwm_overridden (hwm_config)) {
        const int admission_hwm = spot_node_pubsub_admission_hwm (hwm_config);
        socket->setsockopt (ZLINK_INTERNAL_OPT_SNDHWM, &admission_hwm,
                            sizeof (admission_hwm));
    }
    socket->setsockopt (ZLINK_INTERNAL_OPT_RCVHWM, &zero, sizeof (zero));
    socket->setsockopt (ZLINK_INTERNAL_OPT_LINGER, &linger, sizeof (linger));
    if (socket->connect (target_endpoint.c_str ()) != 0) {
        const int saved_errno = errno != 0 ? errno : EIO;
        close_socket_ptr (&socket);
        errno = saved_errno;
        return -1;
    }

    slot = socket;
    connected_endpoint = target_endpoint;
    *out_ = slot;
    return 0;
}

int spot_runtime_t::close_sender_cache (spot_runtime_sender_kind_t kind_,
                                        int timeout_ms_)
{
    if (!owner) {
        errno = EFAULT;
        return -1;
    }
    if (kind_ != spot_runtime_sender_internal_router
        && kind_ != spot_runtime_sender_pub_ingress) {
        errno = EINVAL;
        return -1;
    }

    socket_base_t *socket = NULL;
    std::string endpoint;
    {
        scoped_lock_t lock (owner->_sync);
        if (kind_ == spot_runtime_sender_internal_router) {
            socket = internal_router_tx;
            endpoint = internal_router_sender_endpoint;
            internal_router_tx = NULL;
            internal_router_sender_endpoint.clear ();
        } else {
            socket = pub_ingress_tx;
            endpoint = pub_ingress_sender_endpoint;
            pub_ingress_tx = NULL;
            pub_ingress_sender_endpoint.clear ();
        }
    }

    if (!socket)
        return 0;
    LIBZLINK_UNUSED (endpoint);
    LIBZLINK_UNUSED (timeout_ms_);
    close_socket_ptr (&socket);
    return 0;
}

int spot_runtime_t::close_sender_caches (int timeout_ms_)
{
    int first_error = 0;
    preserve_first_error (
      close_sender_cache (spot_runtime_sender_internal_router, timeout_ms_),
      &first_error);
    preserve_first_error (
      close_sender_cache (spot_runtime_sender_pub_ingress, timeout_ms_),
      &first_error);
    if (first_error != 0) {
        errno = first_error;
        return -1;
    }
    return 0;
}
}
