/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "services/spot/spot_mesh_pub_budget.hpp"

#include "services/spot/spot_auto_hwm_internal.hpp"
#include "services/spot/spot_data_plane_internal.hpp"
#include "services/spot/spot_node.hpp"
#include "services/spot/spot_runtime.hpp"
#include "sockets/socket_base.hpp"
#include "core/auto_hwm_policy.hpp"

namespace zlink
{
namespace
{
int default_mesh_pub_sndhwm (size_t managed_connections_,
                             size_t active_connections_)
{
    auto_hwm_context_plan_t context_plan;
    auto_hwm_context_plan_from_budget_mb (
      ZLINK_CTX_AUTO_HWM_ENABLE_DFLT != 0,
      ZLINK_CTX_AUTO_HWM_TOTAL_MEMORY_BUDGET_MB_DFLT, &context_plan);
    auto_hwm_socket_plan_t socket_plan;
    auto_hwm_socket_plan_for_role (context_plan, auto_hwm_role_fanout,
                                   ZLINK_CORE_SOCKET_PUB, managed_connections_,
                                   active_connections_, &socket_plan, 0, -1,
                                   -1, false, false, auto_hwm_scope_none, 1,
                                   true,
                                   ZLINK_CTX_AUTO_HWM_SPOT_BOOTSTRAP_DFLT);
    return socket_plan.sndhwm;
}

int current_socket_sndhwm (socket_base_t *socket_)
{
    if (!socket_)
        return default_mesh_pub_sndhwm (1, 1);

    int value = default_mesh_pub_sndhwm (1, 1);
    size_t value_size = sizeof (value);
    if (socket_->getsockopt (ZLINK_INTERNAL_OPT_SNDHWM, &value, &value_size) == 0
        && value_size == sizeof (value)) {
        return value;
    }
    return default_mesh_pub_sndhwm (1, 1);
}
}

int spot_mesh_pub_budget_t::resolve_default (const std::string &endpoint_,
                                             unsigned int ready_peers_)
{
    (void) endpoint_;
    (void) ready_peers_;
    return default_mesh_pub_sndhwm (0, 0);
}

bool spot_mesh_pub_budget_t::should_refresh (
  const std::string &endpoint_,
  unsigned int previous_ready_peers_,
  unsigned int next_ready_peers_)
{
    const int previous_budget =
      resolve_default (endpoint_, previous_ready_peers_);
    const int next_budget = resolve_default (endpoint_, next_ready_peers_);
    return previous_budget != next_budget;
}

void spot_mesh_pub_budget_t::reset_runtime_state (spot_runtime_t *runtime_)
{
    if (!runtime_)
        return;

    zlink::reset_mesh_pub_budget_state (&runtime_->execution.mesh_peer_state);
}

bool spot_mesh_pub_budget_t::publish_ready_hint (spot_runtime_t *runtime_,
                                                 uint32_t ready_count_)
{
    if (!runtime_)
        return false;

    spot_mesh_peer_state_t *state = &runtime_->execution.mesh_peer_state;
    const uint32_t previous =
      state->mesh_pub_ready_peer_count.load (std::memory_order_acquire);
    if (previous == ready_count_)
        return false;

    state->mesh_pub_ready_peer_count.store (ready_count_,
                                            std::memory_order_release);
    if (should_refresh (runtime_->bound_endpoint, previous, ready_count_))
        state->budget_version.fetch_add (1, std::memory_order_acq_rel);
    return true;
}

int spot_mesh_pub_budget_t::resolve_runtime_default (
  const spot_runtime_t *runtime_)
{
    if (!runtime_)
        return resolve_default (std::string (), 0);

    if (runtime_->owner) {
        const spot_node_t::pub_defaults_t defaults =
          runtime_->owner->load_pub_defaults ();
        if (defaults.sndhwm.enabled && defaults.sndhwm.size > 0) {
            int value = resolve_default (std::string (), 0);
            memcpy (&value, &defaults.sndhwm.value,
                    std::min (defaults.sndhwm.size, sizeof (value)));
            return value > 0 ? value : 0;
        }
    }

    size_t local_pub_count = 0;
    size_t local_sub_count = 0;
    size_t connected_peer_count = 0;
    size_t active_peer_count = 0;
    runtime_->snapshot_auto_hwm_inputs (&local_pub_count, &local_sub_count,
                                        &connected_peer_count,
                                        &active_peer_count);
    return spot_internal_auto_hwm_default_hwm (
      runtime_->ctx (), auto_hwm_role_fanout, ZLINK_CORE_SOCKET_PUB, false, 0,
      connected_peer_count, active_peer_count);
}

int spot_mesh_pub_budget_t::resolve_initial_bind_sndhwm (
  const spot_runtime_t *runtime_,
  const std::string &endpoint_)
{
    if (runtime_ && runtime_->mesh_pub)
        return current_socket_sndhwm (runtime_->mesh_pub);

    unsigned int ready_peers = 0;
    if (runtime_)
        ready_peers =
          mesh_pub_ready_peer_count (&runtime_->execution.mesh_peer_state);

    return spot_data_plane_forwarder_t::resolve_internal_hwm_override (
      "ZLINK_SPOT_INTERNAL_MESH_PUB_SNDHWM",
      resolve_default (endpoint_, ready_peers));
}

void spot_mesh_pub_budget_t::refresh_live_socket (
  spot_runtime_t *runtime_,
  socket_base_t *mesh_pub_,
  int *current_hwm_,
  uint64_t *last_budget_version_,
  std::string *last_bound_endpoint_)
{
    if (!runtime_ || !mesh_pub_ || !current_hwm_ || !last_budget_version_
        || !last_bound_endpoint_) {
        return;
    }

    const uint64_t budget_version =
      mesh_pub_budget_version (&runtime_->execution.mesh_peer_state);
    const std::string bound_endpoint = runtime_->bound_endpoint;
    if (budget_version == *last_budget_version_
        && bound_endpoint == *last_bound_endpoint_) {
        return;
    }

    *last_budget_version_ = budget_version;
    *last_bound_endpoint_ = bound_endpoint;

    int desired = spot_data_plane_forwarder_t::resolve_internal_hwm_override (
      "ZLINK_SPOT_INTERNAL_MESH_PUB_SNDHWM",
      resolve_runtime_default (runtime_));
    if (desired == *current_hwm_)
        return;

    if (mesh_pub_->setsockopt (ZLINK_INTERNAL_OPT_SNDHWM, &desired,
                               sizeof (desired))
        == 0) {
        *current_hwm_ = desired;
    }
}
}
