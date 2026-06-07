/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/spot/data_plane/spot_data_plane_internal.hpp"
#include "services/spot/data_plane/spot_data_plane_message_io_internal.hpp"
#include "services/spot/common/spot_message_parts_internal.hpp"

#include "api/socket/request_reply_protocol_internal.hpp"
#include "core/ctx.hpp"
#include "core/multipart_send_txn.hpp"
#include "services/spot/common/spot_auto_hwm_internal.hpp"
#include "services/spot/common/spot_control_protocol.hpp"
#include "services/spot/node/spot_node_access.hpp"
#include "services/spot/node/spot_node.hpp"
#include "services/spot/runtime/spot_runtime.hpp"
#include "services/spot/pubsub/spot_subject_access.hpp"
#include "sockets/common/socket_base.hpp"
#include "utils/err.hpp"
#include "utils/debug_log.hpp"

#include <errno.h>
#include <chrono>
#include <functional>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <utility>

namespace zlink
{
namespace
{
namespace spot_io = zlink::spot_data_plane_message_io;

const bool spot_direct_route_debug_on = debug_env_enabled ("ZLINK_DEBUG_SPOT_DIRECT_ROUTE");

bool spot_direct_route_debug_enabled_local ()
{
    return spot_direct_route_debug_on;
}

static const size_t default_queue_message_unit_bytes = 64 * 1024;
static const size_t publish_ingress_drain_batch_limit = 2048;
static const size_t publish_ingress_drain_batch_bytes_limit = 16 * 1024 * 1024;

struct queue_admission_plan_t
{
    queue_admission_plan_t () :
        unlimited (false),
        message_limit (1),
        byte_limit (default_queue_message_unit_bytes),
        resume_message_limit (1),
        resume_byte_limit (default_queue_message_unit_bytes / 2)
    {
    }

    bool unlimited;
    size_t message_limit;
    size_t byte_limit;
    size_t resume_message_limit;
    size_t resume_byte_limit;
};

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
        std::fprintf (stderr, "[spot-direct] send remote socket=%d topic=%s parts=%zu\n",
                      socket_->socket_id (), topic_.c_str (), parts_.size ());
    }
    if (parts_.empty ()) {
        zlink_msg_t empty;
        zlink_msg_init (&empty);
        if (zlink_msg_init_size (&empty, 0) != 0)
            return -1;
        const int rc = logical_multipart_send_prefixed_frames (
          socket_, spot_control_protocol::peer_pub_route_topic,
          strlen (spot_control_protocol::peer_pub_route_topic), 0, topic_.data (), topic_.size (),
          0, &empty, 1, ZLINK_DONTWAIT);
        zlink_msg_close (&empty);
        return rc;
    }

    return logical_multipart_send_prefixed_frames (
      socket_, spot_control_protocol::peer_pub_route_topic,
      strlen (spot_control_protocol::peer_pub_route_topic), 0, topic_.data (), topic_.size (), 0,
      const_cast<zlink_msg_t *> (&parts_[0]), parts_.size (), ZLINK_DONTWAIT);
}

socket_base_t *create_remote_mesh_sender_socket_local (spot_runtime_t *runtime_,
                                                       const std::string &route_endpoint_)
{
    if (!runtime_ || !runtime_->owner || route_endpoint_.empty ()) {
        errno = EINVAL;
        return NULL;
    }

    ctx_t *ctx = runtime_->ctx ();
    socket_base_t *socket = ctx ? ctx->create_socket (ZLINK_CORE_SOCKET_DEALER) : NULL;
    if (!socket)
        return NULL;

    socket->set_auto_hwm_policy_enabled (false);
    size_t local_pub_count = 0;
    size_t local_sub_count = 0;
    size_t connected_peer_count = 0;
    size_t active_peer_count = 0;
    runtime_->snapshot_auto_hwm_inputs (&local_pub_count, &local_sub_count, &connected_peer_count,
                                        &active_peer_count);
    apply_spot_internal_auto_hwm (
      ctx, socket,
      spot_internal_auto_hwm_policy_t{auto_hwm_role_spot_data, ZLINK_CORE_SOCKET_DEALER,
                                      connected_peer_count, active_peer_count, 0, 0, true, true,
                                      true, true});

    std::string ca;
    std::string host;
    int trust_system = 0;
    spot_node_access_t::snapshot_tls_client_config (runtime_->owner, &ca, &host, &trust_system);
    const int linger = 0;
    const int immediate = 1;
    socket->setsockopt (ZLINK_INTERNAL_OPT_LINGER, &linger, sizeof (linger));
    socket->setsockopt (ZLINK_INTERNAL_OPT_IMMEDIATE, &immediate, sizeof (immediate));
    if (spot_node_access_t::apply_tls_client (runtime_->owner, socket, ca, host, trust_system) != 0
        || socket->connect (route_endpoint_.c_str ()) != 0) {
        const int saved_errno = errno != 0 ? errno : EIO;
        socket->close ();
        errno = saved_errno;
        return NULL;
    }

    spot_node_access_t::track_owned_socket (runtime_->owner, socket);
    return socket;
}

void refresh_fixed_poller_interest_local (spot_data_plane_runtime_state_t *state_)
{
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

void refresh_target_pollout_interest_local (spot_data_plane_runtime_state_t *state_,
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

void refresh_local_target_pollout_interest_local (
  spot_data_plane_runtime_state_t *state_,
  spot_data_plane_runtime_state_t::local_target_state_t *target_)
{
    if (!target_)
        return;
    refresh_target_pollout_interest_local (state_, target_->relay_socket,
                                           target_->pending_message_ids, &target_->pollout_armed);
}

void refresh_remote_target_pollout_interest_local (
  spot_data_plane_runtime_state_t *state_,
  spot_data_plane_runtime_state_t::remote_target_state_t *target_)
{
    if (!target_)
        return;
    refresh_target_pollout_interest_local (state_, target_->sender_socket,
                                           target_->pending_message_ids, &target_->pollout_armed);
}

size_t publish_message_unit_bytes (spot_runtime_t *runtime_, size_t entry_bytes_)
{
    if (runtime_ && runtime_->owner) {
        const spot_node_pub_defaults_t defaults = runtime_->owner->load_pub_defaults ();
        if (defaults.auto_hwm_msg_unit_bytes.enabled && defaults.auto_hwm_msg_unit_bytes.value > 0)
            return static_cast<size_t> (defaults.auto_hwm_msg_unit_bytes.value);
    }
    return entry_bytes_ > 0 ? entry_bytes_ : 1;
}

queue_admission_plan_t make_queue_admission_plan (int slots_, size_t message_unit_)
{
    queue_admission_plan_t plan;
    if (slots_ == 0) {
        plan.unlimited = true;
        plan.message_limit = 0;
        plan.byte_limit = 0;
        plan.resume_message_limit = 0;
        plan.resume_byte_limit = 0;
        return plan;
    }
    const size_t slots = static_cast<size_t> (slots_ > 0 ? slots_ : 1);
    const size_t unit = message_unit_ > 0 ? message_unit_ : 1;
    plan.unlimited = false;
    plan.message_limit = slots;
    plan.byte_limit = slots > SIZE_MAX / unit ? SIZE_MAX : slots * unit;
    plan.resume_message_limit = slots / 2;
    plan.resume_byte_limit = plan.byte_limit / 2;
    return plan;
}

bool publish_ingress_has_room (
  const spot_data_plane_runtime_state_t::publish_ingress_queue_t &queue_,
  const queue_admission_plan_t &plan_,
  size_t message_bytes_)
{
    if (plan_.unlimited)
        return true;
    if (queue_.messages.size () >= plan_.message_limit)
        return false;
    if (queue_.messages.empty ())
        return true;
    if (message_bytes_ > plan_.byte_limit)
        return false;
    return queue_.queued_bytes <= plan_.byte_limit - message_bytes_;
}

bool publish_ingress_can_resume (
  const spot_data_plane_runtime_state_t::publish_ingress_queue_t &queue_,
  const queue_admission_plan_t &plan_)
{
    if (plan_.unlimited)
        return true;
    return queue_.messages.size () <= plan_.resume_message_limit
           && queue_.queued_bytes <= plan_.resume_byte_limit;
}

bool wait_for_queue_room (std::condition_variable &cv_,
                          std::unique_lock<std::mutex> &lock_,
                          int sndtimeo_ms_,
                          const std::function<bool ()> &ready_)
{
    if (sndtimeo_ms_ == 0)
        return ready_ ();
    if (sndtimeo_ms_ < 0) {
        while (!ready_ ())
            cv_.wait (lock_);
        return true;
    }
    const std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now () + std::chrono::milliseconds (sndtimeo_ms_);
    while (!ready_ ()) {
        if (cv_.wait_until (lock_, deadline) == std::cv_status::timeout && !ready_ ())
            return false;
    }
    return true;
}

spot_data_plane_runtime_state_t::local_target_state_t *
find_local_target_by_socket_local (spot_data_plane_runtime_state_t *state_,
                                   socket_base_t *relay_socket_)
{
    if (!state_ || !relay_socket_)
        return NULL;

    std::unordered_map<socket_base_t *, uint64_t>::iterator socket_it =
      state_->local_fanout.target_by_socket.find (relay_socket_);
    if (socket_it == state_->local_fanout.target_by_socket.end ())
        return NULL;

    spot_data_plane_runtime_state_t::local_fanout_state_t::target_map_t::iterator target_it =
      state_->local_fanout.targets.find (socket_it->second);
    if (target_it == state_->local_fanout.targets.end () || !target_it->second.relay_socket)
        return NULL;

    return &target_it->second;
}

int flush_local_target_pending_local (
  spot_data_plane_runtime_state_t *state_,
  spot_data_plane_runtime_state_t::local_target_state_t *target_)
{
    if (!state_ || !target_ || !target_->relay_socket)
        return 0;

    while (!target_->pending_message_ids.empty ()) {
        const uint64_t message_id = target_->pending_message_ids.front ();
        spot_data_plane_runtime_state_t::local_fanout_state_t::pending_message_map_t::iterator
          msg_it = state_->local_fanout.pending_messages.find (message_id);
        if (msg_it == state_->local_fanout.pending_messages.end ()) {
            target_->pending_message_ids.pop_front ();
            continue;
        }

        spot_data_plane_forwarder_t::pump_socket_commands (target_->relay_socket);
        if (spot_publish_msg_parts (target_->relay_socket, msg_it->second.topic,
                                    msg_it->second.parts)
            != 0) {
            if (errno == EAGAIN)
                break;
            return -1;
        }

        target_->pending_message_ids.pop_front ();
        spot_data_plane_pending_t::release_local_pending_ref (state_, message_id);
    }

    return 0;
}

int flush_mesh_broadcast_pending_local (spot_data_plane_runtime_state_t *state_)
{
    if (!state_)
        return 0;

    while (!state_->remote_mesh.broadcast_pending_message_ids.empty ()) {
        const uint64_t message_id = state_->remote_mesh.broadcast_pending_message_ids.front ();
        spot_data_plane_runtime_state_t::remote_mesh_state_t::pending_message_map_t::iterator
          msg_it = state_->remote_mesh.pending_messages.find (message_id);
        if (msg_it == state_->remote_mesh.pending_messages.end ()) {
            state_->remote_mesh.broadcast_pending_message_ids.pop_front ();
            continue;
        }

        spot_data_plane_forwarder_t::pump_socket_commands (state_->mesh_pub);
        if (spot_publish_msg_parts (state_->mesh_pub, msg_it->second.topic, msg_it->second.parts)
            != 0) {
            if (errno == EAGAIN)
                break;
            return -1;
        }

        state_->remote_mesh.broadcast_pending_message_ids.pop_front ();
        spot_data_plane_pending_t::release_mesh_pending_ref (state_, message_id);
    }

    return 0;
}

int flush_remote_target_pending_local (
  spot_data_plane_runtime_state_t *state_,
  spot_data_plane_runtime_state_t::remote_target_state_t *target_)
{
    if (!state_ || !target_ || !target_->sender_socket)
        return 0;

    while (!target_->pending_message_ids.empty ()) {
        const uint64_t message_id = target_->pending_message_ids.front ();
        spot_data_plane_runtime_state_t::remote_mesh_state_t::pending_message_map_t::iterator
          msg_it = state_->remote_mesh.pending_messages.find (message_id);
        if (msg_it == state_->remote_mesh.pending_messages.end ()) {
            target_->pending_message_ids.pop_front ();
            continue;
        }

        if (send_remote_mesh_message_local (target_->sender_socket, msg_it->second.topic,
                                            msg_it->second.parts)
            != 0) {
            if (errno == EAGAIN)
                break;
            return -1;
        }

        target_->pending_message_ids.pop_front ();
        spot_data_plane_pending_t::release_mesh_pending_ref (state_, message_id);
    }

    return 0;
}

int copy_raw_parts_to_owned (zlink_msg_t *parts_, size_t part_count_, spot_owned_msg_parts_t *out_)
{
    if (!out_) {
        errno = EINVAL;
        return -1;
    }
    spot_clear_msg_parts (out_);
    if (part_count_ == 0) {
        zlink_msg_t empty;
        memset (&empty, 0, sizeof (empty));
        spot_init_msg_frame (&empty);
        if (zlink_msg_init_size (&empty, 0) != 0)
            return -1;
        out_->push_back (empty);
        return 0;
    }

    for (size_t i = 0; i < part_count_; ++i) {
        zlink_msg_t frame;
        memset (&frame, 0, sizeof (frame));
        spot_init_msg_frame (&frame);
        if (zlink_msg_copy (&frame, &parts_[i]) != 0) {
            const int err = errno;
            spot_close_msg_frame (&frame);
            spot_clear_msg_parts (out_);
            errno = err;
            return -1;
        }
        out_->push_back (frame);
    }
    return 0;
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

    for (spot_data_plane_runtime_state_t::local_fanout_state_t::target_map_t::iterator it =
           state_->local_fanout.targets.begin ();
         it != state_->local_fanout.targets.end ();) {
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
                state_->local_fanout.target_by_socket.erase (it->second.relay_socket);
            it->second.relay_socket = snap_it->second;
            if (it->second.relay_socket)
                state_->local_fanout.target_by_socket[it->second.relay_socket] = it->first;
        }
        ++it;
    }

    for (std::unordered_map<uint64_t, socket_base_t *>::const_iterator it = snapshot.begin ();
         it != snapshot.end (); ++it) {
        if (state_->local_fanout.targets.find (it->first) != state_->local_fanout.targets.end ())
            continue;
        spot_data_plane_runtime_state_t::local_target_state_t target;
        target.attachment_id = it->first;
        target.relay_socket = it->second;
        state_->local_fanout.targets[it->first] = target;
        state_->local_fanout.target_by_socket[it->second] = it->first;
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

    while (!state_->remote_mesh.targets.empty ()) {
        const std::string endpoint = state_->remote_mesh.targets.begin ()->first;
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
      std::max (fanout_hwm / 2, static_cast<int> (default_queue_message_unit_bytes)));
    state_->local_fanout.pending_hard_limit = pending_limit;
    state_->remote_mesh.pending_hard_limit = pending_limit;
    state_->local_fanout.pending_pause_threshold = pending_limit;
    state_->local_fanout.pending_resume_threshold =
      std::max (pending_limit / 2, default_queue_message_unit_bytes / 2);
    state_->remote_mesh.pending_pause_threshold = pending_limit;
    state_->remote_mesh.pending_resume_threshold =
      std::max (pending_limit / 2, default_queue_message_unit_bytes / 2);
}

void spot_data_plane_forwarder_t::refresh_poller_interest (spot_data_plane_runtime_state_t *state_)
{
    if (!state_ || !state_->poller)
        return;

    refresh_fixed_poller_interest_local (state_);

    for (spot_data_plane_runtime_state_t::local_fanout_state_t::target_map_t::iterator it =
           state_->local_fanout.targets.begin ();
         it != state_->local_fanout.targets.end (); ++it) {
        refresh_local_target_pollout_interest_local (state_, &it->second);
    }

    for (spot_data_plane_runtime_state_t::remote_mesh_state_t::target_map_t::iterator it =
           state_->remote_mesh.targets.begin ();
         it != state_->remote_mesh.targets.end (); ++it) {
        refresh_remote_target_pollout_interest_local (state_, &it->second);
    }
}

int spot_data_plane_forwarder_t::forward_local_fanout (spot_runtime_t *runtime_,
                                                       spot_data_plane_runtime_state_t *state_,
                                                       const std::string &topic_,
                                                       const spot_owned_msg_parts_t &parts_,
                                                       size_t precomputed_encoded_bytes_)
{
    if (!runtime_ || !state_ || topic_.empty ()) {
        errno = EINVAL;
        return -1;
    }

    if (state_->local_fanout.targets.empty ())
        return 0;

    const size_t encoded_bytes = precomputed_encoded_bytes_ > 0
                                   ? precomputed_encoded_bytes_
                                   : spot_msg_parts_encoded_bytes (parts_);
    if (!spot_data_plane_pending_t::queue_has_room (state_->local_fanout.pending_bytes,
                                                    state_->local_fanout.pending_hard_limit,
                                                    encoded_bytes)) {
        refresh_poller_interest (state_);
        errno = EAGAIN;
        return -1;
    }

    uint64_t local_pending_message_id = 0;
    for (spot_data_plane_runtime_state_t::local_fanout_state_t::target_map_t::iterator target_it =
           state_->local_fanout.targets.begin ();
         target_it != state_->local_fanout.targets.end ();) {
        spot_data_plane_runtime_state_t::local_target_state_t &target = target_it->second;
        if (!target.relay_socket) {
            ++target_it;
            continue;
        }

        pump_socket_commands (target.relay_socket);
        if (target.pending_message_ids.empty ()
            && spot_publish_msg_parts (target.relay_socket, topic_, parts_) == 0) {
            ++target_it;
            continue;
        }

        if (errno != EAGAIN && !target.pending_message_ids.empty ())
            errno = EAGAIN;
        if (errno != EAGAIN
            || !spot_data_plane_pending_t::enqueue_local_target_message (
              state_, &target, topic_, parts_, &local_pending_message_id, encoded_bytes)) {
            return -1;
        }
        ++target_it;
    }

    refresh_poller_interest (state_);
    return 0;
}

int spot_data_plane_forwarder_t::forward_mesh_pub (spot_runtime_t *runtime_,
                                                   spot_data_plane_runtime_state_t *state_,
                                                   const std::string &topic_,
                                                   const spot_owned_msg_parts_t &parts_,
                                                   size_t precomputed_encoded_bytes_)
{
    if (!runtime_ || !state_ || topic_.empty ()) {
        errno = EINVAL;
        return -1;
    }

    const size_t encoded_bytes = precomputed_encoded_bytes_ > 0
                                   ? precomputed_encoded_bytes_
                                   : spot_msg_parts_encoded_bytes (parts_);
    if (!spot_data_plane_pending_t::queue_has_room (state_->remote_mesh.pending_bytes,
                                                    state_->remote_mesh.pending_hard_limit,
                                                    encoded_bytes)) {
        refresh_poller_interest (state_);
        errno = EAGAIN;
        return -1;
    }

    pump_socket_commands (state_->mesh_pub);
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
    if (!spot_data_plane_pending_t::enqueue_mesh_broadcast_pending (
          state_, topic_, parts_, &mesh_pending_message_id, encoded_bytes)) {
        return -1;
    }

    refresh_poller_interest (state_);
    return 0;
}

int spot_data_plane_forwarder_t::stage_message (spot_data_plane_runtime_state_t *state_,
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
      source_mesh_ ? state_->staged.mesh_messages : state_->staged.ingress_messages;
    if (!spot_data_plane_pending_t::stage_publish_message (&queue, topic_, parts_, need_local_,
                                                           need_mesh_)) {
        if (errno == 0)
            errno = ENOMEM;
        return -1;
    }
    refresh_poller_interest (state_);
    return 0;
}

int spot_data_plane_forwarder_t::enqueue_publish_ingress (spot_runtime_t *runtime_,
                                                          const char *topic_,
                                                          zlink_msg_t *parts_,
                                                          size_t part_count_,
                                                          zlink_send_flags_t flags_,
                                                          int sndtimeo_ms_)
{
    if (!runtime_ || !topic_ || !*topic_) {
        errno = EINVAL;
        return -1;
    }

    spot_data_plane_runtime_state_t::staged_publish_entry_t entry;
    entry.topic = topic_;
    entry.need_local = true;
    entry.need_mesh = runtime_->mesh_pub != NULL;
    if (copy_raw_parts_to_owned (parts_, part_count_, &entry.parts) != 0)
        return -1;
    entry.encoded_bytes = spot_msg_parts_encoded_bytes (entry.parts);

    spot_data_plane_runtime_state_t::publish_ingress_queue_t &queue =
      runtime_->execution.data_plane_state.publish_ingress;
    const int slots = spot_node_pubsub_admission_hwm (runtime_->hwm_config_snapshot ());
    const queue_admission_plan_t plan =
      make_queue_admission_plan (slots, publish_message_unit_bytes (runtime_, entry.encoded_bytes));
    std::unique_lock<std::mutex> lock (queue.mutex);
    if (!publish_ingress_has_room (queue, plan, entry.encoded_bytes))
        queue.backpressure_active = true;
    const bool dontwait = (flags_ & ZLINK_DONTWAIT) != 0;
    const bool ready =
      wait_for_queue_room (queue.cv, lock, dontwait ? 0 : sndtimeo_ms_, [&queue, &plan, &entry] () {
          return queue.closed || publish_ingress_has_room (queue, plan, entry.encoded_bytes);
      });
    if (!ready) {
        spot_clear_msg_parts (&entry.parts);
        errno = EAGAIN;
        return -1;
    }
    if (queue.closed) {
        spot_clear_msg_parts (&entry.parts);
        errno = ESHUTDOWN;
        return -1;
    }

    queue.queued_bytes += entry.encoded_bytes;
    queue.messages.push_back (std::move (entry));
    if (!publish_ingress_has_room (queue, plan, 1))
        queue.backpressure_active = true;
    if (!queue.signal_armed && queue.signaler.valid ()) {
        queue.signal_armed = true;
        queue.signaler.send ();
    }
    lock.unlock ();

    zlink_multipart_close (parts_, part_count_);
    return 0;
}

int spot_data_plane_forwarder_t::drain_publish_ingress_queue (
  spot_runtime_t *runtime_, spot_data_plane_runtime_state_t *state_)
{
    if (!runtime_ || !state_)
        return 0;

    std::deque<spot_data_plane_runtime_state_t::staged_publish_entry_t> local;
    bool notify_recovery = false;
    {
        spot_data_plane_runtime_state_t::publish_ingress_queue_t &queue = state_->publish_ingress;
        std::lock_guard<std::mutex> lock (queue.mutex);
        size_t moved_bytes = 0;
        size_t moved_count = 0;
        while (!queue.messages.empty ()) {
            const size_t entry_bytes =
              queue.messages.front ().encoded_bytes > 0 ? queue.messages.front ().encoded_bytes : 1;
            if (moved_count > 0
                && (moved_count >= publish_ingress_drain_batch_limit
                    || moved_bytes + entry_bytes > publish_ingress_drain_batch_bytes_limit))
                break;
            moved_bytes += entry_bytes;
            local.push_back (std::move (queue.messages.front ()));
            queue.messages.pop_front ();
            ++moved_count;
        }
        queue.queued_bytes =
          queue.queued_bytes > moved_bytes ? queue.queued_bytes - moved_bytes : 0;
        if (queue.backpressure_active
            && publish_ingress_can_resume (
              queue, make_queue_admission_plan (
                       spot_node_pubsub_admission_hwm (runtime_->hwm_config_snapshot ()), 1))) {
            queue.backpressure_active = false;
            notify_recovery = true;
        }
        if (!queue.messages.empty () && !queue.signal_armed && queue.signaler.valid ()) {
            queue.signal_armed = true;
            queue.signaler.send ();
        }
        queue.cv.notify_all ();
    }
    if (notify_recovery)
        notify_spot_send_ready_recovery (runtime_->owner);

    while (!local.empty ()) {
        spot_data_plane_runtime_state_t::staged_publish_entry_t &entry = local.front ();
        if (stage_message (state_, entry.topic, entry.parts, false, entry.need_local,
                           entry.need_mesh)
            != 0) {
            spot_clear_msg_parts (&entry.parts);
            return -1;
        }
        spot_clear_msg_parts (&entry.parts);
        local.pop_front ();
    }
    return flush_staged_messages (runtime_, state_);
}

int spot_data_plane_forwarder_t::drain_pub_ingress_socket (spot_runtime_t *runtime_,
                                                           spot_data_plane_runtime_state_t *state_)
{
    if (!runtime_ || !state_ || !state_->pub_ingress_sub)
        return 0;

    unsigned int processed = 0;
    size_t processed_bytes = 0;
    for (;;) {
        msg_t topic_msg;
        if (topic_msg.init () != 0)
            return -1;
        if (state_->pub_ingress_sub->recv (&topic_msg, ZLINK_DONTWAIT) != 0) {
            const int err = errno;
            topic_msg.close ();
            if (err == EAGAIN || err == EINTR)
                break;
            errno = err;
            return -1;
        }

        processed_bytes += topic_msg.size ();
        const bool has_payload = (topic_msg.flags () & msg_t::more) != 0;
        std::string topic (static_cast<const char *> (topic_msg.data ()), topic_msg.size ());
        topic_msg.close ();

        if (!has_payload) {
            ++processed;
            if (processed >= publish_ingress_drain_batch_limit
                || processed_bytes >= publish_ingress_drain_batch_bytes_limit)
                break;
            continue;
        }

        spot_owned_msg_parts_t frames;
        if (spot_io::recv_remaining_frames_to_parts (state_->pub_ingress_sub, &frames,
                                                     &processed_bytes)
            != 0) {
            if (errno == EAGAIN || errno == EINTR)
                break;
            return -1;
        }

        if (topic.empty ()
            || spot_control_protocol::is_reserved_subject (topic.data (), topic.size ())) {
            spot_clear_msg_parts (&frames);
        } else {
            const bool need_local = !state_->local_fanout.targets.empty ();
            const bool need_mesh = runtime_->mesh_pub != NULL;

            if (need_local && forward_local_fanout (runtime_, state_, topic, frames) != 0) {
                if (errno != EAGAIN
                    || stage_message (state_, topic, frames, false, true, need_mesh) != 0) {
                    spot_clear_msg_parts (&frames);
                    return -1;
                }
                spot_clear_msg_parts (&frames);
                break;
            }

            if (need_mesh && forward_mesh_pub (runtime_, state_, topic, frames) != 0) {
                if (errno != EAGAIN
                    || stage_message (state_, topic, frames, false, false, true) != 0) {
                    spot_clear_msg_parts (&frames);
                    return -1;
                }
                spot_clear_msg_parts (&frames);
                break;
            }

            spot_clear_msg_parts (&frames);
        }

        ++processed;
        if (processed >= publish_ingress_drain_batch_limit
            || processed_bytes >= publish_ingress_drain_batch_bytes_limit)
            break;
    }

    return flush_staged_messages (runtime_, state_);
}

int spot_data_plane_forwarder_t::flush_staged_messages (spot_runtime_t *runtime_,
                                                        spot_data_plane_runtime_state_t *state_)
{
    if (!runtime_ || !state_)
        return 0;

    while (!state_->staged.ingress_messages.empty ()) {
        spot_data_plane_runtime_state_t::staged_publish_entry_t &entry =
          state_->staged.ingress_messages.front ();
        if (entry.need_local) {
            if (state_->local_fanout.targets.empty ()) {
                entry.need_local = false;
            } else if (forward_local_fanout (runtime_, state_, entry.topic, entry.parts,
                                             entry.encoded_bytes)
                       != 0) {
                if (errno == EAGAIN)
                    break;
                return -1;
            } else {
                entry.need_local = false;
            }
        }
        if (entry.need_mesh) {
            if (forward_mesh_pub (runtime_, state_, entry.topic, entry.parts, entry.encoded_bytes)
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
            } else if (forward_local_fanout (runtime_, state_, entry.topic, entry.parts,
                                             entry.encoded_bytes)
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
  spot_runtime_t *runtime_, spot_data_plane_runtime_state_t *state_, socket_base_t *relay_socket_)
{
    (void) runtime_;

    if (!runtime_ || !state_)
        return 0;

    if (relay_socket_) {
        spot_data_plane_runtime_state_t::local_target_state_t *target =
          find_local_target_by_socket_local (state_, relay_socket_);
        if (!target) {
            if (state_->poller)
                refresh_fixed_poller_interest_local (state_);
            return 0;
        }

        if (flush_local_target_pending_local (state_, target) != 0)
            return -1;

        if (state_->poller) {
            refresh_fixed_poller_interest_local (state_);
            refresh_local_target_pollout_interest_local (state_, target);
        }
        return 0;
    }

    for (spot_data_plane_runtime_state_t::local_fanout_state_t::target_map_t::iterator it =
           state_->local_fanout.targets.begin ();
         it != state_->local_fanout.targets.end (); ++it) {
        spot_data_plane_runtime_state_t::local_target_state_t &target = it->second;
        if (!target.relay_socket || (relay_socket_ && target.relay_socket != relay_socket_)) {
            continue;
        }

        if (flush_local_target_pending_local (state_, &target) != 0)
            return -1;
    }

    refresh_poller_interest (state_);
    return 0;
}

int spot_data_plane_forwarder_t::flush_mesh_pub_pending (spot_runtime_t *runtime_,
                                                         spot_data_plane_runtime_state_t *state_,
                                                         socket_base_t *sender_socket_)
{
    if (!runtime_ || !state_)
        return 0;

    if (!sender_socket_ || sender_socket_ == state_->mesh_pub) {
        if (flush_mesh_broadcast_pending_local (state_) != 0)
            return -1;
    }

    for (spot_data_plane_runtime_state_t::remote_mesh_state_t::target_map_t::iterator it =
           state_->remote_mesh.targets.begin ();
         it != state_->remote_mesh.targets.end (); ++it) {
        spot_data_plane_runtime_state_t::remote_target_state_t &target = it->second;
        if (!target.sender_socket || (sender_socket_ && target.sender_socket != sender_socket_)) {
            continue;
        }

        if (flush_remote_target_pending_local (state_, &target) != 0)
            return -1;
    }

    refresh_poller_interest (state_);
    return 0;
}

}
