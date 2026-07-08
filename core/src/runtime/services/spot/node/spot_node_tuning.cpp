/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/spot/node/spot_node.hpp"
#include "services/spot/common/spot_auto_hwm_internal.hpp"
#include "services/spot/dispatch/spot_dispatch_worker_pool.hpp"
#include "services/spot/runtime/spot_runtime.hpp"
#include "core/auto_hwm_policy.hpp"
#include "sockets/common/socket_base.hpp"

namespace zlink
{
namespace
{
static void copy_int_option_value (const void *optval_, size_t optvallen_, int *out_)
{
    if (!optval_ || !out_ || optvallen_ == 0)
        return;

    *out_ = 0;
    memcpy (out_, optval_, std::min (optvallen_, sizeof (*out_)));
}

static void refresh_runtime_pubsub_admission_hwm (spot_runtime_t *runtime_, int hwm_)
{
    if (!runtime_)
        return;

    if (runtime_->mesh_pub)
        (void) runtime_->mesh_pub->setsockopt (ZLINK_INTERNAL_OPT_SNDHWM, &hwm_, sizeof (hwm_));
    if (runtime_->mesh_xsub)
        (void) runtime_->mesh_xsub->setsockopt (ZLINK_INTERNAL_OPT_RCVHWM, &hwm_, sizeof (hwm_));
}

static void refresh_runtime_router_admission_hwm (spot_runtime_t *runtime_, int hwm_)
{
    if (!runtime_)
        return;

    if (runtime_->routed_router) {
        (void) runtime_->routed_router->setsockopt (ZLINK_INTERNAL_OPT_SNDHWM, &hwm_,
                                                    sizeof (hwm_));
        (void) runtime_->routed_router->setsockopt (ZLINK_INTERNAL_OPT_RCVHWM, &hwm_,
                                                    sizeof (hwm_));
    }
}

static void
refresh_runtime_auto_hwm_msg_unit (spot_runtime_t *runtime_, const void *optval_, size_t optvallen_)
{
    if (!runtime_ || !optval_ || optvallen_ == 0)
        return;

    int msg_unit = 0;
    copy_int_option_value (optval_, optvallen_, &msg_unit);

    socket_base_t *sockets[] = {runtime_->mesh_pub,      runtime_->local_fanout_xpub,
                                runtime_->mesh_xsub,     runtime_->peer_ctrl_pub,
                                runtime_->peer_ctrl_sub, runtime_->routed_router};

    for (size_t i = 0; i != sizeof (sockets) / sizeof (sockets[0]); ++i) {
        socket_base_t *socket = sockets[i];
        if (!socket)
            continue;
        (void) socket->setsockopt (ZLINK_INTERNAL_OPT_AUTO_HWM_MSG_UNIT_BYTES, optval_, optvallen_);
    }

    ctx_t *ctx = runtime_->ctx ();
    if (!ctx)
        return;

    size_t local_pub_count = 0;
    size_t local_sub_count = 0;
    size_t connected_peer_count = 0;
    size_t active_peer_count = 0;
    runtime_->snapshot_auto_hwm_inputs (&local_pub_count, &local_sub_count, &connected_peer_count,
                                        &active_peer_count);
    const spot_node_runtime_tuning_t hwm = runtime_->runtime_tuning_snapshot ();
    apply_spot_runtime_hwm_policy (ctx, hwm, local_sub_count, connected_peer_count,
                                   active_peer_count, msg_unit, runtime_->mesh_pub,
                                   runtime_->local_fanout_xpub, runtime_->mesh_xsub,
                                   runtime_->routed_router, NULL);
}

static bool apply_runtime_tuning_option (spot_node_runtime_tuning_t *config_,
                                         int option_,
                                         const void *optval_,
                                         size_t optvallen_)
{
    if (!config_ || !optval_ || optvallen_ == 0 || optvallen_ > sizeof (int))
        return false;

    int value = 0;
    copy_int_option_value (optval_, optvallen_, &value);
    switch (option_) {
        case ZLINK_SPOT_NODE_OPT_ROUTER_HWM_PROFILE:
            if (!spot_node_valid_hwm_profile (value))
                return false;
            config_->router_profile = static_cast<zlink_auto_hwm_profile_t> (value);
            return true;
        case ZLINK_SPOT_NODE_OPT_ROUTER_HWM:
            if (value < 0)
                return false;
            config_->router_hwm_override = value;
            return true;
        case ZLINK_SPOT_NODE_OPT_PUBSUB_HWM_PROFILE:
            if (!spot_node_valid_hwm_profile (value))
                return false;
            config_->pubsub_profile = static_cast<zlink_auto_hwm_profile_t> (value);
            return true;
        case ZLINK_SPOT_NODE_OPT_PUBSUB_HWM:
            if (value < 0)
                return false;
            config_->pubsub_hwm_override = value;
            return true;
        case ZLINK_SPOT_NODE_OPT_DISPATCH_WORKERS_MIN:
            if (value < 1)
                return false;
            config_->dispatch_workers_min = value;
            if (config_->dispatch_workers_max > 0 && config_->dispatch_workers_max < value)
                config_->dispatch_workers_max = value;
            return true;
        case ZLINK_SPOT_NODE_OPT_DISPATCH_WORKERS_MAX:
            if (value < 1)
                return false;
            config_->dispatch_workers_max = value;
            if (config_->dispatch_workers_min > value)
                return false;
            return true;
        default:
            return false;
    }
}

static bool read_runtime_tuning_option (ctx_t *ctx_,
                                        const spot_runtime_t *runtime_,
                                        const spot_node_runtime_tuning_t &config_,
                                        int option_,
                                        int *value_out_)
{
    if (!value_out_)
        return false;
    LIBZLINK_UNUSED (ctx_);
    LIBZLINK_UNUSED (runtime_);

    switch (option_) {
        case ZLINK_SPOT_NODE_OPT_ROUTER_HWM_PROFILE:
            *value_out_ = config_.router_profile;
            return true;
        case ZLINK_SPOT_NODE_OPT_ROUTER_HWM:
            *value_out_ = spot_node_router_admission_hwm (config_);
            return true;
        case ZLINK_SPOT_NODE_OPT_PUBSUB_HWM_PROFILE:
            *value_out_ = config_.pubsub_profile;
            return true;
        case ZLINK_SPOT_NODE_OPT_PUBSUB_HWM:
            *value_out_ = spot_node_pubsub_admission_hwm (config_);
            return true;
        case ZLINK_SPOT_NODE_OPT_DISPATCH_WORKERS_MIN:
            *value_out_ = config_.dispatch_workers_min > 0 ? config_.dispatch_workers_min
                                                           : spot_dispatch_default_workers_min ();
            return true;
        case ZLINK_SPOT_NODE_OPT_DISPATCH_WORKERS_MAX:
            *value_out_ = config_.dispatch_workers_max > 0 ? config_.dispatch_workers_max
                                                           : spot_dispatch_default_workers_max ();
            return true;
        default:
            return false;
    }
}
}

int spot_node_t::set_pub_option (int option_, const void *optval_, size_t optvallen_)
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return -1;
    const int rc = _handle_state.handle_defaults.set_pub_option (option_, optval_, optvallen_);
    if (rc != 0)
        return rc;

    if (option_ == ZLINK_SPOT_PUB_OPT_AUTO_HWM_MSG_UNIT_BYTES)
        refresh_runtime_auto_hwm_msg_unit (_runtime, optval_, optvallen_);
    return 0;
}

int spot_node_t::set_sub_option (int option_, const void *optval_, size_t optvallen_)
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return -1;
    const int rc = _handle_state.handle_defaults.set_sub_option (option_, optval_, optvallen_);
    if (rc != 0)
        return rc;

    if (option_ == ZLINK_SPOT_SUB_OPT_AUTO_HWM_MSG_UNIT_BYTES)
        refresh_runtime_auto_hwm_msg_unit (_runtime, optval_, optvallen_);
    return 0;
}

int spot_node_t::set_node_option (int option_, const void *optval_, size_t optvallen_)
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return -1;
    if (!_runtime || !optval_) {
        errno = EFAULT;
        return -1;
    }

    spot_node_runtime_tuning_t runtime_tuning = _runtime->runtime_tuning_snapshot ();
    if (!apply_runtime_tuning_option (&runtime_tuning, option_, optval_, optvallen_)) {
        errno = EINVAL;
        return -1;
    }
    const int router_hwm = spot_node_router_admission_hwm (runtime_tuning);
    const int pubsub_hwm = spot_node_pubsub_admission_hwm (runtime_tuning);
    _runtime->set_runtime_tuning (runtime_tuning);

    switch (option_) {
        case ZLINK_SPOT_NODE_OPT_ROUTER_HWM_PROFILE:
        case ZLINK_SPOT_NODE_OPT_ROUTER_HWM:
            refresh_runtime_router_admission_hwm (_runtime, router_hwm);
            return 0;
        case ZLINK_SPOT_NODE_OPT_PUBSUB_HWM_PROFILE:
        case ZLINK_SPOT_NODE_OPT_PUBSUB_HWM:
            refresh_runtime_pubsub_admission_hwm (_runtime, pubsub_hwm);
            return 0;
        case ZLINK_SPOT_NODE_OPT_DISPATCH_WORKERS_MIN:
        case ZLINK_SPOT_NODE_OPT_DISPATCH_WORKERS_MAX:
            return 0;
        default:
            errno = EINVAL;
            return -1;
    }
}

int spot_node_t::get_node_option (int option_, void *optval_, size_t *optvallen_) const
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return -1;
    if (!_runtime || !optval_ || !optvallen_ || *optvallen_ < sizeof (int)) {
        errno = EINVAL;
        return -1;
    }

    int value = 0;
    const spot_node_runtime_tuning_t runtime_tuning = _runtime->runtime_tuning_snapshot ();
    if (!read_runtime_tuning_option (_ctx, _runtime, runtime_tuning, option_, &value)) {
        errno = EINVAL;
        return -1;
    }

    memcpy (optval_, &value, sizeof (value));
    *optvallen_ = sizeof (value);
    return 0;
}
}
