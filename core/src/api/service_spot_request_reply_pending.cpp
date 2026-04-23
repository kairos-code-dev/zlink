/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include <memory>
#include <vector>

#include "api/socket_api_internal.hpp"
#include "api/socket_request_reply_internal.hpp"
#include "api/request_reply_protocol_internal.hpp"
#include "api/service_api_internal.hpp"
#include "core/internal_defs.hpp"
#include "services/spot/spot_node_access.hpp"
#include "services/spot/spot_handle.hpp"
#include "services/spot/spot_runtime.hpp"
#include "services/spot/spot_subject_access.hpp"
#include "api/service_spot_request_reply_internal.hpp"

namespace
{
using zlink::spot_reqrep_internal::pending_reply_t;
using zlink::spot_reqrep_internal::pending_spot_key_t;
using zlink::spot_reqrep_internal::router_spot_request_reply_state_t;
using zlink::spot_reqrep_internal::spot_channel_reply_source_t;
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
        std::unordered_map<pending_spot_key_t,
                           pending_reply_t,
                           zlink::spot_reqrep_internal::pending_spot_key_hash_t>::iterator it =
          ctx->state->pending_replies.find (ctx->key);
        if (it == ctx->state->pending_replies.end ())
            return;
        pending = it->second;
        ctx->state->pending_sequences.erase (ctx->key.request_seq);
        ctx->state->pending_replies.erase (it);
        found = true;
    }

    if (found)
        (void) zlink::spot_reqrep_internal::queue_spot_reply_completion (
          ctx->state, pending.handler, pending.userdata, ETIMEDOUT, NULL, 0);
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
        std::unordered_map<uint64_t, pending_reply_t>::iterator it =
          ctx->state->pending_replies.find (ctx->request_seq);
        if (it == ctx->state->pending_replies.end ())
            return;
        pending = it->second;
        ctx->state->pending_sequences.erase (ctx->request_seq);
        ctx->state->pending_replies.erase (it);
        found = true;
    }

    if (found)
        (void) zlink::spot_reqrep_internal::queue_router_reply_completion (
          ctx->state, pending.handler, pending.userdata, ETIMEDOUT, NULL, 0);
}
}

namespace
{
zlink::ctx_t *resolve_spot_state_ctx (
  const std::shared_ptr<spot_request_reply_state_t> &state_)
{
    if (!state_)
        return NULL;

    spot_handle_t *spot = as_spot_handle (state_->owner);
    if (!spot || !spot->node) {
        errno = EFAULT;
        return NULL;
    }
    return zlink::spot_node_access_t::ctx (spot->node);
}

zlink::ctx_t *resolve_router_state_ctx (
  const std::shared_ptr<router_spot_request_reply_state_t> &state_)
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

std::shared_ptr<spot_channel_reply_source_t> find_channel_reply_source_locked (
  const std::shared_ptr<spot_request_reply_state_t> &state_,
  void *dealer_)
{
    if (!state_ || !dealer_)
        return std::shared_ptr<spot_channel_reply_source_t> ();

    std::map<void *, std::shared_ptr<spot_channel_reply_source_t> >::iterator it =
      state_->channel_reply_sources.find (dealer_);
    return it != state_->channel_reply_sources.end ()
             ? it->second
             : std::shared_ptr<spot_channel_reply_source_t> ();
}

std::shared_ptr<spot_channel_reply_source_t> find_or_create_channel_reply_source (
  const std::shared_ptr<spot_request_reply_state_t> &state_,
  void *dealer_)
{
    if (!state_ || !dealer_) {
        errno = EFAULT;
        return std::shared_ptr<spot_channel_reply_source_t> ();
    }

    std::lock_guard<std::mutex> lock (state_->mutex);
    std::shared_ptr<spot_channel_reply_source_t> source =
      find_channel_reply_source_locked (state_, dealer_);
    if (source)
        return source;

    source.reset (new (std::nothrow) spot_channel_reply_source_t (dealer_));
    if (!source) {
        errno = ENOMEM;
        return std::shared_ptr<spot_channel_reply_source_t> ();
    }
    state_->channel_reply_sources[dealer_] = source;
    return source;
}

}

int zlink::spot_reqrep_internal::ensure_spot_completion_queue_ready (
  const std::shared_ptr<spot_request_reply_state_t> &state_)
{
    zlink::ctx_t *ctx = resolve_spot_state_ctx (state_);
    if (!ctx)
        return -1;
    return zlink::request_completion::ensure_signal_ready (
      &state_->completion, ctx, "zlink.spot.reqrep.completion");
}

int zlink::spot_reqrep_internal::ensure_spot_recv_ready (
  const std::shared_ptr<spot_request_reply_state_t> &state_)
{
    if (!state_) {
        errno = EFAULT;
        return -1;
    }

    spot_handle_t *spot = as_spot_handle (state_->owner);
    if (!spot || !spot->node) {
        errno = EFAULT;
        return -1;
    }

    zlink::ctx_t *ctx = zlink::spot_node_access_t::ctx (spot->node);
    zlink::spot_runtime_t *runtime = zlink::spot_node_access_t::runtime (spot->node);
    if (!ctx || !runtime || !runtime->execution.data_plane_running) {
        errno = ETERM;
        return -1;
    }

    {
        std::lock_guard<std::mutex> lock (state_->mutex);
        if (!state_->routed_recv_socket) {
            if (zlink::internal_pair_queue::ensure (
                  ctx, "zlink.spot.route", &state_->routed_recv_queue.signal)
                != 0) {
                return -1;
            }
            zlink::spot_node_access_t::track_owned_socket (
              spot->node, state_->routed_recv_queue.signal.rx);
            zlink::spot_node_access_t::track_owned_socket (
              spot->node, state_->routed_recv_queue.signal.tx);
            state_->routed_recv_socket = state_->routed_recv_queue.signal.rx;
        }
    }

    return ensure_spot_completion_queue_ready (state_);
}

int zlink::spot_reqrep_internal::ensure_router_completion_queue_ready (
  const std::shared_ptr<router_spot_request_reply_state_t> &state_)
{
    zlink::ctx_t *ctx = resolve_router_state_ctx (state_);
    if (!ctx)
        return -1;
    return zlink::request_completion::ensure_signal_ready (
      &state_->completion, ctx, "zlink.router.spot.reqrep.completion");
}

int zlink::spot_reqrep_internal::queue_spot_reply_completion (
  const std::shared_ptr<spot_request_reply_state_t> &state_,
  zlink_reply_handler_fn handler_,
  void *userdata_,
  int errnum_,
  zlink_msg_t *parts_,
  size_t part_count_)
{
    zlink::ctx_t *ctx = resolve_spot_state_ctx (state_);
    if (!ctx)
        return -1;
    return zlink::request_completion::enqueue (
      &state_->completion, ctx, "zlink.spot.reqrep.completion", handler_,
      userdata_, errnum_, parts_, part_count_);
}

int zlink::spot_reqrep_internal::queue_router_reply_completion (
  const std::shared_ptr<router_spot_request_reply_state_t> &state_,
  zlink_reply_handler_fn handler_,
  void *userdata_,
  int errnum_,
  zlink_msg_t *parts_,
  size_t part_count_)
{
    zlink::ctx_t *ctx = resolve_router_state_ctx (state_);
    if (!ctx)
        return -1;
    return zlink::request_completion::enqueue (
      &state_->completion, ctx, "zlink.router.spot.reqrep.completion",
      handler_, userdata_, errnum_, parts_, part_count_);
}

int zlink::spot_reqrep_internal::queue_spot_channel_reply_completion (
  const std::shared_ptr<spot_request_reply_state_t> &state_,
  void *dealer_,
  zlink_reply_handler_fn handler_,
  void *userdata_,
  int errnum_,
  zlink_msg_t *parts_,
  size_t part_count_)
{
    zlink::ctx_t *ctx = resolve_spot_state_ctx (state_);
    if (!ctx)
        return -1;

    std::shared_ptr<spot_channel_reply_source_t> source =
      find_or_create_channel_reply_source (state_, dealer_);
    if (!source)
        return -1;

    if (zlink::request_completion::enqueue (
          &source->completion, ctx, "zlink.spot.channel.reply", handler_,
          userdata_, errnum_, parts_, part_count_)
        != 0) {
        return -1;
    }

    zlink_spot_notify_dispatch_info (
      state_->owner, ZLINK_SPOT_DISPATCH_EVENT_CHANNEL_REPLY_READABLE,
      ZLINK_SPOT_DISPATCH_SUBJECT_CHANNEL_DEALER, dealer_);
    return 0;
}

int zlink::spot_reqrep_internal::drain_spot_reply_completions (
  const std::shared_ptr<spot_request_reply_state_t> &state_,
  void *owner_handle_)
{
    if (!state_) {
        errno = EFAULT;
        return -1;
    }
    return zlink::request_completion::drain (&state_->completion,
                                             owner_handle_);
}

int zlink::spot_reqrep_internal::drain_spot_channel_reply_completions_from (
  const std::shared_ptr<spot_request_reply_state_t> &state_,
  void *owner_handle_,
  void *dealer_)
{
    if (!state_ || !dealer_) {
        errno = EFAULT;
        return -1;
    }

    std::shared_ptr<spot_channel_reply_source_t> source;
    {
        std::lock_guard<std::mutex> lock (state_->mutex);
        source = find_channel_reply_source_locked (state_, dealer_);
    }
    if (!source) {
        errno = ENOENT;
        return -1;
    }

    return zlink::request_completion::drain (&source->completion,
                                             owner_handle_);
}

int zlink::spot_reqrep_internal::drain_router_reply_completions (
  const std::shared_ptr<router_spot_request_reply_state_t> &state_,
  void *owner_handle_)
{
    if (!state_) {
        errno = EFAULT;
        return -1;
    }
    return zlink::request_completion::drain (&state_->completion,
                                             owner_handle_);
}

bool zlink::spot_reqrep_internal::has_spot_reply_completions (
  const std::shared_ptr<spot_request_reply_state_t> &state_)
{
    return state_
             ? zlink::request_completion::has_pending (&state_->completion)
             : false;
}

bool zlink::spot_reqrep_internal::has_spot_channel_reply_completions (
  const std::shared_ptr<spot_request_reply_state_t> &state_)
{
    if (!state_)
        return false;

    std::lock_guard<std::mutex> lock (state_->mutex);
    for (std::map<void *, std::shared_ptr<spot_channel_reply_source_t> >::const_iterator
           it = state_->channel_reply_sources.begin ();
         it != state_->channel_reply_sources.end (); ++it) {
        if (it->second
            && zlink::request_completion::has_pending (&it->second->completion)) {
            return true;
        }
    }
    return false;
}

bool zlink::spot_reqrep_internal::has_router_reply_completions (
  const std::shared_ptr<router_spot_request_reply_state_t> &state_)
{
    return state_
             ? zlink::request_completion::has_pending (&state_->completion)
             : false;
}

zlink::socket_base_t *zlink::spot_reqrep_internal::spot_completion_signal_socket (
  const std::shared_ptr<spot_request_reply_state_t> &state_)
{
    return state_
             ? zlink::request_completion::signal_socket (&state_->completion)
             : NULL;
}

zlink::socket_base_t *
zlink::spot_reqrep_internal::router_completion_signal_socket (
  const std::shared_ptr<router_spot_request_reply_state_t> &state_)
{
    return state_
             ? zlink::request_completion::signal_socket (&state_->completion)
             : NULL;
}

void zlink::spot_reqrep_internal::claim_spot_completion_owner (
  const std::shared_ptr<spot_request_reply_state_t> &state_)
{
    if (!state_)
        return;
    zlink::request_completion::claim_owner_thread (&state_->completion);
}

void zlink::spot_reqrep_internal::claim_router_completion_owner (
  const std::shared_ptr<router_spot_request_reply_state_t> &state_)
{
    if (!state_)
        return;
    zlink::request_completion::claim_owner_thread (&state_->completion);
}

bool zlink::spot_reqrep_internal::current_thread_is_spot_completion_owner (
  const std::shared_ptr<spot_request_reply_state_t> &state_)
{
    return state_
             ? zlink::request_completion::current_thread_is_owner (
                 &state_->completion)
             : false;
}

bool zlink::spot_reqrep_internal::current_thread_is_router_completion_owner (
  const std::shared_ptr<router_spot_request_reply_state_t> &state_)
{
    return state_
             ? zlink::request_completion::current_thread_is_owner (
                 &state_->completion)
             : false;
}

bool zlink::spot_reqrep_internal::in_spot_request_completion_callback (
  void *spot_)
{
    return zlink::request_completion::in_request_completion_callback (spot_);
}

int zlink::spot_reqrep_internal::drain_attached_channel_reply_bridge_progress (
  const std::shared_ptr<spot_request_reply_state_t> &state_)
{
    if (!state_) {
        errno = EFAULT;
        return -1;
    }

    std::vector<void *> dealers;
    {
        std::lock_guard<std::mutex> lock (state_->mutex);
        for (std::map<void *, std::shared_ptr<spot_channel_reply_source_t> >::const_iterator
               it = state_->channel_reply_sources.begin ();
             it != state_->channel_reply_sources.end (); ++it) {
            dealers.push_back (it->first);
        }
    }

    int drained = 0;
    for (size_t i = 0; i < dealers.size (); ++i) {
        socket_handle_t handle = as_socket_handle (dealers[i]);
        if (!handle.socket)
            continue;
        const std::shared_ptr<zlink::socket_reqrep_internal::socket_request_reply_state_t>
          socket_state =
            zlink::socket_reqrep_internal::find_request_reply_state (handle);
        if (!socket_state)
            continue;
        const int rc =
          zlink::socket_reqrep_internal::drain_reply_completions (socket_state,
                                                                  dealers[i]);
        if (rc < 0)
            return -1;
        drained += rc;
    }
    errno = 0;
    return drained;
}

void zlink::spot_reqrep_internal::unregister_spot_channel_reply_observers (
  const std::shared_ptr<spot_request_reply_state_t> &state_)
{
    if (!state_)
        return;

    std::vector<void *> dealers;
    {
        std::lock_guard<std::mutex> lock (state_->mutex);
        for (std::map<void *, std::shared_ptr<spot_channel_reply_source_t> >::const_iterator
               it = state_->channel_reply_sources.begin ();
             it != state_->channel_reply_sources.end (); ++it) {
            dealers.push_back (it->first);
        }
    }

    for (size_t i = 0; i < dealers.size (); ++i) {
        socket_handle_t handle = as_socket_handle (dealers[i]);
        if (!handle.socket)
            continue;
        const std::shared_ptr<zlink::socket_reqrep_internal::socket_request_reply_state_t>
          socket_state =
            zlink::socket_reqrep_internal::find_request_reply_state (handle);
        if (socket_state) {
            zlink::socket_reqrep_internal::unregister_spot_channel_dispatch_observer (
              socket_state, state_->owner);
        }
    }
}

bool zlink::spot_reqrep_internal::has_pending_spot_request_work (
  const std::shared_ptr<spot_request_reply_state_t> &state_)
{
    if (!state_)
        return false;
    if (has_spot_reply_completions (state_))
        return true;
    if (has_spot_channel_reply_completions (state_))
        return true;
    std::lock_guard<std::mutex> lock (state_->mutex);
    return !state_->pending_replies.empty () || state_->pending_channel_requests > 0;
}

bool zlink::spot_reqrep_internal::has_pending_router_spot_request_work (
  const std::shared_ptr<router_spot_request_reply_state_t> &state_)
{
    if (!state_)
        return false;
    if (has_router_reply_completions (state_))
        return true;
    std::lock_guard<std::mutex> lock (state_->mutex);
    return !state_->pending_replies.empty ();
}

int zlink::spot_reqrep_internal::drain_close_spot_request_reply_state (
  void *spot_)
{
    std::shared_ptr<spot_request_reply_state_t> state = try_find_spot_state (spot_);
    if (!state)
        return 0;

    std::vector<pending_reply_t> pending;
    {
        std::lock_guard<std::mutex> lock (state->mutex);
        for (std::unordered_map<pending_spot_key_t,
                                pending_reply_t,
                                pending_spot_key_hash_t>::iterator it =
               state->pending_replies.begin ();
             it != state->pending_replies.end (); ++it) {
            pending.push_back (it->second);
        }
        state->pending_replies.clear ();
        state->pending_sequences.clear ();
    }

    for (size_t i = 0; i < pending.size (); ++i) {
        zlink::request_timeout::cancel (pending[i].timeout_task);
        if (queue_spot_reply_completion (state, pending[i].handler,
                                         pending[i].userdata, ETERM, NULL, 0)
            != 0) {
            return -1;
        }
    }

    std::vector<void *> dealers;
    {
        std::lock_guard<std::mutex> lock (state->mutex);
        for (std::map<void *,
                      std::shared_ptr<spot_channel_reply_source_t> >::const_iterator
               it = state->channel_reply_sources.begin ();
             it != state->channel_reply_sources.end (); ++it) {
            dealers.push_back (it->first);
        }
    }

    for (size_t i = 0; i < dealers.size (); ++i) {
        const socket_handle_t handle = as_socket_handle (dealers[i]);
        if (!handle.socket)
            continue;
        if (zlink::socket_reqrep_internal::drain_close_request_reply_socket (handle)
            < 0) {
            return -1;
        }
    }

    unregister_spot_channel_reply_observers (state);
    const int direct_rc = drain_spot_reply_completions (state, spot_);
    if (direct_rc < 0)
        return -1;

    int drained = direct_rc;
    for (size_t i = 0; i < dealers.size (); ++i) {
        const int rc =
          drain_spot_channel_reply_completions_from (state, spot_, dealers[i]);
        if (rc < 0 && errno != ENOENT)
            return -1;
        if (rc > 0)
            drained += rc;
    }
    return drained;
}

int zlink::spot_reqrep_internal::drain_close_router_spot_request_reply_state (
  void *router_)
{
    socket_handle_t handle = as_socket_handle (router_);
    if (!handle.socket) {
        errno = EFAULT;
        return -1;
    }

    std::shared_ptr<router_spot_request_reply_state_t> state =
      std::static_pointer_cast<router_spot_request_reply_state_t> (
        handle.socket->router_spot_request_reply_state ());
    if (!state)
        return 0;

    std::vector<pending_reply_t> pending;
    {
        std::lock_guard<std::mutex> lock (state->mutex);
        for (std::unordered_map<uint64_t, pending_reply_t>::iterator it =
               state->pending_replies.begin ();
             it != state->pending_replies.end (); ++it) {
            pending.push_back (it->second);
        }
        state->pending_replies.clear ();
        state->pending_sequences.clear ();
    }

    for (size_t i = 0; i < pending.size (); ++i) {
        zlink::request_timeout::cancel (pending[i].timeout_task);
        if (queue_router_reply_completion (state, pending[i].handler,
                                           pending[i].userdata, ETERM, NULL,
                                           0)
            != 0) {
            return -1;
        }
    }

    return drain_router_reply_completions (state, router_);
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
    std::unordered_map<pending_spot_key_t,
                       pending_reply_t,
                       zlink::spot_reqrep_internal::pending_spot_key_hash_t>::iterator it =
      state_->pending_replies.find (key_);
    if (it == state_->pending_replies.end ())
        return;
    zlink::request_timeout::cancel (it->second.timeout_task);
    state_->pending_replies.erase (it);
}
