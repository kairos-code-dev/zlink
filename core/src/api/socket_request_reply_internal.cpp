/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include <memory>
#include <new>

#include "api/request_reply_protocol_internal.hpp"
#include "api/socket_request_reply_internal.hpp"

namespace zlink
{
namespace socket_reqrep_internal
{
bool pending_key_t::operator< (const pending_key_t &other_) const
{
    if (request_seq != other_.request_seq)
        return request_seq < other_.request_seq;
    return peer_rid < other_.peer_rid;
}

socket_request_reply_state_t::socket_request_reply_state_t (
  zlink::socket_base_t *socket_,
  int socket_type_) :
    socket (socket_),
    socket_type (socket_type_),
    default_timeout_ms (zlink::request_reply::default_timeout_ms),
    next_request_seq (1),
    internal_dispatch_installed (false),
    router_handler (NULL),
    router_handler_userdata (NULL)
{
}

namespace
{
struct socket_timeout_callback_ctx_t
{
    std::shared_ptr<socket_request_reply_state_t> state;
    pending_key_t key;
};

uint64_t allocate_request_seq (socket_request_reply_state_t *state_)
{
    if (!state_) {
        errno = EFAULT;
        return 0;
    }

    const uint64_t start =
      state_->next_request_seq == 0 ? 1 : state_->next_request_seq;
    uint64_t candidate = start;

    do {
        if (candidate == 0)
            candidate = 1;

        if (state_->pending_sequences.count (candidate) == 0) {
            uint64_t next = candidate + 1;
            if (next == 0)
                next = 1;
            state_->next_request_seq = next;
            return candidate;
        }

        ++candidate;
        if (candidate == 0)
            candidate = 1;
    } while (candidate != start);

    errno = EBUSY;
    return 0;
}

void on_socket_request_timeout (void *userdata_)
{
    std::unique_ptr<socket_timeout_callback_ctx_t> ctx (
      static_cast<socket_timeout_callback_ctx_t *> (userdata_));
    if (!ctx.get () || !ctx->state)
        return;

    pending_request_t pending;
    bool found = false;
    {
        std::lock_guard<std::mutex> lock (ctx->state->mutex);
        std::map<pending_key_t, pending_request_t>::iterator it =
          ctx->state->pending_requests.find (ctx->key);
        if (it == ctx->state->pending_requests.end ())
            return;
        pending = it->second;
        ctx->state->pending_sequences.erase (ctx->key.request_seq);
        ctx->state->pending_requests.erase (it);
        found = true;
    }

    if (found)
        zlink::request_reply::complete_reply_callback (
          pending.handler, ETIMEDOUT, NULL, 0, pending.userdata);
}
}

std::shared_ptr<socket_request_reply_state_t>
find_or_create_request_reply_state (socket_handle_t handle_)
{
    std::shared_ptr<socket_request_reply_state_t> state =
      handle_.socket ? std::static_pointer_cast<socket_request_reply_state_t> (
                        handle_.socket->request_reply_state ())
                     : std::shared_ptr<socket_request_reply_state_t> ();
    if (state)
        return state;

    state.reset (
      new socket_request_reply_state_t (handle_.socket, socket_type (handle_)));
    handle_.socket->set_request_reply_state (state);
    return state;
}

std::shared_ptr<socket_request_reply_state_t>
find_request_reply_state (socket_handle_t handle_)
{
    return handle_.socket
             ? std::static_pointer_cast<socket_request_reply_state_t> (
                 handle_.socket->request_reply_state ())
             : std::shared_ptr<socket_request_reply_state_t> ();
}

int start_request (socket_handle_t handle_,
                   const zlink_routing_id_t *peer_rid_,
                   zlink_msg_t *parts_,
                   size_t part_count_,
                   uint32_t timeout_ms_,
                   zlink_reply_handler_fn handler_,
                   void *userdata_)
{
    std::shared_ptr<socket_request_reply_state_t> state =
      find_or_create_request_reply_state (handle_);
    if (ensure_internal_dispatch_installed (state) != 0)
        return -1;

    pending_key_t key;
    pending_request_t pending;
    uint32_t resolved_timeout_ms = zlink::request_reply::default_timeout_ms;
    {
        std::lock_guard<std::mutex> lock (state->mutex);
        const uint64_t request_seq = allocate_request_seq (state.get ());
        if (request_seq == 0)
            return -1;

        key.request_seq = request_seq;
        if (handle_.socket->socket_type () == ZLINK_CORE_SOCKET_ROUTER
            && has_valid_routing_id (peer_rid_)) {
            key.peer_rid = routing_id_key (peer_rid_);
        }

        pending.key = key;
        pending.handler = handler_;
        pending.userdata = userdata_;
        resolved_timeout_ms = zlink::request_reply::resolve_timeout_ms (
          timeout_ms_, state->default_timeout_ms);
        std::unique_ptr<socket_timeout_callback_ctx_t> timeout_ctx (
          new (std::nothrow) socket_timeout_callback_ctx_t ());
        if (!timeout_ctx.get ()) {
            errno = ENOMEM;
            return -1;
        }
        timeout_ctx->state = state;
        timeout_ctx->key = key;
        pending.timeout_task =
          zlink::request_timeout::schedule (resolved_timeout_ms,
                                            &on_socket_request_timeout,
                                            timeout_ctx.release ());
        if (!pending.timeout_task) {
            errno = ENOMEM;
            return -1;
        }
        state->pending_sequences.insert (request_seq);
        state->pending_requests[key] = pending;
    }

    const uint8_t message_type = zlink::request_reply::request_type;
    const int rc =
      send_request_reply_message (handle_.socket, peer_rid_, parts_, part_count_,
                                  message_type, key.request_seq);
    if (rc != 0) {
        std::lock_guard<std::mutex> lock (state->mutex);
        zlink::request_timeout::cancel (pending.timeout_task);
        state->pending_sequences.erase (key.request_seq);
        state->pending_requests.erase (key);
        return -1;
    }
    return 0;
}
}
}
