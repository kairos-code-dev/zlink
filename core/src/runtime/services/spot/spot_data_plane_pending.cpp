/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "services/spot/spot_data_plane_internal.hpp"
#include "services/spot/spot_message_parts_internal.hpp"

#include "core/auto_hwm_policy.hpp"
#include "core/ctx.hpp"
#include "core/socket_poller.hpp"
#include "services/spot/spot_node_access.hpp"
#include "services/spot/spot_node.hpp"
#include "services/spot/spot_runtime.hpp"
#include "sockets/common/socket_base.hpp"

#include <errno.h>
#include <limits.h>
#include <string.h>

namespace zlink
{
namespace
{
static const size_t pending_queue_bytes_floor = 64 * 1024;
}

bool spot_data_plane_pending_t::queue_has_room (size_t current_bytes_,
                                                size_t hard_limit_bytes_,
                                                size_t message_bytes_)
{
    if (hard_limit_bytes_ == 0)
        return true;
    if (current_bytes_ == 0)
        return true;
    if (message_bytes_ > hard_limit_bytes_)
        return false;
    return current_bytes_ <= hard_limit_bytes_ - message_bytes_;
}

int spot_data_plane_pending_t::copy_msg_parts_to_owned (
  const spot_owned_msg_parts_t &src_, spot_owned_msg_parts_t *dst_)
{
    if (!dst_) {
        errno = EINVAL;
        return -1;
    }

    spot_clear_msg_parts (dst_);
    for (spot_owned_msg_parts_t::const_iterator it = src_.begin ();
         it != src_.end (); ++it) {
        zlink_msg_t frame;
        memset (&frame, 0, sizeof (frame));
        spot_init_msg_frame (&frame);
        if (zlink_msg_copy (&frame, const_cast<zlink_msg_t *> (&(*it))) != 0) {
            const int err = errno;
            spot_close_msg_frame (&frame);
            spot_clear_msg_parts (dst_);
            errno = err;
            return -1;
        }
        dst_->push_back (frame);
    }

    return 0;
}

int spot_data_plane_pending_t::resolve_fanout_hwm (
  spot_runtime_t *runtime_)
{
    ctx_t *ctx = runtime_ ? runtime_->ctx () : NULL;
    auto_hwm_context_plan_t context_plan;
    auto_hwm_context_plan_make (
      ctx && ctx->get (ZLINK_CTX_OPT_AUTO_HWM_ENABLE) != 0,
      ctx ? ctx->auto_hwm_profile () : ZLINK_CTX_AUTO_HWM_PROFILE_DFLT,
      &context_plan);
    auto_hwm_socket_plan_t socket_plan;
    auto_hwm_socket_plan_for_role (context_plan, auto_hwm_role_spot_data,
                                   ZLINK_CORE_SOCKET_PUB, 0, 0, &socket_plan);
    return socket_plan.sndhwm;
}

void spot_data_plane_pending_t::release_local_pending_ref (
  spot_data_plane_runtime_state_t *state_, uint64_t message_id_)
{
    if (!state_ || message_id_ == 0)
        return;

    spot_data_plane_runtime_state_t::local_fanout_state_t::pending_message_map_t::
      iterator it = state_->local_fanout.pending_messages.find (message_id_);
    if (it == state_->local_fanout.pending_messages.end ())
        return;

    if (it->second.remaining_targets > 0)
        --it->second.remaining_targets;
    if (it->second.remaining_targets != 0)
        return;

    if (state_->local_fanout.pending_bytes >= it->second.encoded_bytes)
        state_->local_fanout.pending_bytes -= it->second.encoded_bytes;
    else
        state_->local_fanout.pending_bytes = 0;
    spot_clear_msg_parts (&it->second.parts);
    state_->local_fanout.pending_messages.erase (it);
}

void spot_data_plane_pending_t::drop_local_target_state (
  spot_data_plane_runtime_state_t *state_, uint64_t attachment_id_)
{
    if (!state_ || attachment_id_ == 0)
        return;

    spot_data_plane_runtime_state_t::local_fanout_state_t::target_map_t::iterator
      it = state_->local_fanout.targets.find (attachment_id_);
    if (it == state_->local_fanout.targets.end ())
        return;

    if (state_->poller && it->second.relay_socket)
        (void) state_->poller->remove (it->second.relay_socket);
    if (it->second.relay_socket)
        state_->local_fanout.target_by_socket.erase (it->second.relay_socket);

    while (!it->second.pending_message_ids.empty ()) {
        release_local_pending_ref (state_, it->second.pending_message_ids.front ());
        it->second.pending_message_ids.pop_front ();
    }

    state_->local_fanout.targets.erase (it);
}

void spot_data_plane_pending_t::release_mesh_pending_ref (
  spot_data_plane_runtime_state_t *state_, uint64_t message_id_)
{
    if (!state_ || message_id_ == 0)
        return;

    spot_data_plane_runtime_state_t::remote_mesh_state_t::pending_message_map_t::
      iterator it = state_->remote_mesh.pending_messages.find (message_id_);
    if (it == state_->remote_mesh.pending_messages.end ())
        return;

    if (it->second.remaining_targets > 0)
        --it->second.remaining_targets;
    if (it->second.remaining_targets != 0)
        return;

    if (state_->remote_mesh.pending_bytes >= it->second.encoded_bytes)
        state_->remote_mesh.pending_bytes -= it->second.encoded_bytes;
    else
        state_->remote_mesh.pending_bytes = 0;
    spot_clear_msg_parts (&it->second.parts);
    state_->remote_mesh.pending_messages.erase (it);
}

void spot_data_plane_pending_t::close_remote_target_socket (
  spot_runtime_t *runtime_, const std::string &route_endpoint_,
  socket_base_t *&socket_)
{
    if (!socket_)
        return;

    if (!route_endpoint_.empty ())
        (void) socket_->term_endpoint (route_endpoint_.c_str ());
    socket_->set_all_pipes_nodelay ();
    if (runtime_ && runtime_->owner) {
        (void) spot_node_access_t::close_owned_socket_and_wait (
          runtime_->owner, socket_, 1000);
        return;
    }
    socket_->stop ();
    socket_->close ();
    socket_ = NULL;
}

void spot_data_plane_pending_t::drop_remote_target_state (
  spot_runtime_t *runtime_, spot_data_plane_runtime_state_t *state_,
  const std::string &endpoint_)
{
    if (!state_ || endpoint_.empty ())
        return;

    spot_data_plane_runtime_state_t::remote_mesh_state_t::target_map_t::iterator
      it = state_->remote_mesh.targets.find (endpoint_);
    if (it == state_->remote_mesh.targets.end ())
        return;

    if (state_->poller && it->second.sender_socket)
        (void) state_->poller->remove (it->second.sender_socket);
    if (it->second.sender_socket)
        state_->remote_mesh.target_by_socket.erase (it->second.sender_socket);

    while (!it->second.pending_message_ids.empty ()) {
        release_mesh_pending_ref (state_, it->second.pending_message_ids.front ());
        it->second.pending_message_ids.pop_front ();
    }

    close_remote_target_socket (runtime_, it->second.route_endpoint,
                                it->second.sender_socket);
    state_->remote_mesh.targets.erase (it);
}

bool spot_data_plane_pending_t::enqueue_local_target_message (
  spot_data_plane_runtime_state_t *state_,
  spot_data_plane_runtime_state_t::local_target_state_t *target_,
  const std::string &topic_,
  const spot_owned_msg_parts_t &parts_,
  uint64_t *message_id_inout_)
{
    if (!state_ || !target_ || topic_.empty ()) {
        errno = EINVAL;
        return false;
    }

    uint64_t message_id = message_id_inout_ ? *message_id_inout_ : 0;
    if (message_id == 0) {
        message_id = ++state_->next_pending_message_id;
        if (message_id == 0)
            message_id = ++state_->next_pending_message_id;

        spot_data_plane_runtime_state_t::publish_pending_entry_t entry;
        entry.message_id = message_id;
        entry.topic = topic_;
        entry.encoded_bytes = spot_msg_parts_encoded_bytes (parts_);
        if (!queue_has_room (state_->local_fanout.pending_bytes,
                             state_->local_fanout.pending_hard_limit,
                             entry.encoded_bytes)) {
            errno = EAGAIN;
            return false;
        }
        if (copy_msg_parts_to_owned (parts_, &entry.parts) != 0)
            return false;

        state_->local_fanout.pending_bytes += entry.encoded_bytes;
        state_->local_fanout.pending_messages.emplace (message_id,
                                                       std::move (entry));
        if (message_id_inout_)
            *message_id_inout_ = message_id;
    }

    target_->pending_message_ids.push_back (message_id);
    spot_data_plane_runtime_state_t::local_fanout_state_t::pending_message_map_t::
      iterator it = state_->local_fanout.pending_messages.find (message_id);
    if (it != state_->local_fanout.pending_messages.end ())
        ++it->second.remaining_targets;
    return true;
}

bool spot_data_plane_pending_t::enqueue_mesh_pending (
  spot_data_plane_runtime_state_t *state_,
  spot_data_plane_runtime_state_t::remote_target_state_t *target_,
  const std::string &topic_,
  const spot_owned_msg_parts_t &parts_,
  uint64_t *message_id_inout_)
{
    if (!state_ || !target_ || topic_.empty ()) {
        errno = EINVAL;
        return false;
    }

    uint64_t message_id = message_id_inout_ ? *message_id_inout_ : 0;
    if (message_id == 0) {
        message_id = ++state_->next_pending_message_id;
        if (message_id == 0)
            message_id = ++state_->next_pending_message_id;

        spot_data_plane_runtime_state_t::publish_pending_entry_t entry;
        entry.message_id = message_id;
        entry.topic = topic_;
        entry.encoded_bytes = spot_msg_parts_encoded_bytes (parts_);
        if (!queue_has_room (state_->remote_mesh.pending_bytes,
                             state_->remote_mesh.pending_hard_limit,
                             entry.encoded_bytes)) {
            errno = EAGAIN;
            return false;
        }
        if (copy_msg_parts_to_owned (parts_, &entry.parts) != 0)
            return false;

        state_->remote_mesh.pending_bytes += entry.encoded_bytes;
        state_->remote_mesh.pending_messages.emplace (message_id,
                                                      std::move (entry));
        if (message_id_inout_)
            *message_id_inout_ = message_id;
    }

    target_->pending_message_ids.push_back (message_id);
    spot_data_plane_runtime_state_t::remote_mesh_state_t::pending_message_map_t::
      iterator it = state_->remote_mesh.pending_messages.find (message_id);
    if (it != state_->remote_mesh.pending_messages.end ())
        ++it->second.remaining_targets;
    return true;
}

bool spot_data_plane_pending_t::enqueue_mesh_broadcast_pending (
  spot_data_plane_runtime_state_t *state_,
  const std::string &topic_,
  const spot_owned_msg_parts_t &parts_,
  uint64_t *message_id_inout_)
{
    if (!state_ || topic_.empty ()) {
        errno = EINVAL;
        return false;
    }

    uint64_t message_id = message_id_inout_ ? *message_id_inout_ : 0;
    if (message_id == 0) {
        message_id = ++state_->next_pending_message_id;
        if (message_id == 0)
            message_id = ++state_->next_pending_message_id;

        spot_data_plane_runtime_state_t::publish_pending_entry_t entry;
        entry.message_id = message_id;
        entry.topic = topic_;
        entry.encoded_bytes = spot_msg_parts_encoded_bytes (parts_);
        if (!queue_has_room (state_->remote_mesh.pending_bytes,
                             state_->remote_mesh.pending_hard_limit,
                             entry.encoded_bytes)) {
            errno = EAGAIN;
            return false;
        }
        if (copy_msg_parts_to_owned (parts_, &entry.parts) != 0)
            return false;

        state_->remote_mesh.pending_bytes += entry.encoded_bytes;
        state_->remote_mesh.pending_messages.emplace (message_id,
                                                      std::move (entry));
        if (message_id_inout_)
            *message_id_inout_ = message_id;
    }

    state_->remote_mesh.broadcast_pending_message_ids.push_back (message_id);
    spot_data_plane_runtime_state_t::remote_mesh_state_t::pending_message_map_t::
      iterator it = state_->remote_mesh.pending_messages.find (message_id);
    if (it != state_->remote_mesh.pending_messages.end ())
        ++it->second.remaining_targets;
    return true;
}

bool spot_data_plane_pending_t::stage_publish_message (
  std::deque<spot_data_plane_runtime_state_t::staged_publish_entry_t> *queue_,
  const std::string &topic_,
  const spot_owned_msg_parts_t &parts_,
  bool need_local_,
  bool need_mesh_)
{
    if (!queue_ || topic_.empty ()) {
        errno = EINVAL;
        return false;
    }

    spot_data_plane_runtime_state_t::staged_publish_entry_t entry;
    entry.topic = topic_;
    entry.encoded_bytes = spot_msg_parts_encoded_bytes (parts_);
    entry.need_local = need_local_;
    entry.need_mesh = need_mesh_;
    if (copy_msg_parts_to_owned (parts_, &entry.parts) != 0)
        return false;
    queue_->push_back (entry);
    return true;
}
}
