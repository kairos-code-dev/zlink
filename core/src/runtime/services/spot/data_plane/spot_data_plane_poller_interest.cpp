/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/spot/data_plane/spot_data_plane_internal.hpp"

#include "core/socket_poller.hpp"
#include "sockets/common/socket_base.hpp"

namespace zlink
{
namespace
{
void refresh_target_pollout_interest (spot_data_plane_runtime_state_t *state_,
                                      socket_base_t *socket_,
                                      const std::deque<uint64_t> &pending_message_ids_,
                                      bool *pollout_armed_)
{
    if (!state_ || !state_->poller || !socket_ || !pollout_armed_)
        return;
    const bool need_pollout = !pending_message_ids_.empty ();
    if (need_pollout == *pollout_armed_)
        return;
    (void) state_->poller->modify (socket_, need_pollout ? ZLINK_POLLOUT : 0);
    *pollout_armed_ = need_pollout;
}
}

void spot_data_plane_poller_interest_t::refresh_fixed (spot_data_plane_runtime_state_t *state_)
{
    if (!state_ || !state_->poller)
        return;

    const bool pause_mesh =
      state_->remote_mesh.pending_bytes >= state_->remote_mesh.pending_pause_threshold
      || !state_->staged.mesh_messages.empty () || !state_->staged.ingress_messages.empty ();
    const bool resume_mesh =
      state_->remote_mesh.pending_bytes <= state_->remote_mesh.pending_resume_threshold;
    if (!state_->interest.mesh_xsub_pollin_paused && pause_mesh)
        state_->interest.mesh_xsub_pollin_paused = true;
    else if (state_->interest.mesh_xsub_pollin_paused && resume_mesh)
        state_->interest.mesh_xsub_pollin_paused = false;

    const short mesh_xsub_events = state_->interest.mesh_xsub_pollin_paused ? 0 : ZLINK_POLLIN;
    if (state_->mesh_xsub && state_->interest.mesh_xsub_pollin_armed != (mesh_xsub_events != 0)) {
        (void) state_->poller->modify (state_->mesh_xsub, mesh_xsub_events);
        state_->interest.mesh_xsub_pollin_armed = mesh_xsub_events != 0;
    }

    const bool mesh_pub_need_pollout = !state_->remote_mesh.broadcast_pending_message_ids.empty ();
    if (state_->mesh_pub && mesh_pub_need_pollout != state_->remote_mesh.pollout_armed) {
        (void) state_->poller->modify (state_->mesh_pub, mesh_pub_need_pollout ? ZLINK_POLLOUT : 0);
        state_->remote_mesh.pollout_armed = mesh_pub_need_pollout;
    }
}

void spot_data_plane_poller_interest_t::refresh_local_target (
  spot_data_plane_runtime_state_t *state_,
  spot_data_plane_runtime_state_t::local_target_state_t *target_)
{
    if (!target_)
        return;
    refresh_target_pollout_interest (state_, target_->relay_socket, target_->pending_message_ids,
                                     &target_->pollout_armed);
}

void spot_data_plane_poller_interest_t::refresh_remote_target (
  spot_data_plane_runtime_state_t *state_,
  spot_data_plane_runtime_state_t::remote_target_state_t *target_)
{
    if (!target_)
        return;
    refresh_target_pollout_interest (state_, target_->sender_socket, target_->pending_message_ids,
                                     &target_->pollout_armed);
}

void spot_data_plane_poller_interest_t::refresh_all (spot_data_plane_runtime_state_t *state_)
{
    if (!state_ || !state_->poller)
        return;

    refresh_fixed (state_);

    for (spot_data_plane_runtime_state_t::local_fanout_state_t::target_map_t::iterator it =
           state_->local_fanout.targets.begin ();
         it != state_->local_fanout.targets.end (); ++it) {
        refresh_local_target (state_, &it->second);
    }

    for (spot_data_plane_runtime_state_t::remote_mesh_state_t::target_map_t::iterator it =
           state_->remote_mesh.targets.begin ();
         it != state_->remote_mesh.targets.end (); ++it) {
        refresh_remote_target (state_, &it->second);
    }
}
}
