/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/spot/data_plane/spot_mesh_pub_hwm.hpp"

#include "services/spot/common/spot_auto_hwm_internal.hpp"
#include "services/spot/data_plane/spot_data_plane_internal.hpp"
#include "services/spot/node/spot_node.hpp"
#include "services/spot/runtime/spot_runtime.hpp"
#include "sockets/common/socket_base.hpp"
#include "core/auto_hwm_policy.hpp"

namespace zlink
{
void spot_mesh_pub_hwm_t::reset_runtime_state (spot_runtime_t *runtime_)
{
    if (!runtime_)
        return;

    zlink::reset_mesh_pub_hwm_state (&runtime_->execution.mesh_peer_state);
}

bool spot_mesh_pub_hwm_t::publish_ready_hint (spot_runtime_t *runtime_, uint32_t ready_count_)
{
    if (!runtime_)
        return false;

    spot_mesh_peer_state_t *state = &runtime_->execution.mesh_peer_state;
    const uint32_t previous = state->mesh_pub_ready_peer_count.load (std::memory_order_acquire);
    if (previous == ready_count_)
        return false;

    state->mesh_pub_ready_peer_count.store (ready_count_, std::memory_order_release);
    state->hwm_version.fetch_add (1, std::memory_order_acq_rel);
    return true;
}

int spot_mesh_pub_hwm_t::resolve_runtime_default (const spot_runtime_t *runtime_)
{
    if (!runtime_)
        return 0;
    const spot_node_runtime_tuning_t hwm = runtime_->runtime_tuning_snapshot ();
    return spot_node_pubsub_hwm_overridden (hwm) ? spot_node_pubsub_admission_hwm (hwm) : 0;
}

int spot_mesh_pub_hwm_t::resolve_initial_bind_sndhwm (const spot_runtime_t *runtime_,
                                                      const std::string &endpoint_)
{
    if (runtime_ && runtime_->mesh_pub)
        return spot_auto_hwm_internal::current_socket_sndhwm (runtime_->mesh_pub);
    LIBZLINK_UNUSED (endpoint_);
    return 0;
}

void spot_mesh_pub_hwm_t::refresh_live_sockets (spot_runtime_t *runtime_,
                                                socket_base_t *mesh_pub_,
                                                socket_base_t *mesh_xsub_,
                                                socket_base_t *routed_router_,
                                                int *current_mesh_pub_hwm_,
                                                uint64_t *last_hwm_version_,
                                                std::string *last_bound_endpoint_)
{
    if (!runtime_ || !current_mesh_pub_hwm_ || !last_hwm_version_ || !last_bound_endpoint_) {
        return;
    }

    const uint64_t hwm_version = mesh_pub_hwm_version (&runtime_->execution.mesh_peer_state);
    const std::string bound_endpoint = runtime_->bound_endpoint;
    if (hwm_version == *last_hwm_version_ && bound_endpoint == *last_bound_endpoint_) {
        return;
    }

    *last_hwm_version_ = hwm_version;
    *last_bound_endpoint_ = bound_endpoint;

    size_t connected_peer_count = 0;
    size_t active_peer_count = 0;
    runtime_->snapshot_auto_hwm_inputs (NULL, NULL, &connected_peer_count, &active_peer_count);

    ctx_t *ctx = runtime_->ctx ();
    const spot_node_runtime_tuning_t hwm = runtime_->runtime_tuning_snapshot ();
    apply_spot_runtime_hwm_policy (ctx, hwm, 0, connected_peer_count, active_peer_count, 0,
                                   mesh_pub_, NULL, mesh_xsub_, routed_router_,
                                   current_mesh_pub_hwm_);
}
}
