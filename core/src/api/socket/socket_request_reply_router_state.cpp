/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "api/socket/socket_request_reply_router_state_internal.hpp"

#include "api/socket/socket_api_internal.hpp"
#include "sockets/common/socket_base.hpp"
#include "core/multipart_send_txn.hpp"
#include "utils/err.hpp"

#include <string.h>

namespace
{
using zlink::reqrep_internal::pending_reply_t;
using zlink::reqrep_internal::router_request_reply_state_t;
using zlink::reqrep_internal::router_state_identity_index_t;

std::mutex g_request_reply_index_mutex;
router_state_identity_index_t g_router_state_identity_index;

zlink::ctx_t *resolve_router_state_ctx (const std::shared_ptr<router_request_reply_state_t> &state_)
{
    if (!state_)
        return NULL;

    socket_handle_t handle = as_socket_handle (state_->owner);
    if (!handle.socket) {
        errno = EFAULT;
        return NULL;
    }
    return handle.socket->get_ctx ();
}

struct router_timeout_callback_ctx_t
{
    std::shared_ptr<router_request_reply_state_t> state;
    uint64_t request_seq;
};

void on_router_request_timeout (void *userdata_)
{
    std::unique_ptr<router_timeout_callback_ctx_t> ctx (
      static_cast<router_timeout_callback_ctx_t *> (userdata_));
    if (!ctx.get () || !ctx->state)
        return;

    pending_reply_t pending;
    bool found = false;
    {
        std::lock_guard<std::mutex> lock (ctx->state->mutex);
        std::unordered_map<uint64_t, pending_reply_t>::iterator it =
          ctx->state->requests.pending_replies.find (ctx->request_seq);
        if (it == ctx->state->requests.pending_replies.end ())
            return;
        pending = it->second;
        ctx->state->requests.pending_sequences.erase (ctx->request_seq);
        ctx->state->requests.pending_replies.erase (it);
        found = true;
    }

    if (found) {
        (void) zlink::reqrep_internal::queue_router_reply_completion (
          ctx->state, pending.handler, pending.userdata, ETIMEDOUT, NULL, 0);
    }
}
}

zlink::reqrep_internal::router_request_reply_request_state_t::
  router_request_reply_request_state_t ()
{
}

zlink::reqrep_internal::router_request_reply_state_t::router_request_reply_state_t (void *owner_) :
    owner (owner_)
{
}

std::mutex &zlink::reqrep_internal::request_reply_index_mutex ()
{
    return g_request_reply_index_mutex;
}

router_state_identity_index_t &zlink::reqrep_internal::router_state_identity_index ()
{
    return g_router_state_identity_index;
}

std::shared_ptr<router_request_reply_state_t>
zlink::reqrep_internal::find_or_create_router_state (void *router_)
{
    const socket_handle_t handle = as_socket_handle (router_);
    if (!handle.socket)
        return std::shared_ptr<router_request_reply_state_t> ();

    std::shared_ptr<router_request_reply_state_t> state =
      handle.socket->router_request_reply_state ();
    if (!state) {
        state.reset (new (std::nothrow) router_request_reply_state_t (router_));
        if (!state) {
            errno = ENOMEM;
            return state;
        }
        handle.socket->set_router_request_reply_state (state);
    }
    return state;
}

int zlink::reqrep_internal::register_router_pending_request (
  const std::shared_ptr<router_request_reply_state_t> &state_,
  uint64_t request_seq_,
  uint32_t timeout_ms_,
  zlink_reply_handler_fn handler_,
  void *userdata_)
{
    if (!state_ || !handler_ || request_seq_ == 0) {
        errno = EINVAL;
        return -1;
    }

    pending_reply_t pending;
    pending.handler = handler_;
    pending.userdata = userdata_;
    pending.deadline_ns = 0;

    const uint32_t resolved_timeout_ms =
      zlink::request_reply::resolve_timeout_ms (timeout_ms_, state_->requests.default_timeout_ms);
    if (zlink::request_reply_runtime::schedule_timeout_task<router_timeout_callback_ctx_t> (
          resolved_timeout_ms, &on_router_request_timeout,
          [&] (router_timeout_callback_ctx_t &ctx_) {
              ctx_.state = state_;
              ctx_.request_seq = request_seq_;
          },
          &pending.timeout_task)
        != 0) {
        return -1;
    }

    std::lock_guard<std::mutex> lock (state_->mutex);
    state_->requests.pending_sequences.insert (request_seq_);
    state_->requests.pending_replies[request_seq_] = pending;
    return 0;
}

int zlink::reqrep_internal::validate_request_parts (zlink_msg_t *parts_, size_t part_count_)
{
    if ((!parts_ && part_count_ > 0) || part_count_ == 0) {
        errno = EFAULT;
        return -1;
    }

    return 0;
}

int zlink::reqrep_internal::init_buffer_frame (zlink_msg_t *msg_, const void *data_, size_t size_)
{
    if (!msg_) {
        errno = EFAULT;
        return -1;
    }
    if (zlink_msg_init_size (msg_, size_) != 0)
        return -1;
    if (size_ > 0 && data_)
        memcpy (zlink_msg_data (msg_), data_, size_);
    return 0;
}

int zlink::reqrep_internal::ensure_router_completion_queue_ready (
  const std::shared_ptr<router_request_reply_state_t> &state_)
{
    zlink::ctx_t *ctx = resolve_router_state_ctx (state_);
    if (!ctx)
        return -1;
    return zlink::request_completion::ensure_signal_ready (&state_->completion, ctx,
                                                           "zlink.router.reqrep.completion");
}

int zlink::reqrep_internal::queue_router_reply_completion (
  const std::shared_ptr<router_request_reply_state_t> &state_,
  zlink_reply_handler_fn handler_,
  void *userdata_,
  int errnum_,
  zlink_msg_t *parts_,
  size_t part_count_)
{
    zlink::ctx_t *ctx = resolve_router_state_ctx (state_);
    if (!ctx)
        return -1;
    return zlink::request_completion::enqueue (&state_->completion, ctx,
                                               "zlink.router.reqrep.completion", handler_,
                                               userdata_, errnum_, parts_, part_count_);
}

int zlink::reqrep_internal::drain_router_reply_completions (
  const std::shared_ptr<router_request_reply_state_t> &state_, void *owner_handle_)
{
    if (!state_) {
        errno = EFAULT;
        return -1;
    }
    return zlink::request_completion::drain (&state_->completion, owner_handle_);
}

bool zlink::reqrep_internal::has_router_reply_completions (
  const std::shared_ptr<router_request_reply_state_t> &state_)
{
    return state_ ? zlink::request_completion::has_pending (&state_->completion) : false;
}

zlink::socket_base_t *zlink::reqrep_internal::router_completion_signal_socket (
  const std::shared_ptr<router_request_reply_state_t> &state_)
{
    return state_ ? zlink::request_completion::signal_socket (&state_->completion) : NULL;
}

void zlink::reqrep_internal::claim_router_completion_owner (
  const std::shared_ptr<router_request_reply_state_t> &state_)
{
    if (!state_)
        return;
    zlink::request_completion::claim_owner_thread (&state_->completion);
}

bool zlink::reqrep_internal::current_thread_is_router_completion_owner (
  const std::shared_ptr<router_request_reply_state_t> &state_)
{
    return state_ ? zlink::request_completion::current_thread_is_owner (&state_->completion)
                  : false;
}

bool zlink::reqrep_internal::in_request_completion_callback (void *handle_)
{
    return zlink::request_completion::in_request_completion_callback (handle_);
}

bool zlink::reqrep_internal::has_pending_router_request_work (
  const std::shared_ptr<router_request_reply_state_t> &state_)
{
    if (!state_)
        return false;
    if (has_router_reply_completions (state_))
        return true;
    std::lock_guard<std::mutex> lock (state_->mutex);
    return !state_->requests.pending_replies.empty ();
}

int zlink::reqrep_internal::drain_close_router_request_reply_state (void *router_)
{
    socket_handle_t handle = as_socket_handle (router_);
    if (!handle.socket) {
        errno = EFAULT;
        return -1;
    }

    std::shared_ptr<router_request_reply_state_t> state =
      handle.socket->router_request_reply_state ();
    if (!state)
        return 0;

    std::vector<pending_reply_t> pending;
    {
        std::lock_guard<std::mutex> lock (state->mutex);
        for (std::unordered_map<uint64_t, pending_reply_t>::iterator it =
               state->requests.pending_replies.begin ();
             it != state->requests.pending_replies.end (); ++it) {
            pending.push_back (it->second);
        }
        state->requests.pending_replies.clear ();
        state->requests.pending_sequences.clear ();
    }

    for (size_t i = 0; i < pending.size (); ++i) {
        zlink::request_timeout::cancel (pending[i].timeout_task);
        if (queue_router_reply_completion (state, pending[i].handler, pending[i].userdata, ETERM,
                                           NULL, 0)
            != 0) {
            return -1;
        }
    }

    return drain_router_reply_completions (state, router_);
}

void zlink::reqrep_internal::cleanup_router_request_reply_state (void *router_)
{
    const socket_handle_t handle = as_socket_handle (router_);
    if (!handle.socket)
        return;

    std::shared_ptr<router_request_reply_state_t> state =
      handle.socket->router_request_reply_state ();
    if (state)
        (void) drain_close_router_request_reply_state (router_);
    if (state) {
        std::lock_guard<std::mutex> state_lock (state->mutex);
        zlink::request_completion::close (&state->completion);
    }
    handle.socket->clear_router_request_reply_state ();
    std::lock_guard<std::mutex> lock (request_reply_index_mutex ());
    for (router_state_identity_index_t::iterator it = router_state_identity_index ().begin ();
         it != router_state_identity_index ().end ();) {
        std::shared_ptr<router_request_reply_state_t> indexed = it->second.lock ();
        if (!indexed || indexed == state)
            it = router_state_identity_index ().erase (it);
        else
            ++it;
    }
}

int zlink::reqrep_internal::send_combined_parts_on_socket (zlink::socket_base_t *socket_,
                                                           std::vector<zlink_msg_t> *parts_,
                                                           zlink_send_flags_t flags_)
{
    if (!socket_ || !parts_ || parts_->empty ()) {
        errno = EFAULT;
        return -1;
    }

    socket_->set_all_pipes_nodelay ();
    return zlink::logical_multipart_send (socket_, &(*parts_)[0], parts_->size (), flags_);
}
