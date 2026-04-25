/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "services/spot/spot_data_plane_internal.hpp"
#include "services/spot/spot_data_plane_message_io_internal.hpp"
#include "services/spot/spot_message_parts_internal.hpp"

#include "api/request_reply_protocol_internal.hpp"
#include "core/auto_hwm_policy.hpp"
#include "core/ctx.hpp"
#include "core/multipart_send_txn.hpp"
#include "services/spot/spot_auto_hwm_internal.hpp"
#include "services/spot/spot_control_protocol.hpp"
#include "services/spot/spot_node_access.hpp"
#include "services/spot/spot_node.hpp"
#include "services/spot/spot_runtime.hpp"
#include "sockets/socket_base.hpp"
#include "utils/err.hpp"

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

namespace zlink
{
namespace
{
bool spot_direct_route_debug_enabled_local ()
{
    return std::getenv ("ZLINK_DEBUG_SPOT_DIRECT_ROUTE") != NULL;
}

static const unsigned int ingress_forward_batch_limit = 2048;
static const size_t ingress_forward_batch_bytes_limit = 16 * 1024 * 1024;
static const size_t pending_queue_bytes_floor = 64 * 1024;

bool pending_queue_has_room_local (size_t current_bytes_,
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

int copy_msg_parts_to_owned_local (const spot_owned_msg_parts_t &src_,
                                   spot_owned_msg_parts_t *dst_)
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

int resolve_fanout_budget_bytes_local (ctx_t *ctx_)
{
    auto_hwm_context_plan_t context_plan;
    auto_hwm_context_plan_from_budget_mb (
      ctx_ && ctx_->get (ZLINK_CTX_OPT_AUTO_HWM_ENABLE) != 0,
      ctx_ ? ctx_->get (ZLINK_CTX_OPT_AUTO_HWM_TOTAL_MEMORY_BUDGET_MB)
           : ZLINK_CTX_AUTO_HWM_TOTAL_MEMORY_BUDGET_MB_DFLT,
      &context_plan);
    auto_hwm_socket_plan_t socket_plan;
    auto_hwm_socket_plan_for_role (context_plan, auto_hwm_role_fanout,
                                   ZLINK_CORE_SOCKET_PUB, 0, 0, &socket_plan);
    return socket_plan.group_budget_bytes > static_cast<uint64_t> (INT_MAX)
             ? INT_MAX
             : static_cast<int> (socket_plan.group_budget_bytes);
}

void release_local_pending_ref_local (spot_data_plane_runtime_state_t *state_,
                                      uint64_t message_id_)
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

void drop_local_target_state_local (spot_data_plane_runtime_state_t *state_,
                                    uint64_t attachment_id_)
{
    if (!state_ || attachment_id_ == 0)
        return;

    spot_data_plane_runtime_state_t::local_fanout_state_t::target_map_t::iterator
      it = state_->local_fanout.targets.find (attachment_id_);
    if (it == state_->local_fanout.targets.end ())
        return;

    if (state_->poller && it->second.relay_socket)
        (void) state_->poller->remove (it->second.relay_socket);

    while (!it->second.pending_message_ids.empty ()) {
        release_local_pending_ref_local (state_, it->second.pending_message_ids.front ());
        it->second.pending_message_ids.pop_front ();
    }

    state_->local_fanout.targets.erase (it);
}

void release_mesh_pending_ref_local (spot_data_plane_runtime_state_t *state_,
                                     uint64_t message_id_)
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

void close_remote_target_socket_local (spot_runtime_t *runtime_,
                                       const std::string &route_endpoint_,
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

void drop_remote_target_state_local (spot_runtime_t *runtime_,
                                     spot_data_plane_runtime_state_t *state_,
                                     const std::string &endpoint_)
{
    if (!state_ || endpoint_.empty ())
        return;

    spot_data_plane_runtime_state_t::remote_mesh_state_t::target_map_t::iterator
      it =
      state_->remote_mesh.targets.find (endpoint_);
    if (it == state_->remote_mesh.targets.end ())
        return;

    if (state_->poller && it->second.sender_socket)
        (void) state_->poller->remove (it->second.sender_socket);

    while (!it->second.pending_message_ids.empty ()) {
        release_mesh_pending_ref_local (state_,
                                        it->second.pending_message_ids.front ());
        it->second.pending_message_ids.pop_front ();
    }

    close_remote_target_socket_local (runtime_, it->second.route_endpoint,
                                      it->second.sender_socket);
    state_->remote_mesh.targets.erase (it);
}

bool enqueue_local_target_message_local (
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
        if (!pending_queue_has_room_local (state_->local_fanout.pending_bytes,
                                           state_->local_fanout.pending_hard_limit,
                                           entry.encoded_bytes)) {
            errno = EAGAIN;
            return false;
        }
        if (copy_msg_parts_to_owned_local (parts_, &entry.parts) != 0)
            return false;

        state_->local_fanout.pending_bytes += entry.encoded_bytes;
        state_->local_fanout.pending_messages[message_id] = entry;
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

bool enqueue_mesh_pending_local (spot_data_plane_runtime_state_t *state_,
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
        if (!pending_queue_has_room_local (state_->remote_mesh.pending_bytes,
                                           state_->remote_mesh.pending_hard_limit,
                                           entry.encoded_bytes)) {
            errno = EAGAIN;
            return false;
        }
        if (copy_msg_parts_to_owned_local (parts_, &entry.parts) != 0)
            return false;

        state_->remote_mesh.pending_bytes += entry.encoded_bytes;
        state_->remote_mesh.pending_messages[message_id] = entry;
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

bool enqueue_mesh_broadcast_pending_local (
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
        if (!pending_queue_has_room_local (state_->remote_mesh.pending_bytes,
                                           state_->remote_mesh.pending_hard_limit,
                                           entry.encoded_bytes)) {
            errno = EAGAIN;
            return false;
        }
        if (copy_msg_parts_to_owned_local (parts_, &entry.parts) != 0)
            return false;

        state_->remote_mesh.pending_bytes += entry.encoded_bytes;
        state_->remote_mesh.pending_messages[message_id] = entry;
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

int send_remote_mesh_message_local (socket_base_t *socket_,
                                    const std::string &topic_,
                                    const spot_owned_msg_parts_t &parts_)
{
    if (!socket_ || topic_.empty ()) {
        errno = EINVAL;
        return -1;
    }

    spot_data_plane_forwarder_t::pump_socket_commands (socket_);
    socket_->set_all_pipes_nodelay ();
    if (spot_direct_route_debug_enabled_local ()) {
        std::fprintf (stderr,
                      "[spot-direct] send remote socket=%d topic=%s parts=%zu\n",
                      socket_->socket_id (), topic_.c_str (), parts_.size ());
    }
    if (parts_.empty ()) {
        zlink_msg_t empty;
        zlink_msg_init (&empty);
        if (zlink_msg_init_size (&empty, 0) != 0)
            return -1;
        const int rc = logical_multipart_send_prefixed_frames (
          socket_, spot_control_protocol::peer_pub_route_topic,
          strlen (spot_control_protocol::peer_pub_route_topic), 0, topic_.data (),
          topic_.size (), 0, &empty, 1, ZLINK_DONTWAIT);
        zlink_msg_close (&empty);
        return rc;
    }

    return logical_multipart_send_prefixed_frames (
      socket_, spot_control_protocol::peer_pub_route_topic,
      strlen (spot_control_protocol::peer_pub_route_topic), 0, topic_.data (),
      topic_.size (), 0, const_cast<zlink_msg_t *> (&parts_[0]), parts_.size (),
      ZLINK_DONTWAIT);
}

socket_base_t *create_remote_mesh_sender_socket_local (spot_runtime_t *runtime_,
                                                       const std::string &route_endpoint_)
{
    if (!runtime_ || !runtime_->owner || route_endpoint_.empty ()) {
        errno = EINVAL;
        return NULL;
    }

    ctx_t *ctx = runtime_->ctx ();
    socket_base_t *socket = ctx ? ctx->create_socket (ZLINK_CORE_SOCKET_DEALER)
                                : NULL;
    if (!socket)
        return NULL;

    socket->set_auto_hwm_policy_enabled (false);
    size_t local_pub_count = 0;
    size_t local_sub_count = 0;
    size_t connected_peer_count = 0;
    size_t active_peer_count = 0;
    runtime_->snapshot_auto_hwm_inputs (&local_pub_count, &local_sub_count,
                                        &connected_peer_count,
                                        &active_peer_count);
    apply_spot_internal_auto_hwm (
      ctx, socket,
      spot_internal_auto_hwm_policy_t{auto_hwm_role_fanout,
                                      ZLINK_CORE_SOCKET_DEALER,
                                      connected_peer_count,
                                      active_peer_count,
                                      0, 0, true, true, true, true});

    std::string ca;
    std::string host;
    int trust_system = 0;
    spot_node_access_t::snapshot_tls_client_config (runtime_->owner, &ca, &host,
                                                    &trust_system);
    const int linger = 0;
    const int immediate = 1;
    socket->setsockopt (ZLINK_INTERNAL_OPT_LINGER, &linger, sizeof (linger));
    socket->setsockopt (ZLINK_INTERNAL_OPT_IMMEDIATE, &immediate,
                        sizeof (immediate));
    if (spot_node_access_t::apply_tls_client (runtime_->owner, socket, ca, host,
                                              trust_system)
          != 0
        || socket->connect (route_endpoint_.c_str ()) != 0) {
        const int saved_errno = errno != 0 ? errno : EIO;
        socket->close ();
        errno = saved_errno;
        return NULL;
    }

    spot_node_access_t::track_owned_socket (runtime_->owner, socket);
    return socket;
}

bool stage_publish_message_local (
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
    if (copy_msg_parts_to_owned_local (parts_, &entry.parts) != 0)
        return false;
    queue_->push_back (entry);
    return true;
}
}

void spot_data_plane_forwarder_t::pump_socket_commands (socket_base_t *socket_)
{
    if (!socket_)
        return;

    uint32_t ignored = 0;
    const int rc = socket_->get_events_internal (0, &ignored);
    if (rc == 0)
        return;

    if (errno == EINTR || errno == ETERM)
        return;

    errno_assert (false);
}

void spot_data_plane_forwarder_t::sync_local_fanout_targets (
  spot_runtime_t *runtime_, spot_data_plane_runtime_state_t *state_)
{
    if (!runtime_ || !state_)
        return;

    std::unordered_map<uint64_t, socket_base_t *> snapshot;
    std::deque<socket_base_t *> retired_relays;
    {
        scoped_lock_t lock (runtime_->attachment_sync);
        for (std::map<uint64_t, spot_attachment_t>::const_iterator it =
               runtime_->attachments.begin ();
             it != runtime_->attachments.end (); ++it) {
            if (it->second.kind == spot_attachment_sub && it->second.relay_socket)
                snapshot[it->first] = it->second.relay_socket;
        }
        retired_relays.swap (runtime_->retired_attachment_relay_sockets);
    }

    for (spot_data_plane_runtime_state_t::local_fanout_state_t::target_map_t::
           iterator it = state_->local_fanout.targets.begin ();
         it != state_->local_fanout.targets.end ();) {
        const std::unordered_map<uint64_t, socket_base_t *>::const_iterator snap_it =
          snapshot.find (it->first);
        if (snap_it == snapshot.end () || snap_it->second == NULL) {
            const uint64_t attachment_id = it->first;
            ++it;
            drop_local_target_state_local (state_, attachment_id);
            continue;
        }
        it->second.relay_socket = snap_it->second;
        ++it;
    }

    for (std::unordered_map<uint64_t, socket_base_t *>::const_iterator it =
           snapshot.begin ();
         it != snapshot.end (); ++it) {
        if (state_->local_fanout.targets.find (it->first) != state_->local_fanout.targets.end ())
            continue;
        spot_data_plane_runtime_state_t::local_target_state_t target;
        target.attachment_id = it->first;
        target.relay_socket = it->second;
        state_->local_fanout.targets[it->first] = target;
        if (state_->poller && it->second)
            (void) state_->poller->add (it->second, NULL, 0);
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

    while (!state_->remote_mesh.targets.empty ()) {
        const std::string endpoint = state_->remote_mesh.targets.begin ()->first;
        drop_remote_target_state_local (runtime_, state_, endpoint);
    }
}

void spot_data_plane_forwarder_t::drop_remote_mesh_target (
  spot_runtime_t *runtime_,
  spot_data_plane_runtime_state_t *state_,
  const std::string &endpoint_)
{
    drop_remote_target_state_local (runtime_, state_, endpoint_);
}

void spot_data_plane_forwarder_t::update_pending_queue_limits (
  spot_runtime_t *runtime_, spot_data_plane_runtime_state_t *state_)
{
    if (!runtime_ || !state_)
        return;

    const int total_fanout_budget =
      resolve_fanout_budget_bytes_local (runtime_->ctx ());
    const size_t half_budget =
      static_cast<size_t> (std::max (total_fanout_budget / 2,
                                     static_cast<int> (pending_queue_bytes_floor)));
    state_->local_fanout.pending_hard_limit = half_budget;
    state_->remote_mesh.pending_hard_limit = half_budget;
    state_->local_fanout.pending_pause_threshold = half_budget;
    state_->local_fanout.pending_resume_threshold =
      std::max (half_budget / 2, pending_queue_bytes_floor / 2);
    state_->remote_mesh.pending_pause_threshold = half_budget;
    state_->remote_mesh.pending_resume_threshold =
      std::max (half_budget / 2, pending_queue_bytes_floor / 2);
}

void spot_data_plane_forwarder_t::refresh_poller_interest (
  spot_data_plane_runtime_state_t *state_)
{
    if (!state_ || !state_->poller)
        return;

    const bool pause_ingress =
      state_->local_fanout.pending_bytes >= state_->local_fanout.pending_pause_threshold
      || state_->remote_mesh.pending_bytes >= state_->remote_mesh.pending_pause_threshold
      || !state_->staged.ingress_messages.empty ()
      || !state_->staged.mesh_messages.empty ();
    const bool resume_ingress =
      state_->local_fanout.pending_bytes <= state_->local_fanout.pending_resume_threshold
      && state_->remote_mesh.pending_bytes <= state_->remote_mesh.pending_resume_threshold;
    if (!state_->interest.ingress_pollin_paused && pause_ingress)
        state_->interest.ingress_pollin_paused = true;
    else if (state_->interest.ingress_pollin_paused && resume_ingress)
        state_->interest.ingress_pollin_paused = false;

    const bool pause_mesh =
      state_->remote_mesh.pending_bytes >= state_->remote_mesh.pending_pause_threshold
      || !state_->staged.mesh_messages.empty ()
      || !state_->staged.ingress_messages.empty ();
    const bool resume_mesh =
      state_->remote_mesh.pending_bytes <= state_->remote_mesh.pending_resume_threshold;
    if (!state_->interest.mesh_xsub_pollin_paused && pause_mesh)
        state_->interest.mesh_xsub_pollin_paused = true;
    else if (state_->interest.mesh_xsub_pollin_paused && resume_mesh)
        state_->interest.mesh_xsub_pollin_paused = false;

    (void) state_->poller->modify (state_->ingress,
                                   state_->interest.ingress_pollin_paused ? 0
                                                                 : ZLINK_POLLIN);
    (void) state_->poller->modify (state_->mesh_xsub,
                                   state_->interest.mesh_xsub_pollin_paused ? 0
                                                                    : ZLINK_POLLIN);

    const bool mesh_pub_need_pollout =
      !state_->remote_mesh.broadcast_pending_message_ids.empty ();
    if (mesh_pub_need_pollout != state_->remote_mesh.pollout_armed) {
        (void) state_->poller->modify (
          state_->mesh_pub,
          mesh_pub_need_pollout ? ZLINK_POLLOUT : 0);
        state_->remote_mesh.pollout_armed = mesh_pub_need_pollout;
    }

    for (spot_data_plane_runtime_state_t::local_fanout_state_t::target_map_t::
           iterator it = state_->local_fanout.targets.begin ();
         it != state_->local_fanout.targets.end (); ++it) {
        if (!it->second.relay_socket)
            continue;
        const bool need_pollout = !it->second.pending_message_ids.empty ();
        if (need_pollout == it->second.pollout_armed)
            continue;
        (void) state_->poller->modify (it->second.relay_socket,
                                       need_pollout ? ZLINK_POLLOUT : 0);
        it->second.pollout_armed = need_pollout;
    }

    for (spot_data_plane_runtime_state_t::remote_mesh_state_t::target_map_t::
           iterator it = state_->remote_mesh.targets.begin ();
         it != state_->remote_mesh.targets.end (); ++it) {
        if (!it->second.sender_socket)
            continue;
        const bool need_pollout = !it->second.pending_message_ids.empty ();
        if (need_pollout == it->second.pollout_armed)
            continue;
        (void) state_->poller->modify (it->second.sender_socket,
                                       need_pollout ? ZLINK_POLLOUT : 0);
        it->second.pollout_armed = need_pollout;
    }
}

int spot_data_plane_forwarder_t::forward_local_fanout (
  spot_runtime_t *runtime_,
  spot_data_plane_runtime_state_t *state_,
  const std::string &topic_,
  const spot_owned_msg_parts_t &parts_)
{
    if (!runtime_ || !state_ || topic_.empty ()) {
        errno = EINVAL;
        return -1;
    }

    if (state_->local_fanout.targets.empty ())
        return 0;

    const size_t encoded_bytes = spot_msg_parts_encoded_bytes (parts_);
    if (!pending_queue_has_room_local (state_->local_fanout.pending_bytes,
                                       state_->local_fanout.pending_hard_limit,
                                       encoded_bytes)) {
        refresh_poller_interest (state_);
        errno = EAGAIN;
        return -1;
    }

    uint64_t local_pending_message_id = 0;
    for (spot_data_plane_runtime_state_t::local_fanout_state_t::target_map_t::
           iterator target_it = state_->local_fanout.targets.begin ();
         target_it != state_->local_fanout.targets.end (); ++target_it) {
        spot_data_plane_runtime_state_t::local_target_state_t &target =
          target_it->second;
        if (!target.relay_socket)
            continue;

        pump_socket_commands (target.relay_socket);
        target.relay_socket->set_all_pipes_nodelay ();
        if (target.pending_message_ids.empty ()
            && spot_publish_msg_parts (target.relay_socket, topic_, parts_) == 0) {
            continue;
        }

        if (errno != EAGAIN && !target.pending_message_ids.empty ())
            errno = EAGAIN;
        if (errno != EAGAIN
            || !enqueue_local_target_message_local (
              state_, &target, topic_, parts_, &local_pending_message_id)) {
            return -1;
        }
    }

    refresh_poller_interest (state_);
    return 0;
}

int spot_data_plane_forwarder_t::forward_mesh_pub (
  spot_runtime_t *runtime_,
  spot_data_plane_runtime_state_t *state_,
  const std::string &topic_,
  const spot_owned_msg_parts_t &parts_)
{
    if (!runtime_ || !state_ || topic_.empty ()) {
        errno = EINVAL;
        return -1;
    }

    const size_t encoded_bytes = spot_msg_parts_encoded_bytes (parts_);
    if (!pending_queue_has_room_local (state_->remote_mesh.pending_bytes,
                                       state_->remote_mesh.pending_hard_limit,
                                       encoded_bytes)) {
        refresh_poller_interest (state_);
        errno = EAGAIN;
        return -1;
    }

    pump_socket_commands (state_->mesh_pub);
    state_->mesh_pub->set_all_pipes_nodelay ();
    if (state_->remote_mesh.broadcast_pending_message_ids.empty ()
        && spot_publish_msg_parts (state_->mesh_pub, topic_, parts_) == 0) {
        return 0;
    }

    if (errno != EAGAIN && !state_->remote_mesh.broadcast_pending_message_ids.empty ())
        errno = EAGAIN;
    if (errno != EAGAIN) {
        return -1;
    }

    uint64_t mesh_pending_message_id = 0;
    if (!enqueue_mesh_broadcast_pending_local (state_, topic_, parts_,
                                               &mesh_pending_message_id)) {
        return -1;
    }

    refresh_poller_interest (state_);
    return 0;
}

int spot_data_plane_forwarder_t::stage_message (
  spot_data_plane_runtime_state_t *state_,
  const std::string &topic_,
  const spot_owned_msg_parts_t &parts_,
  bool source_mesh_,
  bool need_local_,
  bool need_mesh_)
{
    if (!state_) {
        errno = EINVAL;
        return -1;
    }

    std::deque<spot_data_plane_runtime_state_t::staged_publish_entry_t> &queue =
      source_mesh_ ? state_->staged.mesh_messages
                   : state_->staged.ingress_messages;
    if (!stage_publish_message_local (&queue, topic_, parts_, need_local_,
                                      need_mesh_)) {
        if (errno == 0)
            errno = ENOMEM;
        return -1;
    }
    refresh_poller_interest (state_);
    return 0;
}

int spot_data_plane_forwarder_t::flush_staged_messages (
  spot_runtime_t *runtime_, spot_data_plane_runtime_state_t *state_)
{
    if (!runtime_ || !state_)
        return 0;

    while (!state_->staged.ingress_messages.empty ()) {
        spot_data_plane_runtime_state_t::staged_publish_entry_t &entry =
          state_->staged.ingress_messages.front ();
        if (entry.need_local) {
            if (state_->local_fanout.targets.empty ()) {
                entry.need_local = false;
            } else if (forward_local_fanout (runtime_, state_, entry.topic,
                                             entry.parts)
                       != 0) {
                if (errno == EAGAIN)
                    break;
                return -1;
            } else {
                entry.need_local = false;
            }
        }
        if (entry.need_mesh) {
            if (forward_mesh_pub (runtime_, state_, entry.topic, entry.parts)
                != 0) {
                if (errno == EAGAIN)
                    break;
                return -1;
            }
            entry.need_mesh = false;
        }
        if (entry.need_local || entry.need_mesh)
            break;
        spot_clear_msg_parts (&entry.parts);
        state_->staged.ingress_messages.pop_front ();
    }

    while (!state_->staged.mesh_messages.empty ()) {
        spot_data_plane_runtime_state_t::staged_publish_entry_t &entry =
          state_->staged.mesh_messages.front ();
        if (entry.need_local) {
            if (state_->local_fanout.targets.empty ()) {
                entry.need_local = false;
            } else if (forward_local_fanout (runtime_, state_, entry.topic,
                                             entry.parts)
                       != 0) {
                if (errno == EAGAIN)
                    break;
                return -1;
            } else {
                entry.need_local = false;
            }
        }
        if (entry.need_local)
            break;
        spot_clear_msg_parts (&entry.parts);
        state_->staged.mesh_messages.pop_front ();
    }

    refresh_poller_interest (state_);
    return 0;
}

int spot_data_plane_forwarder_t::flush_local_fanout_pending (
  spot_runtime_t *runtime_,
  spot_data_plane_runtime_state_t *state_,
  socket_base_t *relay_socket_)
{
    (void) runtime_;

    if (!runtime_ || !state_)
        return 0;

    for (spot_data_plane_runtime_state_t::local_fanout_state_t::target_map_t::
           iterator it = state_->local_fanout.targets.begin ();
         it != state_->local_fanout.targets.end (); ++it) {
        spot_data_plane_runtime_state_t::local_target_state_t &target = it->second;
        if (!target.relay_socket
            || (relay_socket_ && target.relay_socket != relay_socket_)) {
            continue;
        }

        while (!target.pending_message_ids.empty ()) {
            const uint64_t message_id = target.pending_message_ids.front ();
            spot_data_plane_runtime_state_t::local_fanout_state_t::pending_message_map_t::
              iterator msg_it = state_->local_fanout.pending_messages.find (message_id);
            if (msg_it == state_->local_fanout.pending_messages.end ()) {
                target.pending_message_ids.pop_front ();
                continue;
            }

            pump_socket_commands (target.relay_socket);
            target.relay_socket->set_all_pipes_nodelay ();
            if (spot_publish_msg_parts (target.relay_socket, msg_it->second.topic,
                                        msg_it->second.parts)
                != 0) {
                if (errno == EAGAIN)
                    break;
                return -1;
            }

            target.pending_message_ids.pop_front ();
            release_local_pending_ref_local (state_, message_id);
        }
    }

    refresh_poller_interest (state_);
    return 0;
}

int spot_data_plane_forwarder_t::flush_mesh_pub_pending (
  spot_runtime_t *runtime_,
  spot_data_plane_runtime_state_t *state_,
  socket_base_t *sender_socket_)
{
    if (!runtime_ || !state_)
        return 0;

    if (!sender_socket_ || sender_socket_ == state_->mesh_pub) {
        while (!state_->remote_mesh.broadcast_pending_message_ids.empty ()) {
            const uint64_t message_id =
              state_->remote_mesh.broadcast_pending_message_ids.front ();
            spot_data_plane_runtime_state_t::remote_mesh_state_t::pending_message_map_t::
              iterator msg_it = state_->remote_mesh.pending_messages.find (message_id);
            if (msg_it == state_->remote_mesh.pending_messages.end ()) {
                state_->remote_mesh.broadcast_pending_message_ids.pop_front ();
                continue;
            }

            pump_socket_commands (state_->mesh_pub);
            state_->mesh_pub->set_all_pipes_nodelay ();
            if (spot_publish_msg_parts (state_->mesh_pub, msg_it->second.topic,
                                        msg_it->second.parts)
                != 0) {
                if (errno == EAGAIN)
                    break;
                return -1;
            }

            state_->remote_mesh.broadcast_pending_message_ids.pop_front ();
            release_mesh_pending_ref_local (state_, message_id);
        }
    }

    for (spot_data_plane_runtime_state_t::remote_mesh_state_t::target_map_t::
           iterator it = state_->remote_mesh.targets.begin ();
         it != state_->remote_mesh.targets.end (); ++it) {
        spot_data_plane_runtime_state_t::remote_target_state_t &target =
          it->second;
        if (!target.sender_socket
            || (sender_socket_ && target.sender_socket != sender_socket_)) {
            continue;
        }

        while (!target.pending_message_ids.empty ()) {
            const uint64_t message_id = target.pending_message_ids.front ();
            spot_data_plane_runtime_state_t::remote_mesh_state_t::pending_message_map_t::
              iterator msg_it = state_->remote_mesh.pending_messages.find (message_id);
            if (msg_it == state_->remote_mesh.pending_messages.end ()) {
                target.pending_message_ids.pop_front ();
                continue;
            }

            if (send_remote_mesh_message_local (target.sender_socket,
                                                msg_it->second.topic,
                                                msg_it->second.parts)
                != 0) {
                if (errno == EAGAIN)
                    break;
                return -1;
            }

            target.pending_message_ids.pop_front ();
            release_mesh_pending_ref_local (state_, message_id);
        }
    }

    refresh_poller_interest (state_);
    return 0;
}

int spot_data_plane_forwarder_t::resolve_internal_hwm_override (
  const char *env_name_, int default_value_)
{
    if (!env_name_ || env_name_[0] == '\0')
        return default_value_;

    const char *value = getenv (env_name_);
    if (!value || value[0] == '\0')
        return default_value_;

    char *end = NULL;
    errno = 0;
    const long parsed = strtol (value, &end, 10);
    if (errno != 0 || end == value)
        return default_value_;
    if (parsed < 0)
        return 0;
    if (parsed > INT_MAX)
        return INT_MAX;
    return static_cast<int> (parsed);
}

int spot_data_plane_forwarder_t::recv_and_forward_ingress (
  socket_base_t *src_,
  socket_base_t *mesh_pub_,
  socket_base_t *fanout_,
  spot_runtime_t *runtime_,
  spot_data_plane_runtime_state_t *state_,
  const spot_node_t *node_)
{
    (void) fanout_;
    (void) node_;

    if (!src_ || !runtime_ || !state_) {
        errno = EINVAL;
        return -1;
    }

    unsigned int forwarded_messages = 0;
    size_t forwarded_bytes = 0;

    for (;;) {
        std::string topic;
        spot_owned_msg_parts_t parts;
        size_t wire_bytes = 0;
        if (spot_recv_logical_message_parts (
              src_, true, &topic, &parts, &wire_bytes)
            != 0) {
            if (errno == EAGAIN)
                return 0;
            return -1;
        }

        if (spot_direct_route_debug_enabled_local ()) {
            std::fprintf (stderr,
                          "[spot-direct] ingress topic=%s local_targets=%zu mesh_targets=%zu parts=%zu\n",
                          topic.c_str (), state_->local_fanout.targets.size (),
                          state_->remote_mesh.targets.size (), parts.size ());
        }

        if (!state_->local_fanout.targets.empty ()
            && forward_local_fanout (runtime_, state_, topic, parts) != 0) {
            if (errno == EAGAIN
                && stage_message (state_, topic, parts, false, true,
                                  mesh_pub_ != NULL)
                     == 0) {
                spot_clear_msg_parts (&parts);
                return 0;
            }
            spot_clear_msg_parts (&parts);
            return -1;
        }

        if (mesh_pub_ && forward_mesh_pub (runtime_, state_, topic, parts) != 0) {
            if (errno == EAGAIN
                && stage_message (state_, topic, parts, false, false, true)
                     == 0) {
                spot_clear_msg_parts (&parts);
                return 0;
            }
            spot_clear_msg_parts (&parts);
            return -1;
        }

        spot_clear_msg_parts (&parts);

        ++forwarded_messages;
        forwarded_bytes += wire_bytes;
        if (forwarded_messages >= ingress_forward_batch_limit
            || forwarded_bytes >= ingress_forward_batch_bytes_limit)
            return 0;
    }
}
}
