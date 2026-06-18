/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/spot/common/spot_control_protocol.hpp"
#include "services/spot/common/spot_auto_hwm_internal.hpp"
#include "services/spot/data_plane/spot_data_plane.hpp"
#include "services/spot/data_plane/spot_data_plane_internal.hpp"
#include "services/spot/data_plane/spot_mesh_pub_hwm.hpp"
#include "services/spot/node/spot_node.hpp"
#include "services/spot/node/spot_node_access.hpp"
#include "services/spot/runtime/spot_runtime.hpp"

#include "api/spot/dispatch/service_spot_dispatch_surface_internal.hpp"
#include "api/socket/request_reply_protocol_internal.hpp"
#include "core/auto_hwm_policy.hpp"
#include "core/socket_poller.hpp"
#include "services/common/socket_monitor_bridge.hpp"
#include "sockets/common/socket_base.hpp"

#include <errno.h>
#include <string.h>

namespace zlink
{
namespace
{
static const int spot_data_plane_hwm_default = 0;
static const int spot_internal_peer_ctrl_sndhwm_default = 0;
static const int spot_internal_peer_ctrl_rcvhwm_default = 0;

static bool read_socket_int_option (socket_base_t *socket_, int option_, int *value_out_)
{
    if (!socket_ || !value_out_)
        return false;

    size_t value_size = sizeof (*value_out_);
    return socket_->getsockopt (option_, value_out_, &value_size) == 0
           && value_size == sizeof (*value_out_);
}

static int resolve_pubsub_admission_hwm (const spot_runtime_t *runtime_)
{
    if (!runtime_ || !runtime_->owner)
        return spot_node_admission_hwm_for_profile (ZLINK_AUTO_HWM_PROFILE_BALANCED);

    const spot_node_runtime_tuning_t hwm = runtime_->runtime_tuning_snapshot ();
    return spot_node_pubsub_admission_hwm (hwm);
}

static int resolve_router_admission_hwm (const spot_runtime_t *runtime_)
{
    if (!runtime_ || !runtime_->owner)
        return spot_node_admission_hwm_for_profile (ZLINK_AUTO_HWM_PROFILE_BALANCED);

    const spot_node_runtime_tuning_t hwm = runtime_->runtime_tuning_snapshot ();
    return spot_node_router_admission_hwm (hwm);
}

static int apply_common_internal_opts (socket_base_t *socket_, int linger_)
{
    return socket_->setsockopt (ZLINK_INTERNAL_OPT_LINGER, &linger_, sizeof (linger_));
}

static void apply_internal_auto_hwm (ctx_t *ctx_,
                                     socket_base_t *socket_,
                                     auto_hwm_role_t role_,
                                     int socket_type_,
                                     size_t managed_connections_,
                                     size_t active_connections_,
                                     bool apply_sndhwm_,
                                     bool apply_rcvhwm_,
                                     bool connection_bucket_enabled_ = false)
{
    if (!ctx_ || !socket_)
        return;

    apply_spot_internal_auto_hwm (ctx_, socket_,
                                  spot_internal_auto_hwm_policy_t{
                                    role_, socket_type_, managed_connections_, active_connections_,
                                    0, 0, apply_sndhwm_, apply_rcvhwm_, auto_hwm_scope_none, 1, 0,
                                    connection_bucket_enabled_});
}

static void close_mesh_peer_observer (spot_node_t *node_, spot_data_plane_runtime_state_t *state_)
{
    if (!state_)
        return;
    if (state_->mesh_pub)
        (void) state_->mesh_pub->monitor (NULL, 0, 3, ZLINK_CORE_SOCKET_PAIR);
    if (state_->mesh_xsub)
        (void) state_->mesh_xsub->monitor (NULL, 0, 3, ZLINK_CORE_SOCKET_PAIR);
    spot_data_plane_t::close_socket_ptr (node_, state_->mesh_peer_observer.pub_monitor);
    spot_data_plane_t::close_socket_ptr (node_, state_->mesh_peer_observer.xsub_monitor);
}

static int open_mesh_peer_observer (spot_data_plane_runtime_state_t *state_)
{
    if (!state_) {
        errno = EFAULT;
        return -1;
    }

    const int monitor_events = ZLINK_EVENT_CONNECTION_READY | ZLINK_EVENT_DISCONNECTED;
    if (state_->mesh_pub) {
        state_->mesh_peer_observer.pub_monitor = static_cast<socket_base_t *> (
          open_socket_monitor_bridge (state_->mesh_pub, monitor_events));
        if (!state_->mesh_peer_observer.pub_monitor)
            return -1;
    }
    if (state_->mesh_xsub) {
        state_->mesh_peer_observer.xsub_monitor = static_cast<socket_base_t *> (
          open_socket_monitor_bridge (state_->mesh_xsub, monitor_events));
        if (!state_->mesh_peer_observer.xsub_monitor)
            return -1;
    }
    return 0;
}

static int add_mesh_peer_observer_to_poller (spot_data_plane_runtime_state_t *state_)
{
    if (!state_ || !state_->poller) {
        errno = EFAULT;
        return -1;
    }
    if (state_->mesh_peer_observer.xsub_monitor
        && state_->poller->add (state_->mesh_peer_observer.xsub_monitor, NULL, ZLINK_POLLIN) != 0)
        return -1;
    if (state_->mesh_peer_observer.pub_monitor
        && state_->poller->add (state_->mesh_peer_observer.pub_monitor, NULL, ZLINK_POLLIN) != 0)
        return -1;
    return 0;
}

static void close_runtime_sockets (spot_node_t *node_, spot_data_plane_runtime_state_t *state_)
{
    if (!state_)
        return;

    for (spot_data_plane_runtime_state_t::remote_mesh_state_t::target_map_t::iterator it =
           state_->remote_mesh.targets.begin ();
         it != state_->remote_mesh.targets.end (); ++it) {
        if (it->second.sender_socket && !it->second.route_endpoint.empty ())
            (void) it->second.sender_socket->term_endpoint (it->second.route_endpoint.c_str ());
        spot_data_plane_t::close_socket_ptr (node_, it->second.sender_socket);
    }
    state_->remote_mesh.targets.clear ();
    LIBZLINK_DELETE (state_->poller);
    state_->poller = NULL;
    spot_data_plane_t::close_socket_ptr (node_, state_->fanout);
    spot_data_plane_t::close_socket_ptr (node_, state_->external_router);
    spot_data_plane_t::close_socket_ptr (node_, state_->peer_ctrl_sub);
    spot_data_plane_t::close_socket_ptr (node_, state_->peer_ctrl_pub);
    close_mesh_peer_observer (node_, state_);
    spot_data_plane_t::close_socket_ptr (node_, state_->pub_ingress_sub);
    spot_data_plane_t::close_socket_ptr (node_, state_->mesh_xsub);
    spot_data_plane_t::close_socket_ptr (node_, state_->mesh_pub);
    spot_data_plane_t::close_socket_ptr (node_, state_->ctrl);
}

static int subscribe_runtime_mesh_topics (spot_runtime_t *runtime_, socket_base_t *mesh_xsub_)
{
    if (!runtime_ || !mesh_xsub_ || !runtime_->owner) {
        errno = EFAULT;
        return -1;
    }

    if (spot_data_plane_protocol_t::send_subscription_update (
          mesh_xsub_, spot_control_protocol::bootstrap_ctrl_descriptor_topic, true)
        != 0) {
        return -1;
    }

    return 0;
}

static int configure_runtime_sockets (spot_runtime_t *runtime_,
                                      spot_data_plane_runtime_state_t *state_)
{
    const int linger = 0;
    const int zero = 0;
    const int neg_one = -1;
    const int one = 1;
    const int submit_retry_mode = ZLINK_SUBMIT_RETRY_LOCAL_FAILURE;
    const int submit_retry_timeout = 100;
    const int submit_retry_attempts = 2;
    const spot_node_runtime_tuning_t runtime_tuning =
      runtime_ ? runtime_->runtime_tuning_snapshot () : spot_node_runtime_tuning_t ();
    const bool pubsub_hwm_override = spot_node_pubsub_hwm_overridden (runtime_tuning);
    const bool router_hwm_override = spot_node_router_hwm_overridden (runtime_tuning);
    const int pubsub_admission_hwm =
      pubsub_hwm_override ? resolve_pubsub_admission_hwm (runtime_) : 0;
    const int router_admission_hwm =
      router_hwm_override ? resolve_router_admission_hwm (runtime_) : 0;
    ctx_t *ctx = runtime_ ? runtime_->ctx () : NULL;
    size_t local_pub_count = 0;
    size_t local_sub_count = 0;
    size_t connected_peer_count = 0;
    size_t active_peer_count = 0;
    if (runtime_) {
        runtime_->snapshot_auto_hwm_inputs (&local_pub_count, &local_sub_count,
                                            &connected_peer_count, &active_peer_count);
    }
    zlink_routing_id_t node_rid;
    memset (&node_rid, 0, sizeof (node_rid));
    if (runtime_ && runtime_->owner && runtime_->owner->node_routing_id (&node_rid) == 0
        && node_rid.size > 0 && state_->external_router) {
        state_->external_router->setsockopt (ZLINK_INTERNAL_OPT_ROUTING_ID, node_rid.data,
                                             node_rid.size);
    }
    int mesh_xsub_rcvhwm = pubsub_admission_hwm;
    int mesh_pub_sndhwm = pubsub_admission_hwm;
    int peer_ctrl_rcvhwm = spot_internal_peer_ctrl_rcvhwm_default;
    int peer_ctrl_sndhwm = spot_internal_peer_ctrl_sndhwm_default;
    int external_router_rcvhwm = router_admission_hwm;
    int external_router_sndhwm = router_admission_hwm;
    int fanout_sndhwm = spot_data_plane_hwm_default;

    apply_common_internal_opts (state_->ctrl, linger);
    if (state_->mesh_pub)
        apply_common_internal_opts (state_->mesh_pub, linger);
    if (state_->mesh_xsub)
        apply_common_internal_opts (state_->mesh_xsub, linger);
    if (state_->pub_ingress_sub)
        apply_common_internal_opts (state_->pub_ingress_sub, linger);
    if (state_->peer_ctrl_pub)
        apply_common_internal_opts (state_->peer_ctrl_pub, linger);
    if (state_->peer_ctrl_sub)
        apply_common_internal_opts (state_->peer_ctrl_sub, linger);
    if (state_->external_router)
        apply_common_internal_opts (state_->external_router, linger);
    if (state_->fanout)
        apply_common_internal_opts (state_->fanout, linger);
    apply_internal_auto_hwm (ctx, state_->ctrl, auto_hwm_role_control, ZLINK_CORE_SOCKET_PAIR,
                             connected_peer_count, active_peer_count, false, false);
    apply_internal_auto_hwm (ctx, state_->mesh_pub, auto_hwm_role_spot_data, ZLINK_CORE_SOCKET_PUB,
                             connected_peer_count, active_peer_count, true, false, true);
    apply_internal_auto_hwm (ctx, state_->mesh_xsub, auto_hwm_role_recv_ingress,
                             ZLINK_CORE_SOCKET_XSUB, connected_peer_count, active_peer_count, false,
                             true, true);
    apply_internal_auto_hwm (ctx, state_->pub_ingress_sub, auto_hwm_role_recv_ingress,
                             ZLINK_CORE_SOCKET_SUB, local_pub_count, local_pub_count, false, true);
    apply_internal_auto_hwm (ctx, state_->peer_ctrl_pub, auto_hwm_role_control,
                             ZLINK_CORE_SOCKET_PUB, connected_peer_count, active_peer_count, false,
                             false);
    apply_internal_auto_hwm (ctx, state_->peer_ctrl_sub, auto_hwm_role_control,
                             ZLINK_CORE_SOCKET_SUB, connected_peer_count, active_peer_count, false,
                             false);
    apply_internal_auto_hwm (ctx, state_->external_router, auto_hwm_role_routed,
                             ZLINK_CORE_SOCKET_ROUTER, connected_peer_count, active_peer_count,
                             true, true, true);
    apply_internal_auto_hwm (ctx, state_->fanout, auto_hwm_role_spot_data, ZLINK_CORE_SOCKET_PUB,
                             local_sub_count, local_sub_count, false, false);

    state_->ctrl->connect (runtime_->data_ctrl_endpoint.c_str ());
    if (state_->fanout) {
        state_->fanout->setsockopt (ZLINK_INTERNAL_OPT_SNDHWM, &fanout_sndhwm,
                                    sizeof (fanout_sndhwm));
        state_->fanout->setsockopt (ZLINK_INTERNAL_OPT_SNDTIMEO, &zero, sizeof (zero));
        state_->fanout->setsockopt (ZLINK_INTERNAL_OPT_RCVHWM, &zero, sizeof (zero));
        state_->fanout->setsockopt (ZLINK_INTERNAL_OPT_XPUB_NODROP, &one, sizeof (one));
    }
    if (state_->mesh_pub) {
        if (pubsub_hwm_override) {
            state_->mesh_pub->setsockopt (ZLINK_INTERNAL_OPT_SNDHWM, &mesh_pub_sndhwm,
                                          sizeof (mesh_pub_sndhwm));
        }
        state_->mesh_pub->setsockopt (ZLINK_INTERNAL_OPT_SNDTIMEO, &zero, sizeof (zero));
    }
    if (state_->mesh_xsub) {
        if (pubsub_hwm_override) {
            state_->mesh_xsub->setsockopt (ZLINK_INTERNAL_OPT_RCVHWM, &mesh_xsub_rcvhwm,
                                           sizeof (mesh_xsub_rcvhwm));
        }
        state_->mesh_xsub->setsockopt (ZLINK_INTERNAL_OPT_SNDTIMEO, &neg_one, sizeof (neg_one));
    }
    if (state_->pub_ingress_sub) {
        if (pubsub_hwm_override) {
            state_->pub_ingress_sub->setsockopt (ZLINK_INTERNAL_OPT_RCVHWM, &mesh_xsub_rcvhwm,
                                                 sizeof (mesh_xsub_rcvhwm));
        }
        state_->pub_ingress_sub->setsockopt (ZLINK_INTERNAL_OPT_SUBSCRIBE, "", 0);
    }
    if (state_->peer_ctrl_pub) {
        state_->peer_ctrl_pub->setsockopt (ZLINK_INTERNAL_OPT_SNDHWM, &peer_ctrl_sndhwm,
                                           sizeof (peer_ctrl_sndhwm));
        state_->peer_ctrl_pub->setsockopt (ZLINK_INTERNAL_OPT_SNDTIMEO, &neg_one, sizeof (neg_one));
    }
    if (state_->peer_ctrl_sub) {
        state_->peer_ctrl_sub->setsockopt (ZLINK_INTERNAL_OPT_RCVHWM, &peer_ctrl_rcvhwm,
                                           sizeof (peer_ctrl_rcvhwm));
        state_->peer_ctrl_sub->setsockopt (ZLINK_INTERNAL_OPT_SUBSCRIBE,
                                           spot_control_protocol::ctrl_prefix,
                                           strlen (spot_control_protocol::ctrl_prefix));
    }
    if (state_->external_router) {
        if (router_hwm_override) {
            state_->external_router->setsockopt (ZLINK_INTERNAL_OPT_RCVHWM, &external_router_rcvhwm,
                                                 sizeof (external_router_rcvhwm));
            state_->external_router->setsockopt (ZLINK_INTERNAL_OPT_SNDHWM, &external_router_sndhwm,
                                                 sizeof (external_router_sndhwm));
        }
        state_->external_router->setsockopt (ZLINK_INTERNAL_OPT_RCVTIMEO, &neg_one,
                                             sizeof (neg_one));
        state_->external_router->setsockopt (ZLINK_INTERNAL_OPT_SNDTIMEO, &zero, sizeof (zero));
        state_->external_router->setsockopt (ZLINK_INTERNAL_OPT_SUBMIT_RETRY_MODE,
                                             &submit_retry_mode, sizeof (submit_retry_mode));
        state_->external_router->setsockopt (ZLINK_INTERNAL_OPT_SUBMIT_RETRY_TIMEOUT,
                                             &submit_retry_timeout, sizeof (submit_retry_timeout));
        state_->external_router->setsockopt (ZLINK_INTERNAL_OPT_SUBMIT_RETRY_ATTEMPTS,
                                             &submit_retry_attempts,
                                             sizeof (submit_retry_attempts));
    }
    state_->mesh_pub_hwm.current_sndhwm =
      state_->mesh_pub
          && read_socket_int_option (state_->mesh_pub, ZLINK_INTERNAL_OPT_SNDHWM, &mesh_pub_sndhwm)
        ? mesh_pub_sndhwm
        : 0;
    return 0;
}

}

spot_data_plane_runtime_state_t::spot_data_plane_runtime_state_t () :
    ctrl (NULL),
    mesh_pub (NULL),
    mesh_xsub (NULL),
    pub_ingress_sub (NULL),
    peer_ctrl_pub (NULL),
    peer_ctrl_sub (NULL),
    external_router (NULL),
    fanout (NULL),
    next_pending_message_id (0),
    last_attachment_version (UINT64_MAX),
    runtime_sockets_nodelay_applied (false),
    poller (NULL)
{
}

void spot_data_plane_t::close_socket_ptr (spot_node_t *node_, socket_base_t *&socket_)
{
    if (!socket_)
        return;
    if (!spot_node_access_t::ctx (node_)) {
        socket_->stop ();
        socket_->close ();
        socket_ = NULL;
        return;
    }
    (void) spot_node_access_t::close_owned_socket (node_, socket_, 2000);
}

void spot_data_plane_t::clear_runtime_socket_refs (spot_runtime_t *runtime_)
{
    if (!runtime_)
        return;
    runtime_->data_ctrl_back = NULL;
    runtime_->mesh_pub = NULL;
    runtime_->mesh_xsub = NULL;
    runtime_->pub_ingress_sub = NULL;
    runtime_->peer_ctrl_pub = NULL;
    runtime_->peer_ctrl_sub = NULL;
    runtime_->external_router = NULL;
    runtime_->local_fanout_xpub = NULL;
}

int spot_data_plane_t::initialize_runtime (spot_node_t *node_,
                                           spot_runtime_t *runtime_,
                                           spot_data_plane_runtime_state_t *state_out_)
{
    if (!node_ || !runtime_ || !state_out_) {
        errno = EINVAL;
        return -1;
    }

    const bool pubsub_enabled = spot_node_access_t::pubsub_enabled (node_);
    const bool routed_enabled = spot_node_access_t::routed_enabled (node_);

    state_out_->ctrl = spot_node_access_t::create_socket (node_, ZLINK_CORE_SOCKET_PAIR);
    if (pubsub_enabled) {
        state_out_->mesh_pub = spot_node_access_t::create_socket (node_, ZLINK_CORE_SOCKET_PUB);
        state_out_->mesh_xsub = spot_node_access_t::create_socket (node_, ZLINK_CORE_SOCKET_XSUB);
        state_out_->pub_ingress_sub =
          spot_node_access_t::create_socket (node_, ZLINK_CORE_SOCKET_SUB);
        state_out_->fanout = spot_node_access_t::create_socket (node_, ZLINK_CORE_SOCKET_PUB);
    }
    state_out_->peer_ctrl_pub = spot_node_access_t::create_socket (node_, ZLINK_CORE_SOCKET_PUB);
    state_out_->peer_ctrl_sub = spot_node_access_t::create_socket (node_, ZLINK_CORE_SOCKET_SUB);
    if (routed_enabled) {
        state_out_->external_router =
          spot_node_access_t::create_socket (node_, ZLINK_CORE_SOCKET_ROUTER);
    }

    if (!state_out_->ctrl
        || (pubsub_enabled
            && (!state_out_->mesh_pub || !state_out_->mesh_xsub || !state_out_->pub_ingress_sub
                || !state_out_->fanout))
        || !state_out_->peer_ctrl_pub || !state_out_->peer_ctrl_sub
        || (routed_enabled && !state_out_->external_router)) {
        const int err = errno != 0 ? errno : ENOMEM;
        if (state_out_->ctrl) {
            (void) state_out_->ctrl->connect (runtime_->data_ctrl_endpoint.c_str ());
            (void) spot_data_plane_protocol_t::send_errno_reply (state_out_->ctrl, err);
        }
        close_runtime_sockets (node_, state_out_);
        {
            scoped_lock_t lock (spot_node_access_t::sync (node_));
            clear_runtime_socket_refs (runtime_);
            runtime_->mark_fault (err);
        }
        spot_data_plane_protocol_t::clear_mesh_connected_endpoints (runtime_);
        errno = err;
        return -1;
    }

    state_out_->ctrl->set_auto_hwm_policy_enabled (false);
    if (state_out_->mesh_pub)
        state_out_->mesh_pub->set_auto_hwm_policy_enabled (false);
    if (state_out_->mesh_xsub)
        state_out_->mesh_xsub->set_auto_hwm_policy_enabled (false);
    if (state_out_->pub_ingress_sub)
        state_out_->pub_ingress_sub->set_auto_hwm_policy_enabled (false);
    state_out_->peer_ctrl_pub->set_auto_hwm_policy_enabled (false);
    state_out_->peer_ctrl_sub->set_auto_hwm_policy_enabled (false);
    if (state_out_->external_router)
        state_out_->external_router->set_auto_hwm_policy_enabled (false);
    if (state_out_->fanout)
        state_out_->fanout->set_auto_hwm_policy_enabled (false);

    {
        scoped_lock_t lock (spot_node_access_t::sync (node_));
        runtime_->data_ctrl_back = state_out_->ctrl;
        runtime_->mesh_pub = state_out_->mesh_pub;
        runtime_->mesh_xsub = state_out_->mesh_xsub;
        runtime_->pub_ingress_sub = state_out_->pub_ingress_sub;
        runtime_->peer_ctrl_pub = state_out_->peer_ctrl_pub;
        runtime_->peer_ctrl_sub = state_out_->peer_ctrl_sub;
        runtime_->external_router = state_out_->external_router;
        runtime_->local_fanout_xpub = state_out_->fanout;
        spot_node_access_t::track_owned_socket (node_, state_out_->ctrl);
        spot_node_access_t::track_owned_socket (node_, state_out_->mesh_pub);
        spot_node_access_t::track_owned_socket (node_, state_out_->mesh_xsub);
        spot_node_access_t::track_owned_socket (node_, state_out_->pub_ingress_sub);
        spot_node_access_t::track_owned_socket (node_, state_out_->peer_ctrl_pub);
        spot_node_access_t::track_owned_socket (node_, state_out_->peer_ctrl_sub);
        spot_node_access_t::track_owned_socket (node_, state_out_->external_router);
        spot_node_access_t::track_owned_socket (node_, state_out_->fanout);
    }

    configure_runtime_sockets (runtime_, state_out_);

    if (state_out_->external_router
        && zlink_spot_install_external_router_dispatch (node_, state_out_->external_router) != 0) {
        const int err = errno != 0 ? errno : EIO;
        (void) spot_data_plane_protocol_t::send_errno_reply (state_out_->ctrl, err);
        close_runtime_sockets (node_, state_out_);
        {
            scoped_lock_t lock (spot_node_access_t::sync (node_));
            clear_runtime_socket_refs (runtime_);
            runtime_->mark_fault (err);
        }
        spot_data_plane_protocol_t::clear_mesh_connected_endpoints (runtime_);
        errno = err;
        return -1;
    }

    if (open_mesh_peer_observer (state_out_) != 0) {
        const int err = errno != 0 ? errno : EIO;
        (void) spot_data_plane_protocol_t::send_errno_reply (state_out_->ctrl, err);
        close_runtime_sockets (node_, state_out_);
        {
            scoped_lock_t lock (spot_node_access_t::sync (node_));
            clear_runtime_socket_refs (runtime_);
            runtime_->mark_fault (err);
        }
        spot_data_plane_protocol_t::clear_mesh_connected_endpoints (runtime_);
        errno = err;
        return -1;
    }

    state_out_->poller = new (std::nothrow) socket_poller_t ();
    if (!state_out_->poller || state_out_->poller->add (state_out_->ctrl, NULL, ZLINK_POLLIN) != 0
        || (state_out_->mesh_pub && state_out_->poller->add (state_out_->mesh_pub, NULL, 0) != 0)
        || (state_out_->mesh_xsub
            && state_out_->poller->add (state_out_->mesh_xsub, NULL, ZLINK_POLLIN) != 0)
        || (state_out_->pub_ingress_sub
            && state_out_->poller->add (state_out_->pub_ingress_sub, NULL, ZLINK_POLLIN) != 0)
        || (state_out_->peer_ctrl_sub
            && state_out_->poller->add (state_out_->peer_ctrl_sub, NULL, ZLINK_POLLIN) != 0)
        || add_mesh_peer_observer_to_poller (state_out_) != 0
        || (state_out_->publish_ingress.signaler.valid ()
            && state_out_->poller->add_fd (state_out_->publish_ingress.signaler.get_fd (), NULL,
                                           ZLINK_POLLIN)
                 != 0)
        || (state_out_->routed_send.signaler.valid ()
            && state_out_->poller->add_fd (state_out_->routed_send.signaler.get_fd (), NULL,
                                           ZLINK_POLLIN)
                 != 0)
        || (state_out_->external_router_ingress.signaler.valid ()
            && state_out_->poller->add_fd (state_out_->external_router_ingress.signaler.get_fd (),
                                           NULL, ZLINK_POLLIN)
                 != 0)) {
        const int err = errno != 0 ? errno : ENOMEM;
        (void) spot_data_plane_protocol_t::send_errno_reply (state_out_->ctrl, err);
        close_runtime_sockets (node_, state_out_);
        {
            scoped_lock_t lock (spot_node_access_t::sync (node_));
            clear_runtime_socket_refs (runtime_);
            runtime_->mark_fault (err);
        }
        spot_data_plane_protocol_t::clear_mesh_connected_endpoints (runtime_);
        errno = err;
        return -1;
    }

    if ((state_out_->mesh_xsub
         && subscribe_runtime_mesh_topics (runtime_, state_out_->mesh_xsub) != 0)
        || (state_out_->pub_ingress_sub
            && state_out_->pub_ingress_sub->bind (runtime_->pub_ingress_endpoint.c_str ()) != 0)
        || (state_out_->fanout
            && state_out_->fanout->bind (runtime_->sub_fanout_endpoint.c_str ()) != 0)) {
        const int err = errno != 0 ? errno : EIO;
        (void) spot_data_plane_protocol_t::send_errno_reply (state_out_->ctrl, err);
        if (state_out_->mesh_pub)
            (void) state_out_->mesh_pub->monitor (NULL, 0, 3, ZLINK_CORE_SOCKET_PAIR);
        if (state_out_->mesh_xsub)
            (void) state_out_->mesh_xsub->monitor (NULL, 0, 3, ZLINK_CORE_SOCKET_PAIR);
        close_runtime_sockets (node_, state_out_);
        {
            scoped_lock_t lock (spot_node_access_t::sync (node_));
            clear_runtime_socket_refs (runtime_);
            runtime_->mark_fault (err);
        }
        spot_data_plane_protocol_t::clear_mesh_connected_endpoints (runtime_);
        errno = err;
        return -1;
    }

    if (spot_data_plane_protocol_t::send_ok_reply (state_out_->ctrl) != 0) {
        const int err = errno != 0 ? errno : EIO;
        if (state_out_->mesh_pub)
            (void) state_out_->mesh_pub->monitor (NULL, 0, 3, ZLINK_CORE_SOCKET_PAIR);
        if (state_out_->mesh_xsub)
            (void) state_out_->mesh_xsub->monitor (NULL, 0, 3, ZLINK_CORE_SOCKET_PAIR);
        close_runtime_sockets (node_, state_out_);
        {
            scoped_lock_t lock (spot_node_access_t::sync (node_));
            clear_runtime_socket_refs (runtime_);
            runtime_->mark_fault (err);
        }
        spot_data_plane_protocol_t::clear_mesh_connected_endpoints (runtime_);
        errno = err;
        return -1;
    }

    return 0;
}

void spot_data_plane_t::teardown_runtime (spot_node_t *node_,
                                          spot_runtime_t *runtime_,
                                          spot_data_plane_runtime_state_t *state_,
                                          spot_data_plane_protocol_state_t *protocol_state_)
{
    if (!node_ || !runtime_ || !state_)
        return;

    if (state_->external_router && state_->external_router->socket_msg_dispatch_active ())
        (void) state_->external_router->socket_msg_dispatch_stop ();

    spot_data_plane_protocol_t::clear_snapshot_sources (node_, protocol_state_);
    if (protocol_state_) {
        protocol_state_->outbound_ready_filters.clear ();
        for (std::map<std::string, std::string>::const_iterator it =
               protocol_state_->peer_ctrl_endpoints.begin ();
             it != protocol_state_->peer_ctrl_endpoints.end (); ++it) {
            if (!it->second.empty () && state_->peer_ctrl_pub)
                (void) state_->peer_ctrl_pub->term_endpoint (it->second.c_str ());
            if (!it->first.empty () && state_->mesh_xsub)
                (void) state_->mesh_xsub->term_endpoint (it->first.c_str ());
        }
        protocol_state_->peer_ctrl_endpoints.clear ();
    }

    {
        std::lock_guard<std::mutex> lock (state_->publish_ingress.mutex);
        state_->publish_ingress.closed = true;
        while (!state_->publish_ingress.messages.empty ()) {
            spot_clear_msg_parts (&state_->publish_ingress.messages.front ().parts);
            state_->publish_ingress.messages.pop_front ();
        }
        state_->publish_ingress.queued_bytes = 0;
        state_->publish_ingress.signal_armed = false;
        state_->publish_ingress.cv.notify_all ();
    }
    {
        std::lock_guard<std::mutex> lock (state_->routed_send.mutex);
        state_->routed_send.closed = true;
        while (!state_->routed_send.messages.empty ()) {
            zlink::request_reply::close_built_parts (&state_->routed_send.messages.front ().parts);
            state_->routed_send.messages.pop_front ();
        }
        state_->routed_send.retry_after_ms = 0;
        state_->routed_send.signal_armed = false;
        state_->routed_send.cv.notify_all ();
    }
    {
        std::lock_guard<std::mutex> lock (state_->external_router_ingress.mutex);
        state_->external_router_ingress.closed = true;
        while (!state_->external_router_ingress.messages.empty ()) {
            zlink::request_reply::close_built_parts (
              &state_->external_router_ingress.messages.front ().parts);
            state_->external_router_ingress.messages.pop_front ();
        }
        state_->external_router_ingress.signal_armed = false;
    }

    if (state_) {
        for (spot_data_plane_runtime_state_t::remote_mesh_state_t::target_map_t::iterator it =
               state_->remote_mesh.targets.begin ();
             it != state_->remote_mesh.targets.end (); ++it) {
            if (state_->poller && it->second.sender_socket)
                (void) state_->poller->remove (it->second.sender_socket);
            if (it->second.sender_socket && !it->second.route_endpoint.empty ())
                (void) it->second.sender_socket->term_endpoint (it->second.route_endpoint.c_str ());
            if (it->second.sender_socket)
                (void) spot_node_access_t::close_owned_socket_and_wait (
                  runtime_->owner, it->second.sender_socket, 1000);
        }
        state_->remote_mesh.targets.clear ();
        state_->remote_mesh.pending_messages.clear ();
    }

    LIBZLINK_DELETE (state_->poller);
    state_->poller = NULL;

    close_mesh_peer_observer (node_, state_);

    {
        scoped_lock_t lock (spot_node_access_t::sync (node_));
        runtime_->peer_ctrl_endpoint.clear ();
        runtime_->bound_endpoint.clear ();
    }
    (void) runtime_->clear_external_route_ids ();
    spot_mesh_pub_hwm_t::reset_runtime_state (runtime_);
    spot_data_plane_protocol_t::clear_mesh_connected_endpoints (runtime_);
}
}
