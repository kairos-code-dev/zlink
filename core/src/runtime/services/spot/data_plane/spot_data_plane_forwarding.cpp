/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/spot/data_plane/spot_data_plane_forwarding_internal.hpp"
#include "services/spot/data_plane/spot_data_plane_message_io_internal.hpp"
#include "services/spot/data_plane/spot_data_plane_pending_internal.hpp"
#include "services/spot/data_plane/spot_data_plane_poller_interest_internal.hpp"
#include "services/spot/data_plane/spot_data_plane_queue_admission.hpp"
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

static const size_t publish_ingress_drain_batch_limit = 2048;
static const size_t publish_ingress_drain_batch_bytes_limit = 16 * 1024 * 1024;

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

void move_ingress_messages_to_staged (
  spot_data_plane_runtime_state_t *state_,
  std::deque<spot_data_plane_pending_state_t::staged_publish_entry_t> *messages_)
{
    if (!state_ || !messages_)
        return;

    while (!messages_->empty ()) {
        state_->pending.staged.ingress_messages.push_back (std::move (messages_->front ()));
        messages_->pop_front ();
    }
    spot_data_plane_poller_interest_t::refresh_fixed (state_);
}

spot_data_plane_pending_state_t::local_target_state_t *
find_local_target_by_socket_local (spot_data_plane_runtime_state_t *state_,
                                   socket_base_t *relay_socket_)
{
    if (!state_ || !relay_socket_)
        return NULL;

    std::unordered_map<socket_base_t *, uint64_t>::iterator socket_it =
      state_->pending.local_fanout.target_by_socket.find (relay_socket_);
    if (socket_it == state_->pending.local_fanout.target_by_socket.end ())
        return NULL;

    spot_data_plane_pending_state_t::local_fanout_state_t::target_map_t::iterator target_it =
      state_->pending.local_fanout.targets.find (socket_it->second);
    if (target_it == state_->pending.local_fanout.targets.end () || !target_it->second.relay_socket)
        return NULL;

    return &target_it->second;
}

int flush_local_target_pending_local (
  spot_data_plane_runtime_state_t *state_,
  spot_data_plane_pending_state_t::local_target_state_t *target_)
{
    if (!state_ || !target_ || !target_->relay_socket)
        return 0;

    while (!target_->pending_message_ids.empty ()) {
        const uint64_t message_id = target_->pending_message_ids.front ();
        spot_data_plane_pending_state_t::local_fanout_state_t::pending_message_map_t::iterator
          msg_it = state_->pending.local_fanout.pending_messages.find (message_id);
        if (msg_it == state_->pending.local_fanout.pending_messages.end ()) {
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

    while (!state_->pending.remote_mesh.broadcast_pending_message_ids.empty ()) {
        const uint64_t message_id = state_->pending.remote_mesh.broadcast_pending_message_ids.front ();
        spot_data_plane_pending_state_t::remote_mesh_state_t::pending_message_map_t::iterator
          msg_it = state_->pending.remote_mesh.pending_messages.find (message_id);
        if (msg_it == state_->pending.remote_mesh.pending_messages.end ()) {
            state_->pending.remote_mesh.broadcast_pending_message_ids.pop_front ();
            continue;
        }

        spot_data_plane_forwarder_t::pump_socket_commands (state_->mesh_pub);
        if (spot_publish_msg_parts (state_->mesh_pub, msg_it->second.topic, msg_it->second.parts)
            != 0) {
            if (errno == EAGAIN)
                break;
            return -1;
        }

        state_->pending.remote_mesh.broadcast_pending_message_ids.pop_front ();
        spot_data_plane_pending_t::release_mesh_pending_ref (state_, message_id);
    }

    return 0;
}

int flush_remote_target_pending_local (
  spot_data_plane_runtime_state_t *state_,
  spot_data_plane_pending_state_t::remote_target_state_t *target_)
{
    if (!state_ || !target_ || !target_->sender_socket)
        return 0;

    while (!target_->pending_message_ids.empty ()) {
        const uint64_t message_id = target_->pending_message_ids.front ();
        spot_data_plane_pending_state_t::remote_mesh_state_t::pending_message_map_t::iterator
          msg_it = state_->pending.remote_mesh.pending_messages.find (message_id);
        if (msg_it == state_->pending.remote_mesh.pending_messages.end ()) {
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
    out_->reserve (part_count_ == 0 ? 1 : part_count_);
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

int flush_staged_publish_entry_local (spot_runtime_t *runtime_,
                                      spot_data_plane_runtime_state_t *state_,
                                      spot_data_plane_pending_state_t::staged_publish_entry_t *entry_,
                                      bool allow_mesh_);

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

void spot_data_plane_forwarder_t::refresh_poller_interest (spot_data_plane_runtime_state_t *state_)
{
    spot_data_plane_poller_interest_t::refresh_all (state_);
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

    if (state_->pending.local_fanout.targets.empty ())
        return 0;

    const size_t encoded_bytes = precomputed_encoded_bytes_ > 0
                                   ? precomputed_encoded_bytes_
                                   : spot_msg_parts_encoded_bytes (parts_);
    if (!spot_data_plane_pending_t::queue_has_room (state_->pending.local_fanout.pending_bytes,
                                                    state_->pending.local_fanout.pending_hard_limit,
                                                    encoded_bytes)) {
        refresh_poller_interest (state_);
        errno = EAGAIN;
        return -1;
    }

    uint64_t local_pending_message_id = 0;
    for (spot_data_plane_pending_state_t::local_fanout_state_t::target_map_t::iterator target_it =
           state_->pending.local_fanout.targets.begin ();
         target_it != state_->pending.local_fanout.targets.end ();) {
        spot_data_plane_pending_state_t::local_target_state_t &target = target_it->second;
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
    if (!spot_data_plane_pending_t::queue_has_room (state_->pending.remote_mesh.pending_bytes,
                                                    state_->pending.remote_mesh.pending_hard_limit,
                                                    encoded_bytes)) {
        refresh_poller_interest (state_);
        errno = EAGAIN;
        return -1;
    }

    pump_socket_commands (state_->mesh_pub);
    if (state_->pending.remote_mesh.broadcast_pending_message_ids.empty ()
        && spot_publish_msg_parts (state_->mesh_pub, topic_, parts_) == 0) {
        return 0;
    }

    if (errno != EAGAIN && !state_->pending.remote_mesh.broadcast_pending_message_ids.empty ())
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

    std::deque<spot_data_plane_pending_state_t::staged_publish_entry_t> &queue =
      source_mesh_ ? state_->pending.staged.mesh_messages : state_->pending.staged.ingress_messages;
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

    spot_data_plane_pending_state_t::staged_publish_entry_t entry;
    entry.topic = topic_;
    entry.need_local = true;
    entry.need_mesh = runtime_->mesh_pub != NULL;
    if (copy_raw_parts_to_owned (parts_, part_count_, &entry.parts) != 0)
        return -1;
    entry.encoded_bytes = spot_msg_parts_encoded_bytes (entry.parts);

    spot_data_plane_pending_state_t::publish_ingress_queue_t &queue =
      runtime_->execution.data_plane_state.pending.publish_ingress;
    const int slots = spot_node_pubsub_admission_hwm (runtime_->runtime_tuning_snapshot ());
    const spot_data_plane_queue_admission_plan_t plan =
      spot_data_plane_make_queue_admission_plan (
        slots, spot_data_plane_publish_message_unit_bytes (runtime_, entry.encoded_bytes));
    std::unique_lock<std::mutex> lock (queue.mutex);
    if (!spot_data_plane_publish_ingress_has_room (queue, plan, entry.encoded_bytes))
        queue.backpressure_active = true;
    const bool dontwait = (flags_ & ZLINK_DONTWAIT) != 0;
    const bool ready =
      wait_for_queue_room (queue.cv, lock, dontwait ? 0 : sndtimeo_ms_, [&queue, &plan, &entry] () {
          return queue.closed
                 || spot_data_plane_publish_ingress_has_room (queue, plan, entry.encoded_bytes);
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
    if (!spot_data_plane_publish_ingress_has_room (queue, plan, 1))
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

    std::deque<spot_data_plane_pending_state_t::staged_publish_entry_t> local;
    bool notify_recovery = false;
    {
        spot_data_plane_pending_state_t::publish_ingress_queue_t &queue = state_->pending.publish_ingress;
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
            && spot_data_plane_publish_ingress_can_resume (
              queue,
              spot_data_plane_make_queue_admission_plan (
                spot_node_pubsub_admission_hwm (runtime_->runtime_tuning_snapshot ()), 1))) {
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

    if (!state_->pending.staged.ingress_messages.empty () || !state_->pending.staged.mesh_messages.empty ()) {
        move_ingress_messages_to_staged (state_, &local);
        return flush_staged_messages (runtime_, state_);
    }

    while (!local.empty ()) {
        spot_data_plane_pending_state_t::staged_publish_entry_t &entry = local.front ();
        const int flush_rc = flush_staged_publish_entry_local (runtime_, state_, &entry, true);
        if (flush_rc > 0) {
            move_ingress_messages_to_staged (state_, &local);
            return 0;
        }
        if (flush_rc < 0) {
            spot_clear_msg_parts (&entry.parts);
            return -1;
        }

        spot_clear_msg_parts (&entry.parts);
        local.pop_front ();
    }
    return 0;
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
            const bool need_local = !state_->pending.local_fanout.targets.empty ();
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

int flush_staged_publish_entry_local (spot_runtime_t *runtime_,
                                      spot_data_plane_runtime_state_t *state_,
                                      spot_data_plane_pending_state_t::staged_publish_entry_t *entry_,
                                      bool allow_mesh_)
{
    if (!runtime_ || !state_ || !entry_)
        return 0;

    if (entry_->need_local) {
        if (state_->pending.local_fanout.targets.empty ()) {
            entry_->need_local = false;
        } else if (spot_data_plane_forwarder_t::forward_local_fanout (
                     runtime_, state_, entry_->topic, entry_->parts, entry_->encoded_bytes)
                   != 0) {
            return errno == EAGAIN ? 1 : -1;
        } else {
            entry_->need_local = false;
        }
    }

    if (allow_mesh_ && entry_->need_mesh) {
        if (spot_data_plane_forwarder_t::forward_mesh_pub (runtime_, state_, entry_->topic,
                                                           entry_->parts, entry_->encoded_bytes)
            != 0) {
            return errno == EAGAIN ? 1 : -1;
        }
        entry_->need_mesh = false;
    }

    return 0;
}

int spot_data_plane_forwarder_t::flush_staged_messages (spot_runtime_t *runtime_,
                                                        spot_data_plane_runtime_state_t *state_)
{
    if (!runtime_ || !state_)
        return 0;

    while (!state_->pending.staged.ingress_messages.empty ()) {
        spot_data_plane_pending_state_t::staged_publish_entry_t &entry =
          state_->pending.staged.ingress_messages.front ();
        const int flush_rc = flush_staged_publish_entry_local (runtime_, state_, &entry, true);
        if (flush_rc > 0)
            break;
        if (flush_rc < 0)
            return -1;
        if (entry.need_local || entry.need_mesh)
            break;
        spot_clear_msg_parts (&entry.parts);
        state_->pending.staged.ingress_messages.pop_front ();
    }

    while (!state_->pending.staged.mesh_messages.empty ()) {
        spot_data_plane_pending_state_t::staged_publish_entry_t &entry =
          state_->pending.staged.mesh_messages.front ();
        const int flush_rc = flush_staged_publish_entry_local (runtime_, state_, &entry, false);
        if (flush_rc > 0)
            break;
        if (flush_rc < 0)
            return -1;
        if (entry.need_local)
            break;
        spot_clear_msg_parts (&entry.parts);
        state_->pending.staged.mesh_messages.pop_front ();
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
        spot_data_plane_pending_state_t::local_target_state_t *target =
          find_local_target_by_socket_local (state_, relay_socket_);
        if (!target) {
            if (state_->poller)
                spot_data_plane_poller_interest_t::refresh_fixed (state_);
            return 0;
        }

        if (flush_local_target_pending_local (state_, target) != 0)
            return -1;

        if (state_->poller) {
            spot_data_plane_poller_interest_t::refresh_fixed (state_);
            spot_data_plane_poller_interest_t::refresh_local_target (state_, target);
        }
        return 0;
    }

    for (spot_data_plane_pending_state_t::local_fanout_state_t::target_map_t::iterator it =
           state_->pending.local_fanout.targets.begin ();
         it != state_->pending.local_fanout.targets.end (); ++it) {
        spot_data_plane_pending_state_t::local_target_state_t &target = it->second;
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

    for (spot_data_plane_pending_state_t::remote_mesh_state_t::target_map_t::iterator it =
           state_->pending.remote_mesh.targets.begin ();
         it != state_->pending.remote_mesh.targets.end (); ++it) {
        spot_data_plane_pending_state_t::remote_target_state_t &target = it->second;
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
