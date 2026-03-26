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
