/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "services/spot/spot_control_protocol.hpp"
#include "services/spot/spot_data_plane.hpp"
#include "services/spot/spot_data_plane_internal.hpp"
#include "services/spot/spot_data_plane_loop.hpp"
#include "services/spot/spot_node.hpp"
#include "services/spot/spot_runtime.hpp"

namespace zlink
{
void spot_data_plane_t::thread_entry (void *arg_)
{
    run (static_cast<spot_node_t *> (arg_));
}

void spot_data_plane_t::task_entry (void *arg_)
{
    spot_node_t *node = static_cast<spot_node_t *> (arg_);
    if (!node || !node->_runtime)
        return;
    (void) run_tick (node, node->_runtime);
}

int spot_data_plane_t::run_tick (spot_node_t *node_, spot_runtime_t *runtime_)
{
    if (!node_ || !runtime_)
        return EINVAL;
    if (runtime_->stop.get () != 0)
        return 0;

    bool running = true;
    const int fatal_errno = spot_data_plane_loop_t::run_once (
      node_, runtime_, &runtime_->data_plane_state,
      &runtime_->data_plane_protocol_state, &runtime_->next_bootstrap_ms,
      &runtime_->last_bootstrap_peer_version, &running);

    if (!running)
        runtime_->stop.set (1);

    if (fatal_errno != 0 && runtime_->stop.get () == 0) {
        scoped_lock_t lock (node_->_sync);
        runtime_->mark_fault (fatal_errno);
    }

    return fatal_errno;
}

void spot_data_plane_t::run (spot_node_t *node_)
{
    if (!node_)
        return;
    spot_runtime_t *runtime = node_->_runtime;
    if (!runtime)
        return;

    spot_data_plane_runtime_state_t runtime_state;
    if (initialize_runtime (node_, runtime, &runtime_state)
        != 0) {
        return;
    }

    spot_data_plane_protocol_state_t protocol_state;
    const int fatal_errno =
      spot_data_plane_loop_t::run_until_shutdown (node_, runtime,
                                                  &runtime_state,
                                                  &protocol_state);
    teardown_runtime (node_, runtime, &runtime_state, &protocol_state);

    if (fatal_errno != 0 && runtime->stop.get () == 0) {
        scoped_lock_t lock (node_->_sync);
        runtime->mark_fault (fatal_errno);
    }
}
}
