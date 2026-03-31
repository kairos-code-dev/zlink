/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "services/spot/spot_mesh_pub_budget.hpp"

#include "services/spot/spot_data_plane_internal.hpp"
#include "services/spot/spot_runtime.hpp"
#include "sockets/socket_base.hpp"

namespace zlink
{
namespace
{
static const int default_mesh_pub_sndhwm = 1000;
}

int spot_mesh_pub_budget_t::resolve_default (const std::string &endpoint_,
                                             unsigned int ready_peers_)
{
    (void) endpoint_;
    (void) ready_peers_;
    return default_mesh_pub_sndhwm;
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

    zlink::reset_mesh_pub_budget_state (&runtime_->mesh_peer_state);
}

bool spot_mesh_pub_budget_t::publish_ready_hint (spot_runtime_t *runtime_,
                                                 uint32_t ready_count_)
{
    if (!runtime_)
        return false;

    spot_mesh_peer_state_t *state = &runtime_->mesh_peer_state;
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

    const uint32_t ready_peers =
      mesh_pub_ready_peer_count (&runtime_->mesh_peer_state);
    return resolve_default (runtime_->bound_endpoint, ready_peers);
}

int spot_mesh_pub_budget_t::resolve_initial_bind_sndhwm (
  const spot_runtime_t *runtime_,
  const std::string &endpoint_)
{
    unsigned int ready_peers = 0;
    if (runtime_)
        ready_peers = mesh_pub_ready_peer_count (&runtime_->mesh_peer_state);

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
      mesh_pub_budget_version (&runtime_->mesh_peer_state);
    const std::string bound_endpoint = runtime_->bound_endpoint;
    if (budget_version == *last_budget_version_
        && bound_endpoint == *last_bound_endpoint_) {
        return;
    }

    *last_budget_version_ = budget_version;
    *last_bound_endpoint_ = bound_endpoint;

    const int desired = spot_data_plane_forwarder_t::resolve_internal_hwm_override (
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
