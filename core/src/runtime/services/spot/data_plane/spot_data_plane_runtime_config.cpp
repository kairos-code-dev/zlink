/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/spot/common/spot_auto_hwm_internal.hpp"
#include "services/spot/common/spot_control_protocol.hpp"
#include "services/spot/data_plane/spot_data_plane_internal.hpp"
#include "services/spot/data_plane/spot_mesh_pub_hwm.hpp"
#include "services/spot/node/spot_node.hpp"
#include "services/spot/runtime/spot_runtime.hpp"

#include "core/auto_hwm_policy.hpp"
#include "sockets/common/socket_base.hpp"

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

    apply_spot_internal_auto_hwm (
      ctx_, socket_,
      spot_internal_auto_hwm_policy_t{role_, socket_type_, managed_connections_,
                                      active_connections_, 0, 0, apply_sndhwm_, apply_rcvhwm_,
                                      auto_hwm_scope_none, 1, 0, connection_bucket_enabled_});
}
}

int spot_data_plane_configure_runtime_sockets (spot_runtime_t *runtime_,
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
        && node_rid.size > 0 && state_->routed_router) {
        state_->routed_router->setsockopt (ZLINK_INTERNAL_OPT_ROUTING_ID, node_rid.data,
                                           node_rid.size);
    }
    zlink_routing_id_t mesh_pub_rid;
    memset (&mesh_pub_rid, 0, sizeof (mesh_pub_rid));
    if (runtime_ && runtime_->owner && runtime_->owner->pub_routing_id (&mesh_pub_rid)
        && mesh_pub_rid.size > 0) {
        if (state_->mesh_pub)
            state_->mesh_pub->setsockopt (ZLINK_INTERNAL_OPT_ROUTING_ID, mesh_pub_rid.data,
                                          mesh_pub_rid.size);
        if (state_->fanout)
            state_->fanout->setsockopt (ZLINK_INTERNAL_OPT_ROUTING_ID, mesh_pub_rid.data,
                                        mesh_pub_rid.size);
    }
    zlink_routing_id_t mesh_sub_rid;
    memset (&mesh_sub_rid, 0, sizeof (mesh_sub_rid));
    if (runtime_ && runtime_->owner && runtime_->owner->sub_routing_id (&mesh_sub_rid)
        && mesh_sub_rid.size > 0) {
        if (state_->mesh_xsub)
            state_->mesh_xsub->setsockopt (ZLINK_INTERNAL_OPT_ROUTING_ID, mesh_sub_rid.data,
                                           mesh_sub_rid.size);
        if (state_->pub_ingress_sub)
            state_->pub_ingress_sub->setsockopt (ZLINK_INTERNAL_OPT_ROUTING_ID, mesh_sub_rid.data,
                                                 mesh_sub_rid.size);
    }
    int mesh_xsub_rcvhwm = pubsub_admission_hwm;
    int mesh_pub_sndhwm = pubsub_admission_hwm;
    int peer_ctrl_rcvhwm = spot_internal_peer_ctrl_rcvhwm_default;
    int peer_ctrl_sndhwm = spot_internal_peer_ctrl_sndhwm_default;
    int routed_router_rcvhwm = router_admission_hwm;
    int routed_router_sndhwm = router_admission_hwm;
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
    if (state_->routed_router)
        apply_common_internal_opts (state_->routed_router, linger);
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
    apply_internal_auto_hwm (ctx, state_->routed_router, auto_hwm_role_routed,
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
    if (state_->routed_router) {
        if (router_hwm_override) {
            state_->routed_router->setsockopt (ZLINK_INTERNAL_OPT_RCVHWM, &routed_router_rcvhwm,
                                               sizeof (routed_router_rcvhwm));
            state_->routed_router->setsockopt (ZLINK_INTERNAL_OPT_SNDHWM, &routed_router_sndhwm,
                                               sizeof (routed_router_sndhwm));
        }
        state_->routed_router->setsockopt (ZLINK_INTERNAL_OPT_PROBE_ROUTER, &one, sizeof (one));
        state_->routed_router->setsockopt (ZLINK_INTERNAL_OPT_RCVTIMEO, &neg_one, sizeof (neg_one));
        state_->routed_router->setsockopt (ZLINK_INTERNAL_OPT_SNDTIMEO, &zero, sizeof (zero));
        state_->routed_router->setsockopt (ZLINK_INTERNAL_OPT_SUBMIT_RETRY_MODE, &submit_retry_mode,
                                           sizeof (submit_retry_mode));
        state_->routed_router->setsockopt (ZLINK_INTERNAL_OPT_SUBMIT_RETRY_TIMEOUT,
                                           &submit_retry_timeout, sizeof (submit_retry_timeout));
        state_->routed_router->setsockopt (ZLINK_INTERNAL_OPT_SUBMIT_RETRY_ATTEMPTS,
                                           &submit_retry_attempts, sizeof (submit_retry_attempts));
    }
    state_->mesh_pub_hwm.current_sndhwm =
      state_->mesh_pub
          && read_socket_int_option (state_->mesh_pub, ZLINK_INTERNAL_OPT_SNDHWM, &mesh_pub_sndhwm)
        ? mesh_pub_sndhwm
        : 0;
    return 0;
}
}
