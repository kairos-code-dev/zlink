/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "services/spot/spot_control_protocol.hpp"
#include "services/spot/spot_data_plane.hpp"
#include "services/spot/spot_data_plane_internal.hpp"
#include "services/spot/spot_mesh_pub_budget.hpp"
#include "services/spot/spot_node.hpp"
#include "services/spot/spot_node_access.hpp"
#include "services/spot/spot_runtime.hpp"

#include "services/common/socket_monitor_bridge.hpp"
#include "sockets/socket_base.hpp"

#include <errno.h>
#include <string.h>

namespace zlink
{
namespace
{
static const size_t spot_sub_queue_hwm_default = 16;
static const int spot_internal_ingress_rcvhwm_default = 8192;
static const int spot_internal_mesh_xsub_rcvhwm_default = 8192;
static const int spot_internal_peer_ctrl_rcvhwm_default = 1024;

static int apply_common_internal_opts (socket_base_t *socket_, int linger_)
{
    return socket_->setsockopt (ZLINK_INTERNAL_OPT_LINGER, &linger_,
                                sizeof (linger_));
}

static void close_runtime_sockets (spot_node_t *node_,
                                   spot_data_plane_runtime_state_t *state_)
{
    if (!state_)
        return;

    spot_data_plane_t::close_socket_ptr (node_, state_->fanout);
    spot_data_plane_t::close_socket_ptr (node_, state_->ingress);
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
    const int ingress_rcvhwm =
      spot_data_plane_forwarder_t::resolve_internal_hwm_override (
        "ZLINK_SPOT_INTERNAL_INGRESS_RCVHWM",
        spot_internal_ingress_rcvhwm_default);
    const int mesh_xsub_rcvhwm =
      spot_data_plane_forwarder_t::resolve_internal_hwm_override (
        "ZLINK_SPOT_INTERNAL_MESH_XSUB_RCVHWM",
        spot_internal_mesh_xsub_rcvhwm_default);
    const int mesh_pub_sndhwm =
      spot_data_plane_forwarder_t::resolve_internal_hwm_override (
        "ZLINK_SPOT_INTERNAL_MESH_PUB_SNDHWM",
        spot_mesh_pub_budget_t::resolve_default (std::string (), 0));
    const int peer_ctrl_rcvhwm =
      spot_data_plane_forwarder_t::resolve_internal_hwm_override (
        "ZLINK_SPOT_INTERNAL_PEER_CTRL_RCVHWM",
        spot_internal_peer_ctrl_rcvhwm_default);
    const int fanout_sndhwm =
      spot_data_plane_forwarder_t::resolve_internal_hwm_override (
        "ZLINK_SPOT_INTERNAL_FANOUT_SNDHWM",
        static_cast<int> (spot_sub_queue_hwm_default));

    apply_common_internal_opts (state_->ctrl, linger);
    apply_common_internal_opts (state_->mesh_pub, linger);
    apply_common_internal_opts (state_->mesh_xsub, linger);
    apply_common_internal_opts (state_->peer_ctrl_pub, linger);
    apply_common_internal_opts (state_->peer_ctrl_sub, linger);
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
    ingress (NULL),
    fanout (NULL),
    current_mesh_pub_sndhwm (0),
    last_mesh_pub_budget_version (UINT64_MAX)
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
    state_out_->ingress = node_->_ctx->create_socket (ZLINK_CORE_SOCKET_SUB);
    state_out_->fanout = node_->_ctx->create_socket (ZLINK_CORE_SOCKET_PUB);

    if (!state_out_->ctrl || !state_out_->mesh_pub || !state_out_->mesh_xsub
        || !state_out_->peer_ctrl_pub || !state_out_->peer_ctrl_sub
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
        runtime_->local_pub_ingress_sub = state_out_->ingress;
        runtime_->local_fanout_xpub = state_out_->fanout;
        node_->track_owned_socket (state_out_->ctrl);
        node_->track_owned_socket (state_out_->mesh_pub);
        node_->track_owned_socket (state_out_->mesh_xsub);
        node_->track_owned_socket (state_out_->peer_ctrl_pub);
        node_->track_owned_socket (state_out_->peer_ctrl_sub);
        node_->track_owned_socket (state_out_->ingress);
        node_->track_owned_socket (state_out_->fanout);
    }
    configure_runtime_sockets (runtime_, state_out_);

    state_out_->mesh_xsub_monitor =
      static_cast<socket_base_t *> (open_socket_monitor_bridge (
        state_out_->mesh_xsub,
        ZLINK_EVENT_CONNECTION_READY_CHANGED | ZLINK_EVENT_DISCONNECTED));
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

    if (spot_data_plane_protocol_t::send_subscription_update (
          state_out_->mesh_xsub, "", true)
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
