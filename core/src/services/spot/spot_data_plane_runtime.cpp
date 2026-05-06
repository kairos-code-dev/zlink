/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "services/spot/spot_control_protocol.hpp"
#include "services/spot/spot_auto_hwm_internal.hpp"
#include "services/spot/spot_data_plane.hpp"
#include "services/spot/spot_data_plane_internal.hpp"
#include "services/spot/spot_mesh_pub_hwm.hpp"
#include "services/spot/spot_node.hpp"
#include "services/spot/spot_node_access.hpp"
#include "services/spot/spot_runtime.hpp"

#include "api/service_surface_internal.hpp"
#include "core/auto_hwm_policy.hpp"
#include "core/socket_poller.hpp"
#include "services/common/socket_monitor_bridge.hpp"
#include "sockets/socket_base.hpp"

#include <errno.h>
#include <string.h>

namespace zlink
{
namespace
{
static const int spot_data_plane_hwm_default = 0;
static const int spot_internal_peer_ctrl_sndhwm_default = 0;
static const int spot_internal_peer_ctrl_rcvhwm_default = 0;
static const int spot_internal_internal_router_sndhwm_default = 0;

static bool read_socket_int_option (socket_base_t *socket_,
                                    int option_,
                                    int *value_out_)
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
        return spot_node_admission_hwm_for_profile (
          ZLINK_AUTO_HWM_PROFILE_BALANCED);

    const spot_node_hwm_config_t hwm = runtime_->hwm_config_snapshot ();
    return spot_node_pubsub_admission_hwm (hwm);
}

static int resolve_router_admission_hwm (const spot_runtime_t *runtime_)
{
    if (!runtime_ || !runtime_->owner)
        return spot_node_admission_hwm_for_profile (
          ZLINK_AUTO_HWM_PROFILE_BALANCED);

    const spot_node_hwm_config_t hwm = runtime_->hwm_config_snapshot ();
    return spot_node_router_admission_hwm (hwm);
}

static int apply_common_internal_opts (socket_base_t *socket_, int linger_)
{
    return socket_->setsockopt (ZLINK_INTERNAL_OPT_LINGER, &linger_,
                                sizeof (linger_));
}

static void apply_internal_transport_buffers (
  ctx_t *ctx_,
  socket_base_t *socket_,
  auto_hwm_role_t role_,
  int socket_type_,
  size_t managed_connections_,
  size_t active_connections_)
{
    if (!ctx_ || !socket_)
        return;

    apply_spot_internal_auto_hwm (
      ctx_, socket_,
      spot_internal_auto_hwm_policy_t{role_, socket_type_, managed_connections_,
                                      active_connections_, 0, 0, false, false,
                                      true, true});
}

static void close_runtime_sockets (spot_node_t *node_,
                                   spot_data_plane_runtime_state_t *state_)
{
    if (!state_)
        return;

    for (spot_data_plane_runtime_state_t::remote_mesh_state_t::target_map_t::
           iterator it = state_->remote_mesh.targets.begin ();
         it != state_->remote_mesh.targets.end (); ++it) {
        if (it->second.sender_socket && !it->second.route_endpoint.empty ())
            (void) it->second.sender_socket->term_endpoint (
              it->second.route_endpoint.c_str ());
        spot_data_plane_t::close_socket_ptr (node_, it->second.sender_socket);
    }
    state_->remote_mesh.targets.clear ();
    LIBZLINK_DELETE (state_->poller);
    state_->poller = NULL;
    spot_data_plane_t::close_socket_ptr (node_, state_->fanout);
    spot_data_plane_t::close_socket_ptr (node_, state_->ingress);
    spot_data_plane_t::close_socket_ptr (node_, state_->internal_router);
    spot_data_plane_t::close_socket_ptr (node_, state_->external_router);
    spot_data_plane_t::close_socket_ptr (node_, state_->peer_ctrl_sub);
    spot_data_plane_t::close_socket_ptr (node_, state_->peer_ctrl_pub);
    spot_data_plane_t::close_socket_ptr (node_, state_->mesh_xsub_monitor);
    spot_data_plane_t::close_socket_ptr (node_, state_->mesh_xsub);
    spot_data_plane_t::close_socket_ptr (node_, state_->mesh_pub);
    spot_data_plane_t::close_socket_ptr (node_, state_->ctrl);
}

static int subscribe_runtime_mesh_topics (spot_runtime_t *runtime_,
                                          socket_base_t *mesh_xsub_)
{
    if (!runtime_ || !mesh_xsub_ || !runtime_->owner) {
        errno = EFAULT;
        return -1;
    }

    if (spot_data_plane_protocol_t::send_subscription_update (
          mesh_xsub_,
          spot_control_protocol::bootstrap_ctrl_descriptor_topic, true)
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
    const int pubsub_admission_hwm = resolve_pubsub_admission_hwm (runtime_);
    const int router_admission_hwm = resolve_router_admission_hwm (runtime_);
    ctx_t *ctx = runtime_ ? runtime_->ctx () : NULL;
    size_t local_pub_count = 0;
    size_t local_sub_count = 0;
    size_t connected_peer_count = 0;
    size_t active_peer_count = 0;
    if (runtime_) {
        runtime_->snapshot_auto_hwm_inputs (&local_pub_count, &local_sub_count,
                                            &connected_peer_count,
                                            &active_peer_count);
    }
    zlink_routing_id_t node_rid;
    memset (&node_rid, 0, sizeof (node_rid));
    if (runtime_ && runtime_->owner
        && runtime_->owner->node_routing_id (&node_rid) == 0
        && node_rid.size > 0 && state_->external_router) {
        state_->external_router->setsockopt (ZLINK_INTERNAL_OPT_ROUTING_ID,
                                                node_rid.data,
                                                node_rid.size);
    }
    int ingress_rcvhwm = pubsub_admission_hwm;
    int mesh_xsub_rcvhwm = pubsub_admission_hwm;
    int mesh_pub_sndhwm = pubsub_admission_hwm;
    int peer_ctrl_rcvhwm = spot_internal_peer_ctrl_rcvhwm_default;
    int peer_ctrl_sndhwm = spot_internal_peer_ctrl_sndhwm_default;
    int internal_router_rcvhwm = router_admission_hwm;
    int internal_router_sndhwm = spot_internal_internal_router_sndhwm_default;
    int external_router_rcvhwm = router_admission_hwm;
    int external_router_sndhwm = router_admission_hwm;
    int fanout_sndhwm = spot_data_plane_hwm_default;

    apply_common_internal_opts (state_->ctrl, linger);
    if (state_->mesh_pub)
        apply_common_internal_opts (state_->mesh_pub, linger);
    if (state_->mesh_xsub)
        apply_common_internal_opts (state_->mesh_xsub, linger);
    if (state_->peer_ctrl_pub)
        apply_common_internal_opts (state_->peer_ctrl_pub, linger);
    if (state_->peer_ctrl_sub)
        apply_common_internal_opts (state_->peer_ctrl_sub, linger);
    if (state_->external_router)
        apply_common_internal_opts (state_->external_router, linger);
    if (state_->internal_router)
        apply_common_internal_opts (state_->internal_router, linger);
    if (state_->ingress)
        apply_common_internal_opts (state_->ingress, linger);
    if (state_->fanout)
        apply_common_internal_opts (state_->fanout, linger);
    apply_internal_transport_buffers (ctx, state_->ctrl, auto_hwm_role_control,
                                      ZLINK_CORE_SOCKET_PAIR,
                                      connected_peer_count, active_peer_count);
    apply_internal_transport_buffers (ctx, state_->mesh_pub,
                                      auto_hwm_role_spot_data,
                                      ZLINK_CORE_SOCKET_PUB,
                                      connected_peer_count, active_peer_count);
    apply_internal_transport_buffers (ctx, state_->mesh_xsub,
                                      auto_hwm_role_recv_ingress,
                                      ZLINK_CORE_SOCKET_XSUB,
                                      connected_peer_count, active_peer_count);
    apply_internal_transport_buffers (ctx, state_->peer_ctrl_pub,
                                      auto_hwm_role_control,
                                      ZLINK_CORE_SOCKET_PUB,
                                      connected_peer_count, active_peer_count);
    apply_internal_transport_buffers (ctx, state_->peer_ctrl_sub,
                                      auto_hwm_role_control,
                                      ZLINK_CORE_SOCKET_SUB,
                                      connected_peer_count, active_peer_count);
    apply_internal_transport_buffers (ctx, state_->external_router,
                                      auto_hwm_role_routed,
                                      ZLINK_CORE_SOCKET_ROUTER,
                                      connected_peer_count, active_peer_count);
    apply_internal_transport_buffers (ctx, state_->internal_router,
                                      auto_hwm_role_routed,
                                      ZLINK_CORE_SOCKET_ROUTER,
                                      std::max<size_t> (
                                        std::max<size_t> (local_pub_count,
                                                          local_sub_count),
                                        1u),
                                      std::max<size_t> (
                                        std::max<size_t> (local_pub_count,
                                                          local_sub_count),
                                        1u));
    apply_internal_transport_buffers (ctx, state_->ingress,
                                      auto_hwm_role_recv_ingress,
                                      ZLINK_CORE_SOCKET_SUB, local_pub_count,
                                      local_pub_count);
    apply_internal_transport_buffers (ctx, state_->fanout,
                                      auto_hwm_role_spot_data,
                                      ZLINK_CORE_SOCKET_PUB, local_sub_count,
                                      local_sub_count);

    state_->ctrl->connect (runtime_->data_ctrl_endpoint.c_str ());
    if (state_->ingress) {
        state_->ingress->setsockopt (ZLINK_INTERNAL_OPT_RCVHWM,
                                      &ingress_rcvhwm,
                                      sizeof (ingress_rcvhwm));
        state_->ingress->setsockopt (ZLINK_INTERNAL_OPT_RCVTIMEO, &neg_one,
                                      sizeof (neg_one));
        state_->ingress->setsockopt (ZLINK_INTERNAL_OPT_SUBSCRIBE, "", 0);
    }
    if (state_->fanout) {
        state_->fanout->setsockopt (ZLINK_INTERNAL_OPT_SNDHWM, &fanout_sndhwm,
                                     sizeof (fanout_sndhwm));
        state_->fanout->setsockopt (ZLINK_INTERNAL_OPT_SNDTIMEO, &neg_one,
                                     sizeof (neg_one));
        state_->fanout->setsockopt (ZLINK_INTERNAL_OPT_RCVHWM, &zero,
                                     sizeof (zero));
        state_->fanout->setsockopt (ZLINK_INTERNAL_OPT_XPUB_NODROP, &one,
                                     sizeof (one));
    }
    if (state_->mesh_pub) {
        state_->mesh_pub->setsockopt (ZLINK_INTERNAL_OPT_SNDHWM,
                                       &mesh_pub_sndhwm,
                                       sizeof (mesh_pub_sndhwm));
        state_->mesh_pub->setsockopt (ZLINK_INTERNAL_OPT_SNDTIMEO, &neg_one,
                                       sizeof (neg_one));
    }
    if (state_->mesh_xsub) {
        state_->mesh_xsub->setsockopt (ZLINK_INTERNAL_OPT_RCVHWM,
                                        &mesh_xsub_rcvhwm,
                                        sizeof (mesh_xsub_rcvhwm));
        state_->mesh_xsub->setsockopt (ZLINK_INTERNAL_OPT_SNDTIMEO, &neg_one,
                                        sizeof (neg_one));
    }
    if (state_->peer_ctrl_pub) {
        state_->peer_ctrl_pub->setsockopt (ZLINK_INTERNAL_OPT_SNDHWM,
                                            &peer_ctrl_sndhwm,
                                            sizeof (peer_ctrl_sndhwm));
        state_->peer_ctrl_pub->setsockopt (ZLINK_INTERNAL_OPT_SNDTIMEO,
                                            &neg_one, sizeof (neg_one));
    }
    if (state_->peer_ctrl_sub) {
        state_->peer_ctrl_sub->setsockopt (ZLINK_INTERNAL_OPT_RCVHWM,
                                            &peer_ctrl_rcvhwm,
                                            sizeof (peer_ctrl_rcvhwm));
        state_->peer_ctrl_sub->setsockopt (
          ZLINK_INTERNAL_OPT_SUBSCRIBE, spot_control_protocol::ctrl_prefix,
          strlen (spot_control_protocol::ctrl_prefix));
    }
    if (state_->external_router) {
        state_->external_router->setsockopt (ZLINK_INTERNAL_OPT_RCVHWM,
                                              &external_router_rcvhwm,
                                              sizeof (external_router_rcvhwm));
        state_->external_router->setsockopt (ZLINK_INTERNAL_OPT_SNDHWM,
                                              &external_router_sndhwm,
                                              sizeof (external_router_sndhwm));
        state_->external_router->setsockopt (ZLINK_INTERNAL_OPT_RCVTIMEO,
                                              &neg_one, sizeof (neg_one));
        state_->external_router->setsockopt (ZLINK_INTERNAL_OPT_SNDTIMEO,
                                              &neg_one, sizeof (neg_one));
    }
    if (state_->internal_router) {
        state_->internal_router->setsockopt (ZLINK_INTERNAL_OPT_RCVHWM,
                                              &internal_router_rcvhwm,
                                              sizeof (internal_router_rcvhwm));
        state_->internal_router->setsockopt (ZLINK_INTERNAL_OPT_SNDHWM,
                                              &internal_router_sndhwm,
                                              sizeof (internal_router_sndhwm));
        state_->internal_router->setsockopt (ZLINK_INTERNAL_OPT_RCVTIMEO,
                                              &neg_one, sizeof (neg_one));
        state_->internal_router->setsockopt (ZLINK_INTERNAL_OPT_SNDTIMEO,
                                              &neg_one, sizeof (neg_one));
    }
    state_->mesh_pub_hwm.current_sndhwm =
      state_->mesh_pub
        && read_socket_int_option (state_->mesh_pub, ZLINK_INTERNAL_OPT_SNDHWM,
                                   &mesh_pub_sndhwm)
        ? mesh_pub_sndhwm
        : 0;
    return 0;
}

}

spot_data_plane_runtime_state_t::spot_data_plane_runtime_state_t () :
    ctrl (NULL),
    mesh_pub (NULL),
    mesh_xsub (NULL),
    mesh_xsub_monitor (NULL),
    peer_ctrl_pub (NULL),
    peer_ctrl_sub (NULL),
    external_router (NULL),
    internal_router (NULL),
    ingress (NULL),
    fanout (NULL),
    next_pending_message_id (0),
    last_attachment_version (UINT64_MAX),
    runtime_sockets_nodelay_applied (false),
    poller (NULL)
{
}

void spot_data_plane_t::close_socket_ptr (spot_node_t *node_,
                                          socket_base_t *&socket_)
{
    if (!socket_)
        return;
    if (!node_ || !node_->_ctx) {
        socket_->stop ();
        socket_->close ();
        socket_ = NULL;
        return;
    }
    (void) node_->_lifecycle.close_socket (socket_, 2000);
}

void spot_data_plane_t::clear_runtime_socket_refs (
  spot_runtime_t *runtime_)
{
    if (!runtime_)
        return;
    runtime_->data_ctrl_back = NULL;
    runtime_->mesh_pub = NULL;
    runtime_->mesh_xsub = NULL;
    runtime_->peer_ctrl_pub = NULL;
    runtime_->peer_ctrl_sub = NULL;
    runtime_->external_router = NULL;
    runtime_->internal_router = NULL;
    runtime_->local_pub_ingress_sub = NULL;
    runtime_->local_fanout_xpub = NULL;
}

int spot_data_plane_t::initialize_runtime (
  spot_node_t *node_,
  spot_runtime_t *runtime_,
  spot_data_plane_runtime_state_t *state_out_)
{
    if (!node_ || !runtime_ || !state_out_) {
        errno = EINVAL;
        return -1;
    }

    const bool pubsub_enabled = node_->pubsub_enabled ();
    const bool routed_enabled = node_->routed_enabled ();

    state_out_->ctrl = node_->_ctx->create_socket (ZLINK_CORE_SOCKET_PAIR);
    if (pubsub_enabled) {
        state_out_->mesh_pub = node_->_ctx->create_socket (ZLINK_CORE_SOCKET_PUB);
        state_out_->mesh_xsub =
          node_->_ctx->create_socket (ZLINK_CORE_SOCKET_XSUB);
        state_out_->ingress = node_->_ctx->create_socket (ZLINK_CORE_SOCKET_SUB);
        state_out_->fanout = node_->_ctx->create_socket (ZLINK_CORE_SOCKET_PUB);
    }
    state_out_->peer_ctrl_pub =
      node_->_ctx->create_socket (ZLINK_CORE_SOCKET_PUB);
    state_out_->peer_ctrl_sub =
      node_->_ctx->create_socket (ZLINK_CORE_SOCKET_SUB);
    if (routed_enabled) {
        state_out_->external_router =
          node_->_ctx->create_socket (ZLINK_CORE_SOCKET_ROUTER);
        state_out_->internal_router =
          node_->_ctx->create_socket (ZLINK_CORE_SOCKET_ROUTER);
    }

    if (!state_out_->ctrl
        || (pubsub_enabled
            && (!state_out_->mesh_pub || !state_out_->mesh_xsub
                || !state_out_->ingress || !state_out_->fanout))
        || !state_out_->peer_ctrl_pub || !state_out_->peer_ctrl_sub
        || (routed_enabled
            && (!state_out_->external_router || !state_out_->internal_router))) {
        const int err = errno != 0 ? errno : ENOMEM;
        if (state_out_->ctrl) {
            (void) state_out_->ctrl->connect (runtime_->data_ctrl_endpoint.c_str ());
            (void) spot_data_plane_protocol_t::send_errno_reply (state_out_->ctrl,
                                                                 err);
        }
        close_runtime_sockets (node_, state_out_);
        {
            scoped_lock_t lock (node_->_sync);
            clear_runtime_socket_refs (runtime_);
            runtime_->mark_fault (err);
        }
        spot_data_plane_protocol_t::clear_mesh_xsub_connected_endpoints (
          runtime_);
        errno = err;
        return -1;
    }

    state_out_->ctrl->set_auto_hwm_policy_enabled (false);
    if (state_out_->mesh_pub)
        state_out_->mesh_pub->set_auto_hwm_policy_enabled (false);
    if (state_out_->mesh_xsub)
        state_out_->mesh_xsub->set_auto_hwm_policy_enabled (false);
    state_out_->peer_ctrl_pub->set_auto_hwm_policy_enabled (false);
    state_out_->peer_ctrl_sub->set_auto_hwm_policy_enabled (false);
    if (state_out_->external_router)
        state_out_->external_router->set_auto_hwm_policy_enabled (false);
    if (state_out_->internal_router)
        state_out_->internal_router->set_auto_hwm_policy_enabled (false);
    if (state_out_->ingress)
        state_out_->ingress->set_auto_hwm_policy_enabled (false);
    if (state_out_->fanout)
        state_out_->fanout->set_auto_hwm_policy_enabled (false);

    {
        scoped_lock_t lock (node_->_sync);
        runtime_->data_ctrl_back = state_out_->ctrl;
        runtime_->mesh_pub = state_out_->mesh_pub;
        runtime_->mesh_xsub = state_out_->mesh_xsub;
        runtime_->peer_ctrl_pub = state_out_->peer_ctrl_pub;
        runtime_->peer_ctrl_sub = state_out_->peer_ctrl_sub;
        runtime_->external_router = state_out_->external_router;
        runtime_->internal_router = state_out_->internal_router;
        runtime_->local_pub_ingress_sub = state_out_->ingress;
        runtime_->local_fanout_xpub = state_out_->fanout;
        node_->track_owned_socket (state_out_->ctrl);
        node_->track_owned_socket (state_out_->mesh_pub);
        node_->track_owned_socket (state_out_->mesh_xsub);
        node_->track_owned_socket (state_out_->peer_ctrl_pub);
        node_->track_owned_socket (state_out_->peer_ctrl_sub);
        node_->track_owned_socket (state_out_->external_router);
        node_->track_owned_socket (state_out_->internal_router);
        node_->track_owned_socket (state_out_->ingress);
        node_->track_owned_socket (state_out_->fanout);
    }

    configure_runtime_sockets (runtime_, state_out_);

    if (state_out_->mesh_xsub)
        state_out_->mesh_xsub_monitor =
          static_cast<socket_base_t *> (open_socket_monitor_bridge (
            state_out_->mesh_xsub,
            ZLINK_EVENT_CONNECTION_READY | ZLINK_EVENT_DISCONNECTED));
    if (state_out_->mesh_xsub && !state_out_->mesh_xsub_monitor) {
        const int err = errno != 0 ? errno : EIO;
        (void) spot_data_plane_protocol_t::send_errno_reply (state_out_->ctrl,
                                                             err);
        close_runtime_sockets (node_, state_out_);
        {
            scoped_lock_t lock (node_->_sync);
            clear_runtime_socket_refs (runtime_);
            runtime_->mark_fault (err);
        }
        spot_data_plane_protocol_t::clear_mesh_xsub_connected_endpoints (
          runtime_);
        errno = err;
        return -1;
    }

    state_out_->poller = new (std::nothrow) socket_poller_t ();
    if (!state_out_->poller
        || state_out_->poller->add (state_out_->ctrl, NULL, ZLINK_POLLIN) != 0
        || (state_out_->ingress
            && state_out_->poller->add (state_out_->ingress, NULL, ZLINK_POLLIN)
                 != 0)
        || (state_out_->mesh_pub
            && state_out_->poller->add (state_out_->mesh_pub, NULL, 0) != 0)
        || (state_out_->mesh_xsub
            && state_out_->poller->add (state_out_->mesh_xsub, NULL,
                                        ZLINK_POLLIN)
                 != 0)
        || (state_out_->peer_ctrl_sub
            && state_out_->poller->add (state_out_->peer_ctrl_sub, NULL,
                                        ZLINK_POLLIN)
                 != 0)
        || (state_out_->external_router
            && state_out_->poller->add (state_out_->external_router, NULL,
                                        ZLINK_POLLIN)
                 != 0)
        || (state_out_->internal_router
            && state_out_->poller->add (state_out_->internal_router, NULL,
                                        ZLINK_POLLIN)
                 != 0)
        || (state_out_->mesh_xsub_monitor
            && state_out_->poller->add (state_out_->mesh_xsub_monitor, NULL,
                                        ZLINK_POLLIN)
                 != 0)) {
        const int err = errno != 0 ? errno : ENOMEM;
        (void) spot_data_plane_protocol_t::send_errno_reply (state_out_->ctrl,
                                                             err);
        if (state_out_->mesh_xsub)
            (void) state_out_->mesh_xsub->monitor (NULL, 0, 3,
                                                   ZLINK_CORE_SOCKET_PAIR);
        close_runtime_sockets (node_, state_out_);
        {
            scoped_lock_t lock (node_->_sync);
            clear_runtime_socket_refs (runtime_);
            runtime_->mark_fault (err);
        }
        spot_data_plane_protocol_t::clear_mesh_xsub_connected_endpoints (
          runtime_);
        errno = err;
        return -1;
    }

    if ((state_out_->mesh_xsub
         && subscribe_runtime_mesh_topics (runtime_, state_out_->mesh_xsub) != 0)
        || (state_out_->internal_router
            && state_out_->internal_router->bind (
                 runtime_->internal_router_endpoint.c_str ())
                 != 0)
        || (state_out_->ingress
            && state_out_->ingress->bind (runtime_->pub_ingress_endpoint.c_str ())
                 != 0)
        || (state_out_->fanout
            && state_out_->fanout->bind (runtime_->sub_fanout_endpoint.c_str ())
                 != 0)
        || (state_out_->external_router
            && zlink_spot_install_external_router_dispatch (
                 node_, state_out_->external_router)
                 != 0)) {
        const int err = errno != 0 ? errno : EIO;
        (void) spot_data_plane_protocol_t::send_errno_reply (state_out_->ctrl,
                                                             err);
        if (state_out_->mesh_xsub)
            (void) state_out_->mesh_xsub->monitor (NULL, 0, 3,
                                                   ZLINK_CORE_SOCKET_PAIR);
        close_runtime_sockets (node_, state_out_);
        {
            scoped_lock_t lock (node_->_sync);
            clear_runtime_socket_refs (runtime_);
            runtime_->mark_fault (err);
        }
        spot_data_plane_protocol_t::clear_mesh_xsub_connected_endpoints (
          runtime_);
        errno = err;
        return -1;
    }

    if (spot_data_plane_protocol_t::send_ok_reply (state_out_->ctrl) != 0) {
        const int err = errno != 0 ? errno : EIO;
        if (state_out_->mesh_xsub)
            (void) state_out_->mesh_xsub->monitor (NULL, 0, 3,
                                                   ZLINK_CORE_SOCKET_PAIR);
        close_runtime_sockets (node_, state_out_);
        {
            scoped_lock_t lock (node_->_sync);
            clear_runtime_socket_refs (runtime_);
            runtime_->mark_fault (err);
        }
        spot_data_plane_protocol_t::clear_mesh_xsub_connected_endpoints (
          runtime_);
        errno = err;
        return -1;
    }

    return 0;
}

void spot_data_plane_t::teardown_runtime (
  spot_node_t *node_,
  spot_runtime_t *runtime_,
  spot_data_plane_runtime_state_t *state_,
  spot_data_plane_protocol_state_t *protocol_state_)
{
    if (!node_ || !runtime_ || !state_)
        return;

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

    if (state_->external_router
        && state_->external_router->socket_msg_dispatch_active ())
        (void) state_->external_router->socket_msg_dispatch_stop ();

    if (state_) {
        for (spot_data_plane_runtime_state_t::remote_mesh_state_t::
               target_map_t::iterator it = state_->remote_mesh.targets.begin ();
             it != state_->remote_mesh.targets.end (); ++it) {
            if (state_->poller && it->second.sender_socket)
                (void) state_->poller->remove (it->second.sender_socket);
            if (it->second.sender_socket && !it->second.route_endpoint.empty ())
                (void) it->second.sender_socket->term_endpoint (
                  it->second.route_endpoint.c_str ());
            if (it->second.sender_socket)
                (void) spot_node_access_t::close_owned_socket_and_wait (
                  runtime_->owner, it->second.sender_socket, 1000);
        }
        state_->remote_mesh.targets.clear ();
        state_->remote_mesh.pending_messages.clear ();
    }

    LIBZLINK_DELETE (state_->poller);
    state_->poller = NULL;

    if (state_->mesh_xsub)
        (void) state_->mesh_xsub->monitor (NULL, 0, 3, ZLINK_CORE_SOCKET_PAIR);
    spot_data_plane_t::close_socket_ptr (node_, state_->mesh_xsub_monitor);

    {
        scoped_lock_t lock (node_->_sync);
        runtime_->peer_ctrl_endpoint.clear ();
        runtime_->bound_endpoint.clear ();
    }
    (void) runtime_->clear_external_route_ids ();
    spot_mesh_pub_hwm_t::reset_runtime_state (runtime_);
    spot_data_plane_protocol_t::clear_mesh_xsub_connected_endpoints (runtime_);
}
}
