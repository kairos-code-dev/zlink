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
void socket_request_reply_dispatch (const zlink_routing_id_t *source_rid_,
                                    zlink_msg_t *parts_,
                                    size_t part_count_,
                                    void *userdata_)
{
    socket_request_reply_state_t *state = static_cast<socket_request_reply_state_t *> (userdata_);
    if (!state) {
        zlink::request_reply::close_request_reply_parts (parts_, part_count_);
        return;
    }

    zlink::request_reply::parsed_envelope_t envelope;
    if (!zlink::request_reply::parse_envelope (parts_, part_count_, &envelope)) {
        if (state->socket_type == ZLINK_CORE_SOCKET_DEALER) {
            if (dispatch_dealer_message (state, ZLINK_DEALER_MESSAGE_RAW, 0, NULL, parts_,
                                         part_count_)
                != 0)
                zlink::request_reply::close_request_reply_parts (parts_, part_count_);
        } else {
            if (dispatch_router_message (state, source_rid_, 0, parts_, part_count_) != 0)
                zlink::request_reply::close_request_reply_parts (parts_, part_count_);
        }
        return;
    }

    if (envelope.message_type == zlink::request_reply::request_type) {
        if (state->socket_type == ZLINK_CORE_SOCKET_DEALER) {
            if (dispatch_dealer_message (state, ZLINK_DEALER_MESSAGE_REQUEST, envelope.request_seq,
                                         zlink::socket_base_t::current_socket_msg_dispatch_pipe (),
                                         envelope.payload_parts, envelope.payload_part_count)
                != 0) {
                zlink::request_reply::close_request_reply_parts (parts_, part_count_);
            }
        } else if (state->socket_type == ZLINK_CORE_SOCKET_ROUTER
                   && zlink::valid_routing_id (source_rid_)) {
            if (dispatch_router_message (state, source_rid_, envelope.request_seq,
                                         envelope.payload_parts, envelope.payload_part_count)
                != 0) {
                zlink::request_reply::close_request_reply_parts (parts_, part_count_);
            }
        } else {
            zlink::request_reply::close_request_reply_parts (parts_, part_count_);
        }
        return;
    }

    pending_key_t key;
    key.request_seq = envelope.request_seq;
    if (state->socket_type == ZLINK_CORE_SOCKET_ROUTER && zlink::valid_routing_id (source_rid_)) {
        key.peer_rid = zlink::routing_id_key (source_rid_);
    }

    pending_request_t pending;
    {
        std::lock_guard<std::mutex> lock (state->mutex);
        if (!remove_socket_pending_request_locked (state, key, true, &pending)) {
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
        std::shared_ptr<socket_request_reply_state_t> state_ref (
          state, [] (socket_request_reply_state_t *) {});
        (void) queue_reply_completion (state_ref, pending.handler, pending.userdata, EPROTO, NULL,
                                       0);
        zlink::request_reply::close_request_reply_parts (parts_, part_count_);
        return;
    }

    std::shared_ptr<socket_request_reply_state_t> state_ref (
      state, [] (socket_request_reply_state_t *) {});
    (void) queue_reply_completion (state_ref, pending.handler, pending.userdata, callback_errno,
                                   callback_parts, callback_part_count);
    zlink::request_reply::close_request_reply_parts (parts_, part_count_);
}
}

int ensure_internal_dispatch_installed (const std::shared_ptr<socket_request_reply_state_t> &state_)
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

    if (state_->socket->socket_set_msg_handler_with_userdata (&socket_request_reply_dispatch, NULL,
                                                              state_.get ())
        != 0)
        return -1;

    state_->internal_dispatch_installed = true;
    return 0;
}

int ensure_recv_queue_ready (const std::shared_ptr<socket_request_reply_state_t> &state_)
{
    if (!state_ || !state_->socket) {
        errno = EFAULT;
        return -1;
    }

    std::lock_guard<std::mutex> lock (state_->mutex);
    return zlink::internal_pair_queue::ensure (state_->socket->get_ctx (),
                                               "zlink.router.reqrep.recv", &state_->recv_queue);
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

int drain_close_request_reply_socket (socket_handle_t handle_)
{
    if (!handle_.socket) {
        errno = EFAULT;
        return -1;
    }

    std::shared_ptr<socket_request_reply_state_t> state = handle_.socket->request_reply_state ();
    if (!state)
        return 0;

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
    bool close_recv_queue = false;
    std::shared_ptr<socket_request_reply_state_t> state = handle_.socket->request_reply_state ();
    if (state && state->internal_dispatch_installed
        && handle_.socket->socket_msg_dispatch_active ()) {
        (void) handle_.socket->socket_msg_dispatch_stop ();
    }
    if (state) {
        std::lock_guard<std::mutex> state_lock (state->mutex);
        state->internal_dispatch_installed = false;
        for (std::unordered_map<pending_key_t, pending_request_t, pending_key_hash_t>::iterator it =
               state->pending_requests.begin ();
             it != state->pending_requests.end (); ++it) {
            timeout_tasks.push_back (it->second.timeout_task);
        }
        state->pending_requests.clear ();
        state->pending_request_keys_by_seq.clear ();
        state->pending_sequences.clear ();
        state->dealer_reply_targets.clear ();
        close_recv_queue = true;
        zlink::request_completion::close (&state->completion);
    }
    if (state && close_recv_queue)
        zlink::internal_pair_queue::close_and_wait (&state->recv_queue);
    for (size_t i = 0; i < timeout_tasks.size (); ++i)
        zlink::request_timeout::cancel (timeout_tasks[i]);
    handle_.socket->clear_request_reply_state ();
}
}
}
