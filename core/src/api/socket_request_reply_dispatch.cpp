/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include <vector>

#include "api/request_reply_protocol_internal.hpp"
#include "api/socket_request_reply_internal.hpp"
#include "sockets/socket_base.hpp"

namespace zlink
{
namespace socket_reqrep_internal
{
thread_local zlink_routing_id_t g_router_recv_source_rid;
thread_local zlink_routing_id_t g_router_recv_source_spot_rid;

namespace
{
void socket_request_reply_dispatch (const zlink_routing_id_t *source_rid_,
                                    zlink_msg_t *parts_,
                                    size_t part_count_,
                                    void *userdata_)
{
    socket_request_reply_state_t *state =
      static_cast<socket_request_reply_state_t *> (userdata_);
    if (!state) {
        zlink::request_reply::close_request_reply_parts (parts_, part_count_);
        return;
    }

    zlink::request_reply::parsed_envelope_t envelope;
    if (!zlink::request_reply::parse_envelope (parts_, part_count_, &envelope)) {
        if (dispatch_router_message (state, source_rid_, NULL, 0, parts_,
                                     part_count_)
            != 0)
            zlink::request_reply::close_request_reply_parts (parts_, part_count_);
        return;
    }

    if (envelope.message_type == zlink::request_reply::request_type) {
        if (state->socket_type == ZLINK_CORE_SOCKET_ROUTER
            && has_valid_routing_id (source_rid_)) {
            if (dispatch_router_message (
                  state, source_rid_, NULL, envelope.request_seq,
                  envelope.payload_parts, envelope.payload_part_count)
                != 0) {
                zlink::request_reply::close_request_reply_parts (parts_,
                                                                 part_count_);
            }
        } else {
            zlink::request_reply::close_request_reply_parts (parts_, part_count_);
        }
        return;
    }

    pending_key_t key;
    key.request_seq = envelope.request_seq;
    if (state->socket_type == ZLINK_CORE_SOCKET_ROUTER
        && has_valid_routing_id (source_rid_)) {
        key.peer_rid = routing_id_key (source_rid_);
    }

    pending_request_t pending;
    bool found = false;
    {
        std::lock_guard<std::mutex> lock (state->mutex);
        std::unordered_map<pending_key_t,
                           pending_request_t,
                           pending_key_hash_t>::iterator it =
          state->pending_requests.find (key);
        if (it == state->pending_requests.end ()) {
            std::unordered_map<uint64_t, pending_key_t>::iterator seq_it =
              state->pending_request_keys_by_seq.find (key.request_seq);
            if (seq_it != state->pending_request_keys_by_seq.end ())
                it = state->pending_requests.find (seq_it->second);
        }
        if (it != state->pending_requests.end ()) {
            pending = it->second;
            state->pending_sequences.erase (it->first.request_seq);
            state->pending_request_keys_by_seq.erase (it->first.request_seq);
            state->pending_requests.erase (it);
            found = true;
        }
    }

    zlink::request_timeout::cancel (pending.timeout_task);

    if (!found) {
        if (state->socket_type == ZLINK_CORE_SOCKET_ROUTER
            && has_valid_routing_id (source_rid_)) {
            if (dispatch_router_message (
                  state, source_rid_, NULL, envelope.request_seq,
                  envelope.payload_parts, envelope.payload_part_count)
                != 0) {
                zlink::request_reply::close_request_reply_parts (parts_,
                                                                 part_count_);
            }
        } else {
            zlink::request_reply::close_request_reply_parts (parts_, part_count_);
        }
        return;
    }

    int callback_errno = 0;
    zlink_msg_t *callback_parts = envelope.payload_parts;
    size_t callback_part_count = envelope.payload_part_count;
    if (zlink::request_reply::decode_reply_completion (
          envelope.message_type, envelope.payload_parts,
          envelope.payload_part_count, &callback_errno, &callback_parts,
          &callback_part_count)
        != 0) {
        std::shared_ptr<socket_request_reply_state_t> state_ref (
          state, [] (socket_request_reply_state_t *) {});
        (void) queue_reply_completion (
          state_ref, pending.handler, pending.userdata, EPROTO, NULL, 0);
        zlink::request_reply::close_request_reply_parts (parts_, part_count_);
        return;
    }

    std::shared_ptr<socket_request_reply_state_t> state_ref (
      state, [] (socket_request_reply_state_t *) {});
    (void) queue_reply_completion (
      state_ref, pending.handler, pending.userdata, callback_errno,
      callback_parts, callback_part_count);
    zlink::request_reply::close_request_reply_parts (parts_, part_count_);
}
}

int ensure_internal_dispatch_installed (
  const std::shared_ptr<socket_request_reply_state_t> &state_)
{
    if (!state_ || !state_->socket) {
        errno = EFAULT;
        return -1;
    }

    std::lock_guard<std::mutex> lock (state_->mutex);
    if (state_->internal_dispatch_installed)
        return 0;

    if (state_->socket->socket_msg_dispatch_active ()) {
        errno = EBUSY;
        return -1;
    }

    if (state_->socket->socket_set_msg_handler_with_userdata (
          &socket_request_reply_dispatch, NULL, state_.get ())
        != 0)
        return -1;

    state_->internal_dispatch_installed = true;
    return 0;
}

int ensure_recv_queue_ready (
  const std::shared_ptr<socket_request_reply_state_t> &state_)
{
    if (!state_ || !state_->socket) {
        errno = EFAULT;
        return -1;
    }

    std::lock_guard<std::mutex> lock (state_->mutex);
    return zlink::internal_pair_queue::ensure (
      state_->socket->get_ctx (), "zlink.router.reqrep.recv",
      &state_->recv_queue);
}

bool has_pending_request_work (
  const std::shared_ptr<socket_request_reply_state_t> &state_)
{
    if (!state_)
        return false;
    if (has_pending_reply_completions (state_))
        return true;

    std::lock_guard<std::mutex> lock (state_->mutex);
    return !state_->pending_requests.empty ();
}

int drain_close_request_reply_socket (socket_handle_t handle_)
{
    if (!handle_.socket) {
        errno = EFAULT;
        return -1;
    }

    std::shared_ptr<socket_request_reply_state_t> state =
      handle_.socket->request_reply_state ();
    if (!state)
        return 0;

    std::vector<pending_request_t> pending;
    {
        std::lock_guard<std::mutex> lock (state->mutex);
        for (std::unordered_map<pending_key_t,
                                pending_request_t,
                                pending_key_hash_t>::iterator it =
               state->pending_requests.begin ();
             it != state->pending_requests.end (); ++it) {
            pending.push_back (it->second);
        }
        state->pending_requests.clear ();
        state->pending_request_keys_by_seq.clear ();
        state->pending_sequences.clear ();
    }

    for (size_t i = 0; i < pending.size (); ++i) {
        zlink::request_timeout::cancel (pending[i].timeout_task);
        if (queue_reply_completion (state, pending[i].handler,
                                    pending[i].userdata, ETERM, NULL, 0)
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

    zlink::socket_base_t *queue_rx = NULL;
    zlink::socket_base_t *queue_tx = NULL;
    std::vector<std::shared_ptr<zlink::request_timeout::task_t> > timeout_tasks;
    std::shared_ptr<socket_request_reply_state_t> state =
      handle_.socket->request_reply_state ();
    if (state && state->internal_dispatch_installed
        && handle_.socket->socket_msg_dispatch_active ()) {
        (void) handle_.socket->socket_msg_dispatch_stop ();
    }
    if (state) {
        std::lock_guard<std::mutex> state_lock (state->mutex);
        state->internal_dispatch_installed = false;
        queue_rx = state->recv_queue.rx;
        queue_tx = state->recv_queue.tx;
        for (std::unordered_map<pending_key_t,
                                pending_request_t,
                                pending_key_hash_t>::iterator it =
               state->pending_requests.begin ();
             it != state->pending_requests.end (); ++it) {
            timeout_tasks.push_back (it->second.timeout_task);
        }
        state->pending_requests.clear ();
        state->pending_request_keys_by_seq.clear ();
        state->pending_sequences.clear ();
        zlink::internal_pair_queue::close (&state->recv_queue);
        zlink::request_completion::close (&state->completion);
    }
    for (size_t i = 0; i < timeout_tasks.size (); ++i)
        zlink::request_timeout::cancel (timeout_tasks[i]);
    zlink::ctx_t *ctx = handle_.socket->get_ctx ();
    if (ctx && queue_tx)
        (void) ctx->wait_for_socket_removal (queue_tx, 1000);
    if (ctx && queue_rx)
        (void) ctx->wait_for_socket_removal (queue_rx, 1000);
    handle_.socket->clear_request_reply_state ();
}
}
}
