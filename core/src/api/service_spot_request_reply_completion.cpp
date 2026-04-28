/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include <memory>
#include <vector>

#include "api/socket_api_internal.hpp"
#include "api/socket_request_reply_internal.hpp"
#include "api/service_api_internal.hpp"
#include "api/service_spot_request_reply_internal.hpp"
#include "services/spot/spot_handle.hpp"
#include "services/spot/spot_node_access.hpp"
#include "services/spot/spot_runtime.hpp"

namespace
{
using zlink::spot_reqrep_internal::router_spot_request_reply_state_t;
using zlink::spot_reqrep_internal::spot_channel_reply_source_t;
using zlink::spot_reqrep_internal::spot_request_reply_state_t;

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

size_t routed_queue_hard_limit (zlink::spot_runtime_t *runtime_)
{
    if (!runtime_)
        return ZLINK_SPOT_NODE_ROUTED_QUEUE_HARD_LIMIT_DFLT;

    const zlink::spot_node_hwm_config_t config = runtime_->hwm_config_snapshot ();
    if (config.routed_queue_hard_limit_enabled)
        return static_cast<size_t> (config.routed_queue_hard_limit);
    return ZLINK_SPOT_NODE_ROUTED_QUEUE_HARD_LIMIT_DFLT;
}

void apply_routed_queue_limit (zlink::internal_pair_queue::queue_t *queue_,
                               size_t hard_limit_)
{
    if (!queue_)
        return;

    const int hwm = hard_limit_ > static_cast<size_t> (INT_MAX)
                      ? INT_MAX
                      : static_cast<int> (hard_limit_);
    if (queue_->rx)
        (void) queue_->rx->setsockopt (ZLINK_INTERNAL_OPT_RCVHWM, &hwm,
                                       sizeof (hwm));
    if (queue_->tx)
        (void) queue_->tx->setsockopt (ZLINK_INTERNAL_OPT_SNDHWM, &hwm,
                                       sizeof (hwm));
}

std::shared_ptr<spot_channel_reply_source_t> find_channel_reply_source_locked (
  const std::shared_ptr<spot_request_reply_state_t> &state_,
  void *dealer_)
{
    if (!state_ || !dealer_)
        return std::shared_ptr<spot_channel_reply_source_t> ();

    std::map<void *, std::shared_ptr<spot_channel_reply_source_t> >::iterator it =
      state_->completion_state.channel_reply_sources.find (dealer_);
    return it != state_->completion_state.channel_reply_sources.end ()
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
    state_->completion_state.channel_reply_sources[dealer_] = source;
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
      &state_->completion_state.direct, ctx, "zlink.spot.reqrep.completion");
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
        if (state_->recv.routed_recv_queue.disconnected) {
            errno = ENOTCONN;
            return -1;
        }
        if (!state_->recv.routed_recv_socket) {
            if (zlink::internal_pair_queue::ensure (
                  ctx, "zlink.spot.route", &state_->recv.routed_recv_queue.signal)
                != 0) {
                return -1;
            }
            apply_routed_queue_limit (&state_->recv.routed_recv_queue.signal,
                                      routed_queue_hard_limit (runtime));
            zlink::spot_node_access_t::track_owned_socket (
              spot->node, state_->recv.routed_recv_queue.signal.rx);
            zlink::spot_node_access_t::track_owned_socket (
              spot->node, state_->recv.routed_recv_queue.signal.tx);
            state_->recv.routed_recv_socket = state_->recv.routed_recv_queue.signal.rx;
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
      &state_->completion_state.direct,
      ctx,
      "zlink.spot.reqrep.completion",
      handler_,
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

zlink::socket_base_t *zlink::spot_reqrep_internal::spot_routed_recv_socket (
  const std::shared_ptr<spot_request_reply_state_t> &state_)
{
    if (!state_)
        return NULL;

    std::lock_guard<std::mutex> lock (state_->mutex);
    return state_->recv.routed_recv_socket;
}

void zlink::spot_reqrep_internal::close_spot_routed_recv_state (
  const std::shared_ptr<spot_request_reply_state_t> &state_,
  zlink::internal_pair_queue::queue_t *signal_out_)
{
    if (!state_)
        return;

    std::lock_guard<std::mutex> lock (state_->mutex);
    if (signal_out_)
        *signal_out_ = state_->recv.routed_recv_queue.signal;
    state_->recv.routed_recv_queue.signal = zlink::internal_pair_queue::queue_t ();
    state_->recv.routed_recv_queue.pending.clear ();
    state_->recv.routed_recv_queue.pending_count = 0;
    state_->recv.routed_recv_socket = NULL;
}

int zlink::spot_reqrep_internal::drain_spot_reply_completions (
  const std::shared_ptr<spot_request_reply_state_t> &state_,
  void *owner_handle_)
{
    if (!state_) {
        errno = EFAULT;
        return -1;
    }
    return zlink::request_completion::drain (&state_->completion_state.direct,
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
             ? zlink::request_completion::has_pending (
                 &state_->completion_state.direct)
             : false;
}

bool zlink::spot_reqrep_internal::has_spot_channel_reply_completions (
  const std::shared_ptr<spot_request_reply_state_t> &state_)
{
    if (!state_)
        return false;

    std::lock_guard<std::mutex> lock (state_->mutex);
    for (std::map<void *, std::shared_ptr<spot_channel_reply_source_t> >::const_iterator
           it = state_->completion_state.channel_reply_sources.begin ();
         it != state_->completion_state.channel_reply_sources.end (); ++it) {
        if (it->second
            && zlink::request_completion::has_pending (&it->second->completion)) {
            return true;
        }
    }
    return false;
}

bool zlink::spot_reqrep_internal::has_spot_channel_reply_source (
  const std::shared_ptr<spot_request_reply_state_t> &state_,
  void *dealer_)
{
    if (!state_ || !dealer_)
        return false;

    std::lock_guard<std::mutex> lock (state_->mutex);
    return state_->completion_state.channel_reply_sources.count (dealer_) != 0;
}

void zlink::spot_reqrep_internal::snapshot_spot_channel_reply_dealers (
  const std::shared_ptr<spot_request_reply_state_t> &state_,
  std::vector<void *> *dealers_out_)
{
    if (!dealers_out_) {
        errno = EFAULT;
        return;
    }

    dealers_out_->clear ();
    if (!state_)
        return;

    std::lock_guard<std::mutex> lock (state_->mutex);
    dealers_out_->reserve (state_->completion_state.channel_reply_sources.size ());
    for (std::map<void *, std::shared_ptr<spot_channel_reply_source_t> >::const_iterator
           it = state_->completion_state.channel_reply_sources.begin ();
         it != state_->completion_state.channel_reply_sources.end (); ++it) {
        dealers_out_->push_back (it->first);
    }
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
             ? zlink::request_completion::signal_socket (
                 &state_->completion_state.direct)
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
    zlink::request_completion::claim_owner_thread (
      &state_->completion_state.direct);
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
                 &state_->completion_state.direct)
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
    snapshot_spot_channel_reply_dealers (state_, &dealers);

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
