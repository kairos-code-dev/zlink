/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include <vector>

#include "api/socket/request_reply_protocol_internal.hpp"
#include "api/socket/socket_request_reply_internal.hpp"
#include "sockets/common/socket_base.hpp"
#include "utils/routing_id.hpp"

namespace zlink
{
namespace socket_reqrep_internal
{
router_recv_metadata_tls_t &router_recv_metadata_tls ()
{
    static thread_local router_recv_metadata_tls_t metadata;
    return metadata;
}

namespace
{
void complete_reply_from_transport (
  socket_request_reply_state_t *state_,
  const zlink_routing_id_t *source_rid_,
  zlink_msg_t *parts_,
  size_t part_count_)
{
    zlink::request_reply::parsed_envelope_t envelope;
    if (!state_ || !zlink::request_reply::parse_envelope (parts_, part_count_, &envelope)
        || envelope.message_type == zlink::request_reply::request_type) {
        zlink::request_reply::close_request_reply_parts (parts_, part_count_);
        return;
    }

    pending_key_t key;
    key.request_seq = envelope.request_seq;
    if (state_->socket_type == ZLINK_CORE_SOCKET_ROUTER
        && zlink::valid_routing_id (source_rid_))
        key.peer_rid = zlink::routing_id_key (source_rid_);

    pending_request_t pending;
    {
        std::lock_guard<std::mutex> lock (state_->mutex);
        if (!remove_socket_pending_request_locked (state_, key, true, &pending)) {
            zlink::request_reply::close_request_reply_parts (parts_, part_count_);
            return;
        }
    }
    zlink::request_timeout::cancel (pending.timeout_task);

    int callback_errno = 0;
    zlink_msg_t *callback_parts = envelope.payload_parts;
    size_t callback_part_count = envelope.payload_part_count;
    if (zlink::request_reply::decode_reply_completion (
          envelope.message_type, envelope.payload_parts, envelope.payload_part_count,
          &callback_errno, &callback_parts, &callback_part_count)
        != 0) {
        callback_errno = EPROTO;
        callback_parts = NULL;
        callback_part_count = 0;
    }
    zlink::request_completion::claim_owner_thread (&state_->completion);
    zlink::request_completion::invoke_callback (
      state_->socket, pending.handler, callback_errno, callback_parts,
      callback_part_count, pending.userdata);
    state_->socket->notify_request_completion ();
    zlink::request_reply::close_request_reply_parts (parts_, part_count_);
}

}

void process_completion_pipe (zlink::socket_base_t *socket_, zlink::pipe_t *pipe_)
{
    if (!socket_ || !pipe_)
        return;

    std::shared_ptr<socket_request_reply_state_t> state = socket_->request_reply_state ();
    while (true) {
        std::vector<zlink_msg_t> parts;
        bool complete = false;
        while (!complete) {
            zlink::msg_t frame;
            const int init_rc = frame.init ();
            errno_assert (init_rc == 0);
            if (!pipe_->read (&frame)) {
                const int close_rc = frame.close ();
                errno_assert (close_rc == 0);
                zlink::request_reply::close_built_parts (&parts);
                return;
            }

            parts.push_back (zlink_msg_t ());
            zlink_msg_init (&parts.back ());
            zlink::msg_t *stored = reinterpret_cast<zlink::msg_t *> (&parts.back ());
            const bool more = (frame.flags () & zlink::msg_t::more) != 0;
            const int move_rc = stored->move (frame);
            errno_assert (move_rc == 0);
            complete = !more;
        }

        if (!state) {
            zlink::request_reply::close_built_parts (&parts);
            continue;
        }

        zlink_routing_id_t source_rid;
        memset (&source_rid, 0, sizeof (source_rid));
        const blob_t &rid = pipe_->get_routing_id ();
        if (rid.size () > 0 && rid.size () <= sizeof (source_rid.data)) {
            source_rid.size = static_cast<uint8_t> (rid.size ());
            memcpy (source_rid.data, rid.data (), rid.size ());
        }
        complete_reply_from_transport (
          state.get (), source_rid.size > 0 ? &source_rid : NULL, &parts[0], parts.size ());
    }
}

int ensure_internal_dispatch_installed (const std::shared_ptr<socket_request_reply_state_t> &state_)
{
    if (!state_ || !state_->socket) {
        errno = EFAULT;
        return -1;
    }
    std::lock_guard<std::mutex> lock (state_->mutex);
    if (state_->closing) {
        errno = ETERM;
        return -1;
    }
    // Application messages remain in the transport pipe until a public
    // receive operation consumes them. Completion traffic has its own pipe,
    // so no socket-wide dispatcher or payload queue is required.
    state_->internal_dispatch_installed = false;
    errno = 0;
    return 0;
}

bool has_pending_request_work (const std::shared_ptr<socket_request_reply_state_t> &state_)
{
    if (!state_)
        return false;
    if (has_pending_reply_completions (state_))
        return true;

    std::lock_guard<std::mutex> lock (state_->mutex);
    return !state_->pending_requests.empty ();
}

void fail_disconnected_peer_requests (
  const std::shared_ptr<socket_request_reply_state_t> &state_,
  const unsigned char *routing_id_,
  size_t routing_id_size_,
  int errnum_)
{
    if (!state_)
        return;

    const std::string peer_key (
      routing_id_ && routing_id_size_ > 0
        ? reinterpret_cast<const char *> (routing_id_)
        : "",
      routing_id_ && routing_id_size_ > 0 ? routing_id_size_ : 0);
    std::vector<pending_request_t> failed;
    {
        std::lock_guard<std::mutex> lock (state_->mutex);
        for (std::unordered_map<pending_key_t, pending_request_t,
                                pending_key_hash_t>::iterator it =
               state_->pending_requests.begin ();
             it != state_->pending_requests.end ();) {
            const bool matches =
              state_->socket_type == ZLINK_CORE_SOCKET_DEALER
              || it->first.peer_rid == peer_key;
            if (!matches) {
                ++it;
                continue;
            }
            failed.push_back (it->second);
            state_->pending_sequences.erase (it->first.request_seq);
            state_->pending_request_keys_by_seq.erase (it->first.request_seq);
            it = state_->pending_requests.erase (it);
        }
    }

    for (size_t i = 0; i < failed.size (); ++i) {
        zlink::request_timeout::cancel (failed[i].timeout_task);
        (void) queue_reply_completion (
          state_, failed[i].handler, failed[i].userdata, errnum_, NULL, 0);
    }
}

int drain_close_request_reply_socket (socket_handle_t handle_)
{
    if (!handle_.socket) {
        errno = EFAULT;
        return -1;
    }

    std::shared_ptr<socket_request_reply_state_t> state = handle_.socket->request_reply_state ();
    if (!state)
        return 0;

    bool stop_dispatch = false;
    {
        std::unique_lock<std::mutex> lock (state->mutex);
        state->closing = true;
        while (state->internal_dispatch_installing)
            state->internal_dispatch_cv.wait (lock);
        stop_dispatch = state->internal_dispatch_installed;
    }
    if (stop_dispatch && handle_.socket->socket_msg_dispatch_active ()
        && zlink::socket_base_t::current_socket_msg_dispatch_socket () != handle_.socket) {
        (void) handle_.socket->socket_msg_dispatch_stop ();
    }

    std::vector<pending_request_t> pending;
    {
        std::lock_guard<std::mutex> lock (state->mutex);
        for (std::unordered_map<pending_key_t, pending_request_t, pending_key_hash_t>::iterator it =
               state->pending_requests.begin ();
             it != state->pending_requests.end (); ++it) {
            pending.push_back (it->second);
        }
        state->pending_requests.clear ();
        state->pending_request_keys_by_seq.clear ();
        state->pending_sequences.clear ();
        state->dealer_reply_targets.clear ();
        state->router_reply_targets.clear ();
    }

    for (size_t i = 0; i < pending.size (); ++i) {
        zlink::request_timeout::cancel (pending[i].timeout_task);
        if (queue_reply_completion (state, pending[i].handler, pending[i].userdata, ETERM, NULL, 0)
            != 0) {
            return -1;
        }
    }

    return drain_reply_completions (state, handle_.socket);
}

void cleanup_request_reply_socket (socket_handle_t handle_)
{
    if (!handle_.socket)
        return;

    std::vector<std::shared_ptr<zlink::request_timeout::task_t>> timeout_tasks;
    std::shared_ptr<socket_request_reply_state_t> state = handle_.socket->request_reply_state ();
    if (state) {
        {
            std::lock_guard<std::mutex> state_lock (state->mutex);
            state->internal_dispatch_installed = false;
            state->internal_dispatch_installing = false;
            state->closing = true;
            for (std::unordered_map<pending_key_t, pending_request_t,
                                    pending_key_hash_t>::iterator it =
                   state->pending_requests.begin ();
                 it != state->pending_requests.end (); ++it) {
                timeout_tasks.push_back (it->second.timeout_task);
            }
            state->pending_requests.clear ();
            state->pending_request_keys_by_seq.clear ();
            state->pending_sequences.clear ();
            state->dealer_reply_targets.clear ();
            state->router_reply_targets.clear ();
            zlink::request_completion::close (&state->completion);
        }
        state->internal_dispatch_cv.notify_all ();
    }
    for (size_t i = 0; i < timeout_tasks.size (); ++i)
        zlink::request_timeout::cancel (timeout_tasks[i]);
    handle_.socket->clear_request_reply_state ();
}
}
}
