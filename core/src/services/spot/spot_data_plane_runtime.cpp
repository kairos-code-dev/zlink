/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "services/spot/spot_control_protocol.hpp"
#include "services/spot/spot_data_plane.hpp"
#include "services/spot/spot_data_plane_internal.hpp"
#include "services/spot/spot_mesh_pub_budget.hpp"
#include "services/spot/spot_node.hpp"
#include "services/spot/spot_node_access.hpp"
#include "services/spot/spot_runtime.hpp"

#include "core/socket_poller.hpp"
#include "services/common/socket_monitor_bridge.hpp"
#include "sockets/socket_base.hpp"

#include <errno.h>
#include <string.h>

namespace zlink
{
namespace
{
static const int spot_data_plane_hwm_default = 1000;
static const int spot_internal_ingress_rcvhwm_default = 1000;
static const int spot_internal_mesh_xsub_rcvhwm_default = 1000;
static const int spot_internal_peer_ctrl_rcvhwm_default = 1024;
static const int spot_internal_route_ingress_rcvhwm_default = 1000;
static const int spot_internal_node_router_rcvhwm_default = 1000;
static const int spot_internal_node_router_sndhwm_default = 1000;

static int resolve_node_pub_sndhwm_default (const spot_runtime_t *runtime_)
{
    if (!runtime_ || !runtime_->owner)
        return spot_data_plane_hwm_default;

    const spot_node_hwm_config_t hwm = runtime_->hwm_config_snapshot ();
    if (hwm.topic_send_enabled)
        return hwm.topic_send_hwm > 0 ? hwm.topic_send_hwm : 0;

    const spot_node_t::pub_defaults_t defaults =
      runtime_->owner->load_pub_defaults ();
    if (!defaults.sndhwm.enabled || defaults.sndhwm.size == 0)
        return spot_data_plane_hwm_default;

    int value = spot_data_plane_hwm_default;
    memcpy (&value, &defaults.sndhwm.value,
            std::min (defaults.sndhwm.size, sizeof (value)));
    return value > 0 ? value : 0;
}

static int resolve_node_sub_rcvhwm_default (const spot_runtime_t *runtime_)
{
    if (!runtime_ || !runtime_->owner)
        return spot_data_plane_hwm_default;

    const spot_node_hwm_config_t hwm = runtime_->hwm_config_snapshot ();
    if (hwm.topic_recv_enabled)
        return hwm.topic_recv_hwm > 0 ? hwm.topic_recv_hwm : 0;

    const spot_node_t::sub_defaults_t defaults =
      runtime_->owner->load_sub_defaults ();
    if (!defaults.rcvhwm.enabled || defaults.rcvhwm.size == 0)
        return spot_data_plane_hwm_default;

    int value = spot_data_plane_hwm_default;
    memcpy (&value, &defaults.rcvhwm.value,
            std::min (defaults.rcvhwm.size, sizeof (value)));
    return value > 0 ? value : 0;
}

static int apply_common_internal_opts (socket_base_t *socket_, int linger_)
{
    return socket_->setsockopt (ZLINK_INTERNAL_OPT_LINGER, &linger_,
                                sizeof (linger_));
}

static int resolve_routed_send_hwm_default (const spot_runtime_t *runtime_,
                                            int topic_send_hwm_)
{
    if (!runtime_)
        return topic_send_hwm_;

    const spot_node_hwm_config_t hwm = runtime_->hwm_config_snapshot ();
    if (hwm.routed_send_enabled)
        return hwm.routed_send_hwm > 0 ? hwm.routed_send_hwm : 0;
    return topic_send_hwm_;
}

static int resolve_routed_recv_hwm_default (const spot_runtime_t *runtime_,
                                            int topic_recv_hwm_)
{
    if (!runtime_)
        return topic_recv_hwm_;

    const spot_node_hwm_config_t hwm = runtime_->hwm_config_snapshot ();
    if (hwm.routed_recv_enabled)
        return hwm.routed_recv_hwm > 0 ? hwm.routed_recv_hwm : 0;
    return topic_recv_hwm_;
}

static void close_runtime_sockets (spot_node_t *node_,
                                   spot_data_plane_runtime_state_t *state_)
{
    if (!state_)
        return;

    LIBZLINK_DELETE (state_->poller);
    state_->poller = NULL;
    spot_data_plane_t::close_socket_ptr (node_, state_->fanout);
    spot_data_plane_t::close_socket_ptr (node_, state_->ingress);
    spot_data_plane_t::close_socket_ptr (node_, state_->node_router_tx);
    spot_data_plane_t::close_socket_ptr (node_, state_->route_ingress_tx);
    spot_data_plane_t::close_socket_ptr (node_, state_->node_router);
    spot_data_plane_t::close_socket_ptr (node_, state_->route_ingress);
    spot_data_plane_t::close_socket_ptr (node_, state_->peer_ctrl_sub);
    spot_data_plane_t::close_socket_ptr (node_, state_->peer_ctrl_pub);
    spot_data_plane_t::close_socket_ptr (node_, state_->mesh_xsub_monitor);
    spot_data_plane_t::close_socket_ptr (node_, state_->mesh_xsub);
    spot_data_plane_t::close_socket_ptr (node_, state_->mesh_pub);
    spot_data_plane_t::close_socket_ptr (node_, state_->ctrl);
}

static int configure_runtime_sockets (spot_runtime_t *runtime_,
                                      spot_data_plane_runtime_state_t *state_)
{
    const int linger = 0;
    const int zero = 0;
    const int neg_one = -1;
    const int one = 1;
    const int node_pub_sndhwm = resolve_node_pub_sndhwm_default (runtime_);
    const int node_sub_rcvhwm = resolve_node_sub_rcvhwm_default (runtime_);
    const int routed_send_hwm =
      resolve_routed_send_hwm_default (runtime_, node_pub_sndhwm);
    const int routed_recv_hwm =
      resolve_routed_recv_hwm_default (runtime_, node_sub_rcvhwm);
    const int ingress_rcvhwm =
      spot_data_plane_forwarder_t::resolve_internal_hwm_override (
        "ZLINK_SPOT_INTERNAL_INGRESS_RCVHWM",
        node_sub_rcvhwm > 0 ? node_sub_rcvhwm
                            : spot_internal_ingress_rcvhwm_default);
    const int mesh_xsub_rcvhwm =
      spot_data_plane_forwarder_t::resolve_internal_hwm_override (
        "ZLINK_SPOT_INTERNAL_MESH_XSUB_RCVHWM",
        node_sub_rcvhwm > 0 ? node_sub_rcvhwm
                            : spot_internal_mesh_xsub_rcvhwm_default);
    const int mesh_pub_sndhwm =
      spot_data_plane_forwarder_t::resolve_internal_hwm_override (
        "ZLINK_SPOT_INTERNAL_MESH_PUB_SNDHWM",
        node_pub_sndhwm > 0 ? node_pub_sndhwm
                            : spot_mesh_pub_budget_t::resolve_default (
                                std::string (), 0));
    const int peer_ctrl_rcvhwm =
      spot_data_plane_forwarder_t::resolve_internal_hwm_override (
        "ZLINK_SPOT_INTERNAL_PEER_CTRL_RCVHWM",
        spot_internal_peer_ctrl_rcvhwm_default);
    const int route_ingress_rcvhwm =
      spot_data_plane_forwarder_t::resolve_internal_hwm_override (
        "ZLINK_SPOT_INTERNAL_ROUTE_INGRESS_RCVHWM",
        routed_recv_hwm > 0 ? routed_recv_hwm
                            : spot_internal_route_ingress_rcvhwm_default);
    const int node_router_rcvhwm =
      spot_data_plane_forwarder_t::resolve_internal_hwm_override (
        "ZLINK_SPOT_INTERNAL_NODE_ROUTER_RCVHWM",
        routed_recv_hwm > 0 ? routed_recv_hwm
                            : spot_internal_node_router_rcvhwm_default);
    const int node_router_sndhwm =
      spot_data_plane_forwarder_t::resolve_internal_hwm_override (
        "ZLINK_SPOT_INTERNAL_NODE_ROUTER_SNDHWM",
        routed_send_hwm > 0 ? routed_send_hwm
                            : spot_internal_node_router_sndhwm_default);
    const int fanout_sndhwm =
      spot_data_plane_forwarder_t::resolve_internal_hwm_override (
        "ZLINK_SPOT_INTERNAL_FANOUT_SNDHWM",
        node_pub_sndhwm > 0 ? node_pub_sndhwm : spot_data_plane_hwm_default);

    apply_common_internal_opts (state_->ctrl, linger);
    apply_common_internal_opts (state_->mesh_pub, linger);
    apply_common_internal_opts (state_->mesh_xsub, linger);
    apply_common_internal_opts (state_->peer_ctrl_pub, linger);
    apply_common_internal_opts (state_->peer_ctrl_sub, linger);
    apply_common_internal_opts (state_->route_ingress, linger);
    apply_common_internal_opts (state_->node_router, linger);
    apply_common_internal_opts (state_->ingress, linger);
    apply_common_internal_opts (state_->fanout, linger);

    state_->ctrl->connect (runtime_->data_ctrl_endpoint.c_str ());
    state_->ingress->setsockopt (ZLINK_INTERNAL_OPT_RCVHWM, &ingress_rcvhwm,
                                 sizeof (ingress_rcvhwm));
    state_->ingress->setsockopt (ZLINK_INTERNAL_OPT_RCVTIMEO, &neg_one,
                                 sizeof (neg_one));
    state_->ingress->setsockopt (ZLINK_INTERNAL_OPT_SUBSCRIBE, "", 0);
    state_->fanout->setsockopt (ZLINK_INTERNAL_OPT_SNDHWM, &fanout_sndhwm,
                                sizeof (fanout_sndhwm));
    state_->fanout->setsockopt (ZLINK_INTERNAL_OPT_SNDTIMEO, &neg_one,
                                sizeof (neg_one));
    state_->fanout->setsockopt (ZLINK_INTERNAL_OPT_RCVHWM, &zero,
                                sizeof (zero));
    state_->fanout->setsockopt (ZLINK_INTERNAL_OPT_XPUB_NODROP, &one,
                                sizeof (one));
    state_->mesh_pub->setsockopt (ZLINK_INTERNAL_OPT_SNDHWM, &mesh_pub_sndhwm,
                                  sizeof (mesh_pub_sndhwm));
    state_->mesh_pub->setsockopt (ZLINK_INTERNAL_OPT_SNDTIMEO, &neg_one,
                                  sizeof (neg_one));
    state_->mesh_xsub->setsockopt (ZLINK_INTERNAL_OPT_RCVHWM,
                                   &mesh_xsub_rcvhwm,
                                   sizeof (mesh_xsub_rcvhwm));
    state_->mesh_xsub->setsockopt (ZLINK_INTERNAL_OPT_SNDTIMEO, &neg_one,
                                   sizeof (neg_one));
    state_->peer_ctrl_pub->setsockopt (ZLINK_INTERNAL_OPT_SNDTIMEO, &neg_one,
                                       sizeof (neg_one));
    state_->peer_ctrl_sub->setsockopt (ZLINK_INTERNAL_OPT_RCVHWM,
                                       &peer_ctrl_rcvhwm,
                                       sizeof (peer_ctrl_rcvhwm));
    state_->peer_ctrl_sub->setsockopt (ZLINK_INTERNAL_OPT_SUBSCRIBE,
                                       spot_control_protocol::ctrl_prefix,
                                       strlen (
                                         spot_control_protocol::ctrl_prefix));
    state_->route_ingress->setsockopt (ZLINK_INTERNAL_OPT_RCVHWM,
                                       &route_ingress_rcvhwm,
                                       sizeof (route_ingress_rcvhwm));
    state_->route_ingress->setsockopt (ZLINK_INTERNAL_OPT_RCVTIMEO, &neg_one,
                                       sizeof (neg_one));
    state_->node_router->setsockopt (ZLINK_INTERNAL_OPT_RCVHWM,
                                     &node_router_rcvhwm,
                                     sizeof (node_router_rcvhwm));
    state_->node_router->setsockopt (ZLINK_INTERNAL_OPT_SNDHWM,
                                     &node_router_sndhwm,
                                     sizeof (node_router_sndhwm));
    state_->node_router->setsockopt (ZLINK_INTERNAL_OPT_RCVTIMEO, &neg_one,
                                     sizeof (neg_one));
    state_->node_router->setsockopt (ZLINK_INTERNAL_OPT_SNDTIMEO, &neg_one,
                                     sizeof (neg_one));
    state_->current_mesh_pub_sndhwm = mesh_pub_sndhwm;
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
    route_ingress (NULL),
    node_router (NULL),
    route_ingress_tx (NULL),
    node_router_tx (NULL),
    ingress (NULL),
    fanout (NULL),
    current_mesh_pub_sndhwm (0),
    last_mesh_pub_budget_version (UINT64_MAX),
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
    runtime_->route_ingress = NULL;
    runtime_->node_router = NULL;
    runtime_->route_ingress_tx = NULL;
    runtime_->node_router_tx = NULL;
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

    state_out_->ctrl = node_->_ctx->create_socket (ZLINK_CORE_SOCKET_PAIR);
    state_out_->mesh_pub = node_->_ctx->create_socket (ZLINK_CORE_SOCKET_PUB);
    state_out_->mesh_xsub = node_->_ctx->create_socket (ZLINK_CORE_SOCKET_XSUB);
    state_out_->peer_ctrl_pub =
      node_->_ctx->create_socket (ZLINK_CORE_SOCKET_PUB);
    state_out_->peer_ctrl_sub =
      node_->_ctx->create_socket (ZLINK_CORE_SOCKET_SUB);
    state_out_->route_ingress =
      node_->_ctx->create_socket (ZLINK_CORE_SOCKET_ROUTER);
    state_out_->node_router =
      node_->_ctx->create_socket (ZLINK_CORE_SOCKET_ROUTER);
    state_out_->route_ingress_tx = NULL;
    state_out_->node_router_tx = NULL;
    state_out_->ingress = node_->_ctx->create_socket (ZLINK_CORE_SOCKET_SUB);
    state_out_->fanout = node_->_ctx->create_socket (ZLINK_CORE_SOCKET_PUB);

    if (!state_out_->ctrl || !state_out_->mesh_pub || !state_out_->mesh_xsub
        || !state_out_->peer_ctrl_pub || !state_out_->peer_ctrl_sub
        || !state_out_->route_ingress || !state_out_->node_router
        || !state_out_->ingress || !state_out_->fanout) {
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

    {
        scoped_lock_t lock (node_->_sync);
        runtime_->data_ctrl_back = state_out_->ctrl;
        runtime_->mesh_pub = state_out_->mesh_pub;
        runtime_->mesh_xsub = state_out_->mesh_xsub;
        runtime_->peer_ctrl_pub = state_out_->peer_ctrl_pub;
        runtime_->peer_ctrl_sub = state_out_->peer_ctrl_sub;
        runtime_->route_ingress = state_out_->route_ingress;
        runtime_->node_router = state_out_->node_router;
        runtime_->route_ingress_tx = NULL;
        runtime_->node_router_tx = NULL;
        runtime_->local_pub_ingress_sub = state_out_->ingress;
        runtime_->local_fanout_xpub = state_out_->fanout;
        node_->track_owned_socket (state_out_->ctrl);
        node_->track_owned_socket (state_out_->mesh_pub);
        node_->track_owned_socket (state_out_->mesh_xsub);
        node_->track_owned_socket (state_out_->peer_ctrl_pub);
        node_->track_owned_socket (state_out_->peer_ctrl_sub);
        node_->track_owned_socket (state_out_->route_ingress);
        node_->track_owned_socket (state_out_->node_router);
        node_->track_owned_socket (state_out_->ingress);
        node_->track_owned_socket (state_out_->fanout);
    }
    configure_runtime_sockets (runtime_, state_out_);

    state_out_->mesh_xsub_monitor =
      static_cast<socket_base_t *> (open_socket_monitor_bridge (
        state_out_->mesh_xsub,
        ZLINK_EVENT_CONNECTION_READY | ZLINK_EVENT_DISCONNECTED));
    if (!state_out_->mesh_xsub_monitor) {
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
        || state_out_->poller->add (state_out_->ingress, NULL, ZLINK_POLLIN)
             != 0
        || state_out_->poller->add (state_out_->mesh_xsub, NULL, ZLINK_POLLIN)
             != 0
        || state_out_->poller->add (state_out_->peer_ctrl_sub, NULL,
                                    ZLINK_POLLIN)
             != 0
        || state_out_->poller->add (state_out_->route_ingress, NULL,
                                    ZLINK_POLLIN)
             != 0
        || state_out_->poller->add (state_out_->node_router, NULL,
                                    ZLINK_POLLIN)
             != 0
        || state_out_->poller->add (state_out_->mesh_xsub_monitor, NULL,
                                    ZLINK_POLLIN)
             != 0) {
        const int err = errno != 0 ? errno : ENOMEM;
        (void) spot_data_plane_protocol_t::send_errno_reply (state_out_->ctrl,
                                                             err);
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

    if (spot_data_plane_protocol_t::send_subscription_update (
          state_out_->mesh_xsub, "", true)
          != 0
        || state_out_->route_ingress->bind (
             runtime_->route_ingress_endpoint.c_str ())
             != 0
        || state_out_->node_router->bind (runtime_->node_router_endpoint.c_str ())
             != 0
        || state_out_->ingress->bind (runtime_->pub_ingress_endpoint.c_str ())
             != 0
        || state_out_->fanout->bind (runtime_->sub_fanout_endpoint.c_str ())
             != 0) {
        const int err = errno != 0 ? errno : EIO;
        (void) spot_data_plane_protocol_t::send_errno_reply (state_out_->ctrl,
                                                             err);
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

    if (state_->mesh_xsub)
        (void) state_->mesh_xsub->monitor (NULL, 0, 3, ZLINK_CORE_SOCKET_PAIR);
    spot_data_plane_t::close_socket_ptr (node_, state_->mesh_xsub_monitor);

    {
        scoped_lock_t lock (node_->_sync);
        runtime_->peer_ctrl_endpoint.clear ();
        runtime_->bound_endpoint.clear ();
    }
    spot_mesh_pub_budget_t::reset_runtime_state (runtime_);
    spot_data_plane_protocol_t::clear_mesh_xsub_connected_endpoints (runtime_);
}
}
