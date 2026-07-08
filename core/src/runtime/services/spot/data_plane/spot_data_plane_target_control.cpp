/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/spot/data_plane/spot_data_plane_forwarding_internal.hpp"
#include "services/spot/data_plane/spot_data_plane_pending_internal.hpp"
#include "services/spot/data_plane/spot_data_plane_queue_admission.hpp"

#include "core/socket_poller.hpp"
#include "services/spot/node/spot_node.hpp"
#include "services/spot/runtime/spot_runtime.hpp"
#include "sockets/common/socket_base.hpp"

#include <algorithm>
#include <unordered_map>

namespace zlink
{
void spot_data_plane_forwarder_t::sync_local_fanout_targets (
  spot_runtime_t *runtime_, spot_data_plane_runtime_state_t *state_)
{
    if (!runtime_ || !state_)
        return;

    std::unordered_map<uint64_t, socket_base_t *> snapshot;
    std::deque<socket_base_t *> retired_relays;
    {
        scoped_lock_t lock (runtime_->attachment_state.sync);
        for (std::map<uint64_t, spot_attachment_t>::const_iterator it =
               runtime_->attachment_state.items.begin ();
             it != runtime_->attachment_state.items.end (); ++it) {
            if (it->second.kind == spot_attachment_sub && it->second.relay_socket)
                snapshot[it->first] = it->second.relay_socket;
        }
        retired_relays.swap (runtime_->attachment_state.retired_relay_sockets);
    }

    for (spot_data_plane_pending_state_t::local_fanout_state_t::target_map_t::iterator it =
           state_->pending.local_fanout.targets.begin ();
         it != state_->pending.local_fanout.targets.end ();) {
        const std::unordered_map<uint64_t, socket_base_t *>::const_iterator snap_it =
          snapshot.find (it->first);
        if (snap_it == snapshot.end () || snap_it->second == NULL) {
            const uint64_t attachment_id = it->first;
            ++it;
            spot_data_plane_pending_t::drop_local_target_state (state_, attachment_id);
            continue;
        }
        if (it->second.relay_socket != snap_it->second) {
            if (it->second.relay_socket)
                state_->pending.local_fanout.target_by_socket.erase (it->second.relay_socket);
            it->second.relay_socket = snap_it->second;
            if (it->second.relay_socket)
                state_->pending.local_fanout.target_by_socket[it->second.relay_socket] = it->first;
        }
        ++it;
    }

    for (std::unordered_map<uint64_t, socket_base_t *>::const_iterator it = snapshot.begin ();
         it != snapshot.end (); ++it) {
        if (state_->pending.local_fanout.targets.find (it->first) != state_->pending.local_fanout.targets.end ())
            continue;
        spot_data_plane_pending_state_t::local_target_state_t target;
        target.attachment_id = it->first;
        target.relay_socket = it->second;
        state_->pending.local_fanout.targets[it->first] = target;
        state_->pending.local_fanout.target_by_socket[it->second] = it->first;
        if (state_->poller && it->second)
            (void) state_->poller->add (it->second, NULL, 0);
        if (it->second)
            it->second->set_all_pipes_nodelay ();
    }

    for (std::deque<socket_base_t *>::iterator it = retired_relays.begin ();
         it != retired_relays.end (); ++it) {
        if (!*it)
            continue;
        (*it)->set_all_pipes_nodelay ();
        if (runtime_->owner)
            runtime_->owner->untrack_owned_socket (*it);
        (*it)->stop ();
        (*it)->close ();
    }
}

void spot_data_plane_forwarder_t::sync_remote_mesh_targets (
  spot_runtime_t *runtime_,
  spot_data_plane_runtime_state_t *state_,
  const spot_data_plane_protocol_state_t *protocol_state_)
{
    LIBZLINK_UNUSED (protocol_state_);

    if (!runtime_ || !state_)
        return;

    while (!state_->pending.remote_mesh.targets.empty ()) {
        const std::string endpoint = state_->pending.remote_mesh.targets.begin ()->first;
        spot_data_plane_pending_t::drop_remote_target_state (runtime_, state_, endpoint);
    }
}

void spot_data_plane_forwarder_t::drop_remote_mesh_target (spot_runtime_t *runtime_,
                                                           spot_data_plane_runtime_state_t *state_,
                                                           const std::string &endpoint_)
{
    spot_data_plane_pending_t::drop_remote_target_state (runtime_, state_, endpoint_);
}

void spot_data_plane_forwarder_t::update_pending_queue_limits (
  spot_runtime_t *runtime_, spot_data_plane_runtime_state_t *state_)
{
    if (!runtime_ || !state_)
        return;

    const int fanout_hwm = spot_data_plane_pending_t::resolve_fanout_hwm (runtime_);
    const size_t pending_limit = static_cast<size_t> (
      std::max (fanout_hwm / 2,
                static_cast<int> (spot_data_plane_default_queue_message_unit_bytes)));
    state_->pending.local_fanout.pending_hard_limit = pending_limit;
    state_->pending.remote_mesh.pending_hard_limit = pending_limit;
    state_->pending.local_fanout.pending_pause_threshold = pending_limit;
    state_->pending.local_fanout.pending_resume_threshold =
      std::max (pending_limit / 2, spot_data_plane_default_queue_message_unit_bytes / 2);
    state_->pending.remote_mesh.pending_pause_threshold = pending_limit;
    state_->pending.remote_mesh.pending_resume_threshold =
      std::max (pending_limit / 2, spot_data_plane_default_queue_message_unit_bytes / 2);
}
}
