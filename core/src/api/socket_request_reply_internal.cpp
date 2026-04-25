/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include <memory>
#include <new>

#include "api/request_completion_queue_internal.hpp"
#include "api/request_reply_protocol_internal.hpp"
#include "api/service_api_internal.hpp"
#include "api/socket_request_reply_internal.hpp"

namespace zlink
{
namespace socket_reqrep_internal
{
namespace
{
size_t hash_combine (size_t seed_, size_t value_)
{
    return seed_ ^ (value_ + 0x9e3779b97f4a7c15ULL + (seed_ << 6)
                    + (seed_ >> 2));
}
}

bool pending_key_t::operator== (const pending_key_t &other_) const
{
    return request_seq == other_.request_seq && peer_rid == other_.peer_rid;
}

bool pending_key_t::operator< (const pending_key_t &other_) const
{
    if (request_seq != other_.request_seq)
        return request_seq < other_.request_seq;
    return peer_rid < other_.peer_rid;
}

size_t pending_key_hash_t::operator() (const pending_key_t &key_) const
{
    size_t seed = std::hash<uint64_t> () (key_.request_seq);
    return hash_combine (seed, std::hash<std::string> () (key_.peer_rid));
}

socket_request_reply_state_t::socket_request_reply_state_t (
  zlink::socket_base_t *socket_,
  int socket_type_) :
    socket (socket_),
    socket_type (socket_type_),
    internal_dispatch_installed (false)
{
}

int ensure_completion_queue_ready (
  const std::shared_ptr<socket_request_reply_state_t> &state_)
{
    if (!state_ || !state_->socket || !state_->socket->get_ctx ()) {
        errno = EFAULT;
        return -1;
    }

    return zlink::request_completion::ensure_signal_ready (
      &state_->completion, state_->socket->get_ctx (),
      "zlink.router.reqrep.completion");
}

int queue_reply_completion (
  const std::shared_ptr<socket_request_reply_state_t> &state_,
  zlink_reply_handler_fn handler_,
  void *userdata_,
  int errnum_,
  zlink_msg_t *parts_,
  size_t part_count_)
{
    if (!state_ || !state_->socket || !state_->socket->get_ctx ()) {
        errno = EFAULT;
        return -1;
    }

    std::vector<void *> observers;
    {
        std::lock_guard<std::mutex> lock (state_->mutex);
        observers.assign (state_->spot_channel_dispatch_observers.begin (),
                          state_->spot_channel_dispatch_observers.end ());
    }

    const int rc = zlink::request_completion::enqueue (
      &state_->completion, state_->socket->get_ctx (),
      "zlink.router.reqrep.completion", handler_, userdata_, errnum_, parts_,
      part_count_);
    if (rc != 0)
        return rc;

    for (size_t i = 0; i < observers.size (); ++i) {
        zlink_spot_notify_dispatch_info (
          observers[i], ZLINK_SPOT_DISPATCH_EVENT_CHANNEL_REPLY_READABLE,
          ZLINK_SPOT_DISPATCH_SUBJECT_CHANNEL_DEALER, state_->socket);
    }
    return 0;
}

int drain_reply_completions (
  const std::shared_ptr<socket_request_reply_state_t> &state_,
  void *owner_handle_)
{
    if (!state_) {
        errno = EFAULT;
        return -1;
    }
    return zlink::request_completion::drain (&state_->completion, owner_handle_);
}

bool has_pending_reply_completions (
  const std::shared_ptr<socket_request_reply_state_t> &state_)
{
    return state_
             ? zlink::request_completion::has_pending (&state_->completion)
             : false;
}

zlink::socket_base_t *completion_signal_socket (
  const std::shared_ptr<socket_request_reply_state_t> &state_)
{
    return state_
             ? zlink::request_completion::signal_socket (&state_->completion)
             : NULL;
}

void claim_completion_owner (
  const std::shared_ptr<socket_request_reply_state_t> &state_)
{
    if (!state_)
        return;
    zlink::request_completion::claim_owner_thread (&state_->completion);
}

bool current_thread_is_completion_owner (
  const std::shared_ptr<socket_request_reply_state_t> &state_)
{
    return state_
             ? zlink::request_completion::current_thread_is_owner (
                 &state_->completion)
             : false;
}

bool in_socket_request_completion_callback (void *socket_)
{
    return zlink::request_completion::in_request_completion_callback (socket_);
}

void register_spot_channel_dispatch_observer (
  const std::shared_ptr<socket_request_reply_state_t> &state_,
  void *spot_)
{
    if (!state_ || !spot_)
        return;

    std::lock_guard<std::mutex> lock (state_->mutex);
    state_->spot_channel_dispatch_observers.insert (spot_);
}

void unregister_spot_channel_dispatch_observer (
  const std::shared_ptr<socket_request_reply_state_t> &state_,
  void *spot_)
{
    if (!state_ || !spot_)
        return;

    std::lock_guard<std::mutex> lock (state_->mutex);
    state_->spot_channel_dispatch_observers.erase (spot_);
}

namespace
{
struct socket_timeout_callback_ctx_t
{
    std::shared_ptr<socket_request_reply_state_t> state;
    pending_key_t key;
};

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
        std::unordered_map<pending_key_t,
                           pending_request_t,
                           pending_key_hash_t>::iterator it =
          ctx->state->pending_requests.find (ctx->key);
        if (it == ctx->state->pending_requests.end ())
            return;
        pending = it->second;
        ctx->state->pending_sequences.erase (ctx->key.request_seq);
        ctx->state->pending_request_keys_by_seq.erase (ctx->key.request_seq);
        ctx->state->pending_requests.erase (it);
        found = true;
    }

    if (found)
        (void) queue_reply_completion (ctx->state, pending.handler,
                                       pending.userdata, ETIMEDOUT, NULL, 0);
}
}

std::shared_ptr<socket_request_reply_state_t>
find_or_create_request_reply_state (socket_handle_t handle_)
{
    std::shared_ptr<socket_request_reply_state_t> state =
      handle_.socket ? handle_.socket->request_reply_state ()
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
    return handle_.socket ? handle_.socket->request_reply_state ()
                          : std::shared_ptr<socket_request_reply_state_t> ();
}

int start_request (socket_handle_t handle_,
                   const zlink_routing_id_t *peer_rid_,
                   zlink_msg_t *parts_,
                   size_t part_count_,
                   zlink_send_flags_t flags_,
                   uint32_t timeout_ms_,
                   zlink_reply_handler_fn handler_,
                   void *userdata_)
{
    std::shared_ptr<socket_request_reply_state_t> state =
      find_or_create_request_reply_state (handle_);
    if (ensure_completion_queue_ready (state) != 0)
        return -1;
    if (ensure_internal_dispatch_installed (state) != 0)
        return -1;

    pending_key_t key;
    pending_request_t pending;
    uint32_t resolved_timeout_ms = zlink::request_reply::default_timeout_ms;
    {
        std::lock_guard<std::mutex> lock (state->mutex);
        const uint64_t request_seq =
          zlink::request_reply_runtime::allocate_request_sequence (
            state.get ());
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
        if (zlink::request_reply_runtime::schedule_timeout_task<
              socket_timeout_callback_ctx_t> (
              resolved_timeout_ms, &on_socket_request_timeout,
              [&] (socket_timeout_callback_ctx_t &ctx_) {
                  ctx_.state = state;
                  ctx_.key = key;
              },
              &pending.timeout_task)
            != 0) {
            return -1;
        }
        state->pending_sequences.insert (request_seq);
        state->pending_requests[key] = pending;
        state->pending_request_keys_by_seq[request_seq] = key;
    }

    const uint8_t message_type = zlink::request_reply::request_type;
    const int rc =
      send_request_reply_message (handle_.socket, peer_rid_, parts_, part_count_,
                                  flags_, message_type, key.request_seq);
    if (rc != 0) {
        std::lock_guard<std::mutex> lock (state->mutex);
        zlink::request_timeout::cancel (pending.timeout_task);
        state->pending_sequences.erase (key.request_seq);
        state->pending_request_keys_by_seq.erase (key.request_seq);
        state->pending_requests.erase (key);
        return -1;
    }
    return 0;
}
}
}
