/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "services/spot/spot_mesh_pub_hwm.hpp"

#include "services/spot/spot_auto_hwm_internal.hpp"
#include "services/spot/spot_data_plane_internal.hpp"
#include "services/spot/spot_node.hpp"
#include "services/spot/spot_runtime.hpp"
#include "sockets/common/socket_base.hpp"
#include "core/auto_hwm_policy.hpp"

namespace zlink
{
namespace
{
int default_mesh_pub_sndhwm (size_t managed_connections_,
                             size_t active_connections_)
{
    LIBZLINK_UNUSED (managed_connections_);
    LIBZLINK_UNUSED (active_connections_);
    return 0;
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

int spot_mesh_pub_hwm_t::resolve_default (const std::string &endpoint_,
                                             unsigned int ready_peers_)
{
    (void) endpoint_;
    (void) ready_peers_;
    return default_mesh_pub_sndhwm (0, 0);
}

bool spot_mesh_pub_hwm_t::should_refresh (
  const std::string &endpoint_,
  unsigned int previous_ready_peers_,
  unsigned int next_ready_peers_)
{
    const int previous_hwm =
      resolve_default (endpoint_, previous_ready_peers_);
    const int next_hwm = resolve_default (endpoint_, next_ready_peers_);
    return previous_hwm != next_hwm;
}

void spot_mesh_pub_hwm_t::reset_runtime_state (spot_runtime_t *runtime_)
{
    if (!runtime_)
        return;

    zlink::reset_mesh_pub_hwm_state (&runtime_->execution.mesh_peer_state);
}

bool spot_mesh_pub_hwm_t::publish_ready_hint (spot_runtime_t *runtime_,
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
        state->hwm_version.fetch_add (1, std::memory_order_acq_rel);
    return true;
}

int spot_mesh_pub_hwm_t::resolve_runtime_default (
  const spot_runtime_t *runtime_)
{
    if (!runtime_)
        return 0;
    const spot_node_hwm_config_t hwm = runtime_->hwm_config_snapshot ();
    return spot_node_pubsub_hwm_overridden (hwm)
             ? spot_node_pubsub_admission_hwm (hwm)
             : 0;
}

int spot_mesh_pub_hwm_t::resolve_initial_bind_sndhwm (
  const spot_runtime_t *runtime_,
  const std::string &endpoint_)
{
    if (runtime_ && runtime_->mesh_pub)
        return current_socket_sndhwm (runtime_->mesh_pub);

    unsigned int ready_peers = 0;
    if (runtime_)
        ready_peers =
          mesh_pub_ready_peer_count (&runtime_->execution.mesh_peer_state);

    LIBZLINK_UNUSED (endpoint_);
    LIBZLINK_UNUSED (ready_peers);
    return 0;
}

void spot_mesh_pub_hwm_t::refresh_live_socket (
  spot_runtime_t *runtime_,
  socket_base_t *mesh_pub_,
  int *current_hwm_,
  uint64_t *last_hwm_version_,
  std::string *last_bound_endpoint_)
{
    if (!runtime_ || !mesh_pub_ || !current_hwm_ || !last_hwm_version_
        || !last_bound_endpoint_) {
        return;
    }

    const uint64_t hwm_version =
      mesh_pub_hwm_version (&runtime_->execution.mesh_peer_state);
    const std::string bound_endpoint = runtime_->bound_endpoint;
    if (hwm_version == *last_hwm_version_
        && bound_endpoint == *last_bound_endpoint_) {
        return;
    }

    *last_hwm_version_ = hwm_version;
    *last_bound_endpoint_ = bound_endpoint;

    const int desired = resolve_runtime_default (runtime_);
    if (desired <= 0)
        return;
    if (desired == *current_hwm_)
        return;

    if (mesh_pub_->setsockopt (ZLINK_INTERNAL_OPT_SNDHWM, &desired,
                               sizeof (desired))
        == 0) {
        *current_hwm_ = desired;
    }
}
}
