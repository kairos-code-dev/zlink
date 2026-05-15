/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "services/spot/spot_runtime_internal.hpp"

namespace zlink
{
bool spot_runtime_t::try_set_control_task_id (uint64_t task_id_)
{
    if (task_id_ == 0)
        return false;

    scoped_lock_t lock (execution_sync);
    if (execution.control_state.task_id != 0)
        return false;
    execution.control_state.task_id = task_id_;
    return true;
}

uint64_t spot_runtime_t::control_task_id () const
{
    scoped_lock_t lock (execution_sync);
    return execution.control_state.task_id;
}

uint64_t spot_runtime_t::clear_control_task_id ()
{
    scoped_lock_t lock (execution_sync);
    const uint64_t task_id = execution.control_state.task_id;
    execution.control_state.task_id = 0;
    return task_id;
}

bool spot_runtime_t::note_connected_peer_version (
  uint64_t connected_peer_version_)
{
    scoped_lock_t lock (execution_sync);
    if (execution.control_state.connected_peer_version_seen
        == connected_peer_version_)
        return false;
    execution.control_state.connected_peer_version_seen = connected_peer_version_;
    return true;
}

uint64_t spot_runtime_t::connected_peer_version_seen () const
{
    scoped_lock_t lock (execution_sync);
    return execution.control_state.connected_peer_version_seen;
}
}
