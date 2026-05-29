/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/spot/common/spot_control_protocol.hpp"
#include "services/spot/data_plane/spot_data_plane.hpp"
#include "services/spot/data_plane/spot_data_plane_internal.hpp"
#include "services/spot/data_plane/spot_data_plane_loop.hpp"
#include "services/spot/node/spot_node.hpp"
#include "services/spot/node/spot_node_access.hpp"
#include "services/spot/runtime/spot_runtime.hpp"

namespace zlink
{
void spot_data_plane_t::thread_entry (void *arg_)
{
    run (static_cast<spot_node_t *> (arg_));
}

void spot_data_plane_t::task_entry (void *arg_)
{
    spot_node_t *node = static_cast<spot_node_t *> (arg_);
    spot_runtime_t *runtime = spot_node_access_t::runtime (node);
    if (!node || !runtime)
        return;
    (void) run_tick (node, runtime);
}

int spot_data_plane_t::run_tick (spot_node_t *node_, spot_runtime_t *runtime_)
{
    if (!node_ || !runtime_)
        return EINVAL;
    if (runtime_->stop.get () != 0)
        return 0;

    bool running = true;
    const int fatal_errno = spot_data_plane_loop_t::run_once (
      node_, runtime_, &runtime_->execution.data_plane_state,
      &runtime_->execution.data_plane_protocol_state,
      &runtime_->execution.next_bootstrap_ms,
      &runtime_->execution.last_bootstrap_peer_version, &running);

    if (!running)
        runtime_->stop.set (1);

    if (fatal_errno != 0 && runtime_->stop.get () == 0) {
        scoped_lock_t lock (spot_node_access_t::sync (node_));
        runtime_->mark_fault (fatal_errno);
    }

    return fatal_errno;
}

void spot_data_plane_t::run (spot_node_t *node_)
{
    if (!node_)
        return;
    spot_runtime_t *runtime = spot_node_access_t::runtime (node_);
    if (!runtime)
        return;

    const int fatal_errno = spot_data_plane_loop_t::run_until_shutdown (
      node_, runtime, &runtime->execution.data_plane_state,
      &runtime->execution.data_plane_protocol_state);

    if (fatal_errno != 0 && runtime->stop.get () == 0) {
        scoped_lock_t lock (spot_node_access_t::sync (node_));
        runtime->mark_fault (fatal_errno);
    }
}
}
