/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include <memory>

#include "api/request_reply_protocol_internal.hpp"
#include "api/service_spot_request_reply_internal.hpp"

namespace
{
using zlink::spot_reqrep_internal::pending_reply_t;
using zlink::spot_reqrep_internal::pending_spot_key_t;
using zlink::spot_reqrep_internal::router_spot_request_reply_state_t;
using zlink::spot_reqrep_internal::spot_request_reply_state_t;

struct spot_timeout_callback_ctx_t
{
    std::shared_ptr<spot_request_reply_state_t> state;
    pending_spot_key_t key;
};

struct router_spot_timeout_callback_ctx_t
{
    std::shared_ptr<router_spot_request_reply_state_t> state;
    uint64_t request_seq;
};

void destroy_spot_timeout_callback_ctx (void *userdata_)
{
    delete static_cast<spot_timeout_callback_ctx_t *> (userdata_);
}

void destroy_router_spot_timeout_callback_ctx (void *userdata_)
{
    delete static_cast<router_spot_timeout_callback_ctx_t *> (userdata_);
}

void on_spot_request_timeout (void *userdata_)
{
    std::unique_ptr<spot_timeout_callback_ctx_t> ctx (
      static_cast<spot_timeout_callback_ctx_t *> (userdata_));
    if (!ctx.get () || !ctx->state)
        return;

    pending_reply_t pending;
    bool found = false;
    {
        std::lock_guard<std::mutex> lock (ctx->state->mutex);
        std::map<pending_spot_key_t, pending_reply_t>::iterator it =
          ctx->state->pending_replies.find (ctx->key);
        if (it == ctx->state->pending_replies.end ())
            return;
        pending = it->second;
        ctx->state->pending_sequences.erase (ctx->key.request_seq);
        ctx->state->pending_replies.erase (it);
        found = true;
    }

    if (found)
        zlink::request_reply::complete_reply_callback (
          pending.handler, ETIMEDOUT, NULL, 0, pending.userdata);
}

void on_router_spot_request_timeout (void *userdata_)
{
    std::unique_ptr<router_spot_timeout_callback_ctx_t> ctx (
      static_cast<router_spot_timeout_callback_ctx_t *> (userdata_));
    if (!ctx.get () || !ctx->state)
        return;

    pending_reply_t pending;
    bool found = false;
    {
        std::lock_guard<std::mutex> lock (ctx->state->mutex);
        std::map<uint64_t, pending_reply_t>::iterator it =
          ctx->state->pending_replies.find (ctx->request_seq);
        if (it == ctx->state->pending_replies.end ())
            return;
        pending = it->second;
        ctx->state->pending_sequences.erase (ctx->request_seq);
        ctx->state->pending_replies.erase (it);
        found = true;
    }

    if (found)
        zlink::request_reply::complete_reply_callback (
          pending.handler, ETIMEDOUT, NULL, 0, pending.userdata);
}
}

int zlink::spot_reqrep_internal::register_spot_pending_request (
  const std::shared_ptr<spot_request_reply_state_t> &state_,
  const pending_spot_key_t &key_,
  uint32_t timeout_ms_,
  zlink_reply_handler_fn handler_,
  void *userdata_)
{
    if (!state_ || !handler_ || key_.request_seq == 0) {
        errno = EINVAL;
        return -1;
    }

    pending_reply_t pending;
    pending.key = key_;
    pending.handler = handler_;
    pending.userdata = userdata_;

    const uint32_t resolved_timeout_ms =
      zlink::request_reply::resolve_timeout_ms (timeout_ms_,
                                                state_->default_timeout_ms);
    std::unique_ptr<spot_timeout_callback_ctx_t> timeout_ctx (
      new (std::nothrow) spot_timeout_callback_ctx_t ());
    if (!timeout_ctx.get ()) {
        errno = ENOMEM;
        return -1;
    }
    timeout_ctx->state = state_;
    timeout_ctx->key = key_;
    pending.timeout_task =
      zlink::request_timeout::schedule (resolved_timeout_ms,
                                        &on_spot_request_timeout,
                                        timeout_ctx.release (),
                                        &destroy_spot_timeout_callback_ctx);
    if (!pending.timeout_task) {
        errno = ENOMEM;
        return -1;
    }

    std::lock_guard<std::mutex> lock (state_->mutex);
    state_->pending_sequences.insert (key_.request_seq);
    state_->pending_replies[key_] = pending;
    return 0;
}

int zlink::spot_reqrep_internal::register_router_spot_pending_request (
  const std::shared_ptr<router_spot_request_reply_state_t> &state_,
  uint64_t request_seq_,
  const pending_spot_key_t &key_,
  uint32_t timeout_ms_,
  zlink_reply_handler_fn handler_,
  void *userdata_)
{
    if (!state_ || !handler_ || request_seq_ == 0) {
        errno = EINVAL;
        return -1;
    }

    pending_reply_t pending;
    pending.key = key_;
    pending.handler = handler_;
    pending.userdata = userdata_;

    const uint32_t resolved_timeout_ms =
      zlink::request_reply::resolve_timeout_ms (timeout_ms_,
                                                state_->default_timeout_ms);
    std::unique_ptr<router_spot_timeout_callback_ctx_t> timeout_ctx (
      new (std::nothrow) router_spot_timeout_callback_ctx_t ());
    if (!timeout_ctx.get ()) {
        errno = ENOMEM;
        return -1;
    }
    timeout_ctx->state = state_;
    timeout_ctx->request_seq = request_seq_;
    pending.timeout_task =
      zlink::request_timeout::schedule (
        resolved_timeout_ms, &on_router_spot_request_timeout,
        timeout_ctx.release (), &destroy_router_spot_timeout_callback_ctx);
    if (!pending.timeout_task) {
        errno = ENOMEM;
        return -1;
    }

    std::lock_guard<std::mutex> lock (state_->mutex);
    state_->pending_sequences.insert (request_seq_);
    state_->pending_replies[request_seq_] = pending;
    return 0;
}

void zlink::spot_reqrep_internal::erase_spot_pending_request (
  const std::shared_ptr<spot_request_reply_state_t> &state_,
  const pending_spot_key_t &key_)
{
    if (!state_)
        return;

    std::lock_guard<std::mutex> lock (state_->mutex);
    state_->pending_sequences.erase (key_.request_seq);
    std::map<pending_spot_key_t, pending_reply_t>::iterator it =
      state_->pending_replies.find (key_);
    if (it == state_->pending_replies.end ())
        return;
    zlink::request_timeout::cancel (it->second.timeout_task);
    state_->pending_replies.erase (it);
}

