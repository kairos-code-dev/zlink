/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include <memory>
#include <mutex>
#include <string>

#include "api/service_api_internal.hpp"
#include "api/service_spot_dispatch_context_internal.hpp"
#include "api/service_spot_request_reply_internal.hpp"
#include "api/service_spot_request_reply_utils_internal.hpp"
#include "api/socket_api_internal.hpp"
#include "core/ctx.hpp"
#include "services/control/service_control_runtime.hpp"

namespace
{
using zlink::spot_reqrep_internal::g_spot_request_reply_index_mutex;
using zlink::spot_reqrep_internal::g_spot_state_identity_index;
using zlink::spot_reqrep_internal::resolve_spot_identity;
using zlink::spot_reqrep_internal::routing_pair_t;
using zlink::spot_reqrep_internal::router_spot_request_reply_state_t;
using zlink::spot_reqrep_internal::spot_request_reply_state_t;

typedef std::pair<int, void *> dispatch_key_t;

std::deque<zlink_spot_dispatch_info_t> *dispatch_queue_for_event (
  zlink::spot_reqrep_internal::spot_dispatch_state_t *state_,
  zlink_spot_dispatch_event_t event_)
{
    switch (event_) {
    case ZLINK_SPOT_DISPATCH_EVENT_SUBSCRIBE_READABLE:
        return state_ ? &state_->subscribe_pending : NULL;
    case ZLINK_SPOT_DISPATCH_EVENT_ROUTED_READABLE:
        return state_ ? &state_->routed_pending : NULL;
    case ZLINK_SPOT_DISPATCH_EVENT_CHANNEL_REPLY_READABLE:
        return state_ ? &state_->channel_reply_pending : NULL;
    case ZLINK_SPOT_DISPATCH_EVENT_TIMER_READABLE:
        return state_ ? &state_->timer_pending : NULL;
    default:
        return NULL;
    }
}

dispatch_key_t dispatch_key_from_info (const zlink_spot_dispatch_info_t &info_)
{
    return std::make_pair (static_cast<int> (info_.event), info_.subject);
}

bool dispatch_has_pending (
  const zlink::spot_reqrep_internal::spot_dispatch_state_t &dispatch_)
{
    return !dispatch_.subscribe_pending.empty ()
           || !dispatch_.routed_pending.empty ()
           || !dispatch_.channel_reply_pending.empty ()
           || !dispatch_.timer_pending.empty ();
}

bool pop_next_dispatch_info (
  zlink::spot_reqrep_internal::spot_dispatch_state_t *dispatch_,
  zlink_spot_dispatch_info_t *info_out_)
{
    if (!dispatch_ || !info_out_)
        return false;

    std::deque<zlink_spot_dispatch_info_t> *queue =
      dispatch_queue_for_event (dispatch_,
                                ZLINK_SPOT_DISPATCH_EVENT_SUBSCRIBE_READABLE);
    if (queue && !queue->empty ()) {
        *info_out_ = queue->front ();
        queue->pop_front ();
        return true;
    }

    queue = dispatch_queue_for_event (dispatch_,
                                      ZLINK_SPOT_DISPATCH_EVENT_ROUTED_READABLE);
    if (queue && !queue->empty ()) {
        *info_out_ = queue->front ();
        queue->pop_front ();
        return true;
    }

    queue = dispatch_queue_for_event (
      dispatch_, ZLINK_SPOT_DISPATCH_EVENT_CHANNEL_REPLY_READABLE);
    if (queue && !queue->empty ()) {
        *info_out_ = queue->front ();
        queue->pop_front ();
        return true;
    }

    queue = dispatch_queue_for_event (dispatch_,
                                      ZLINK_SPOT_DISPATCH_EVENT_TIMER_READABLE);
    if (queue && !queue->empty ()) {
        *info_out_ = queue->front ();
        queue->pop_front ();
        return true;
    }

    return false;
}

void run_pending_spot_dispatch_events (
  const std::shared_ptr<spot_request_reply_state_t> &state_)
{
    while (true) {
        zlink::spot_reqrep_internal::spot_dispatch_state_t &dispatch =
          state_->dispatch;
        zlink_spot_dispatch_info_t info;
        memset (&info, 0, sizeof (info));
        {
            std::lock_guard<std::mutex> dispatch_lock (dispatch.mutex);
            if (!pop_next_dispatch_info (&dispatch, &info)) {
                dispatch.running = false;
                return;
            }
            dispatch.queued_keys.erase (dispatch_key_from_info (info));
            dispatch.active_info = info;
            dispatch.active_info_valid = true;
        }

        zlink_spot_dispatch_event_handler_fn handler = NULL;
        void *userdata = NULL;
        void *owner = NULL;
        {
            std::lock_guard<std::mutex> lock (state_->mutex);
            handler = dispatch.handler;
            userdata = dispatch.handler_userdata;
            owner = state_->owner;
        }

        if (!handler || !owner) {
            std::lock_guard<std::mutex> dispatch_lock (dispatch.mutex);
            dispatch.subscribe_pending.clear ();
            dispatch.routed_pending.clear ();
            dispatch.channel_reply_pending.clear ();
            dispatch.timer_pending.clear ();
            dispatch.queued_keys.clear ();
            dispatch.rearm_keys.clear ();
            dispatch.active_info_valid = false;
            dispatch.running = false;
            return;
        }

        const zlink::spot_dispatch_event_callback_context_t dispatch_scope (
          owner);
        handler (owner, &info, userdata);

        {
            std::lock_guard<std::mutex> dispatch_lock (dispatch.mutex);
            const dispatch_key_t key = dispatch_key_from_info (info);
            dispatch.active_info_valid = false;
            if (dispatch.rearm_keys.erase (key) != 0
                && dispatch.queued_keys.count (key) == 0) {
                std::deque<zlink_spot_dispatch_info_t> *queue =
                  dispatch_queue_for_event (&dispatch, info.event);
                if (queue) {
                    queue->push_back (info);
                    dispatch.queued_keys.insert (key);
                }
            }
            if (!dispatch_has_pending (dispatch)) {
                dispatch.running = false;
                return;
            }
        }
    }
}

uint32_t dispatch_runtime_key (void *spot_)
{
    const uintptr_t value = reinterpret_cast<uintptr_t> (spot_);
    return static_cast<uint32_t> (value ^ (value >> 32));
}

void spot_dispatch_event_task_main (void *arg_)
{
    if (!arg_)
        return;

    std::shared_ptr<spot_request_reply_state_t> state =
      zlink::spot_reqrep_internal::try_find_spot_state (arg_);
    if (!state)
        return;

    (void)
      zlink::spot_reqrep_internal::drain_attached_channel_reply_bridge_progress (
        state);
    run_pending_spot_dispatch_events (state);
}

void refresh_spot_identity_index (
  spot_handle_t *spot_,
  const std::shared_ptr<spot_request_reply_state_t> &state_)
{
    if (!spot_ || !state_)
        return;

    routing_pair_t identity;
    if (!resolve_spot_identity (spot_, &identity))
        return;

    std::lock_guard<std::mutex> lock (g_spot_request_reply_index_mutex);
    g_spot_state_identity_index[identity.node_rid][identity.spot_rid] = state_;
}
}

std::shared_ptr<zlink::spot_reqrep_internal::spot_request_reply_state_t>
zlink::spot_reqrep_internal::try_find_spot_state (void *spot_)
{
    spot_handle_t *spot = as_spot_handle (spot_);
    if (!spot)
        return std::shared_ptr<spot_request_reply_state_t> ();
    if (spot->mode == ZLINK_SPOT_NODE_MODE_PUBSUB) {
        errno = ENOTSUP;
        return std::shared_ptr<spot_request_reply_state_t> ();
    }

    if (spot->request_reply_state)
        return spot->request_reply_state;

    std::lock_guard<std::mutex> lock (g_spot_request_reply_index_mutex);
    std::unordered_map<void *, std::shared_ptr<spot_request_reply_state_t> >::iterator
      it = spot_owner_states ().find (spot_);
    return it != spot_owner_states ().end ()
             ? it->second
             : std::shared_ptr<spot_request_reply_state_t> ();
}

void zlink::spot_reqrep_internal::erase_spot_owner_state (void *spot_)
{
    spot_handle_t *spot = as_spot_handle (spot_);
    if (!spot)
        return;

    spot->request_reply_state.reset ();
    std::lock_guard<std::mutex> lock (g_spot_request_reply_index_mutex);
    spot_owner_states ().erase (spot_);
}

int zlink::spot_reqrep_internal::install_spot_dispatch_event_task (
  spot_request_reply_state_t *state_)
{
    if (!state_ || !state_->owner) {
        errno = EFAULT;
        return -1;
    }

    zlink::ctx_t *ctx = resolve_spot_ctx (state_->owner);
    if (!ctx)
        return -1;

    zlink::service_control_runtime_t *runtime =
      ctx->spot_worker_runtime_for_key (dispatch_runtime_key (state_->owner));
    if (!runtime) {
        errno = ETERM;
        return -1;
    }

    const uint64_t task_id = runtime->add_periodic_task (
      &spot_dispatch_event_task_main, state_->owner, 10u, true);
    if (task_id == 0)
        return -1;

    state_->dispatch.runtime = runtime;
    state_->dispatch.task_id = task_id;
    return 0;
}

void zlink::spot_reqrep_internal::maybe_dispatch_spot_info (
  spot_request_reply_state_t *state_,
  zlink_spot_dispatch_event_t event_,
  zlink_spot_dispatch_subject_kind_t subject_kind_,
  void *subject_)
{
    if (!state_)
        return;

    {
        std::lock_guard<std::mutex> lock (state_->mutex);
        if (!state_->dispatch.handler)
            return;
    }

    zlink_spot_dispatch_info_t info;
    memset (&info, 0, sizeof (info));
    info.event = event_;
    info.subject_kind = subject_kind_;
    info.subject = subject_;

    bool should_run = false;
    {
        std::lock_guard<std::mutex> dispatch_lock (state_->dispatch.mutex);
        const dispatch_key_t key = dispatch_key_from_info (info);
        if (state_->dispatch.active_info_valid
            && dispatch_key_from_info (state_->dispatch.active_info) == key) {
            state_->dispatch.rearm_keys.insert (key);
        } else if (state_->dispatch.queued_keys.count (key) == 0
                   && state_->dispatch.rearm_keys.count (key) == 0) {
            std::deque<zlink_spot_dispatch_info_t> *queue =
              dispatch_queue_for_event (&state_->dispatch, event_);
            if (queue) {
                queue->push_back (info);
                state_->dispatch.queued_keys.insert (key);
            }
        }
        if (!state_->dispatch.running) {
            state_->dispatch.running = true;
            should_run = true;
        }
    }

    if (should_run) {
        zlink::service_control_runtime_t *runtime = NULL;
        uint64_t task_id = 0;
        {
            std::lock_guard<std::mutex> lock (state_->mutex);
            runtime = state_->dispatch.runtime;
            task_id = state_->dispatch.task_id;
        }

        if (!runtime || task_id == 0 || runtime->wakeup_task (task_id) != 0) {
            std::lock_guard<std::mutex> dispatch_lock (state_->dispatch.mutex);
            state_->dispatch.running = false;
        }
    }
}

std::shared_ptr<zlink::spot_reqrep_internal::spot_request_reply_state_t>
zlink::spot_reqrep_internal::find_or_create_spot_state (void *spot_)
{
    spot_handle_t *spot = as_spot_handle (spot_);
    if (!spot)
        return std::shared_ptr<spot_request_reply_state_t> ();
    if (spot->mode == ZLINK_SPOT_NODE_MODE_PUBSUB) {
        errno = ENOTSUP;
        return std::shared_ptr<spot_request_reply_state_t> ();
    }

    std::shared_ptr<spot_request_reply_state_t> state;
    if (spot->request_reply_state)
        state = spot->request_reply_state;
    {
        std::lock_guard<std::mutex> lock (g_spot_request_reply_index_mutex);
        std::unordered_map<void *, std::shared_ptr<spot_request_reply_state_t> >::iterator
          it = spot_owner_states ().find (spot_);
        if (!state && it != spot_owner_states ().end ())
            state = it->second;
        if (!state) {
            state.reset (new spot_request_reply_state_t (spot_));
            spot_owner_states ()[spot_] = state;
        }
    }
    if (!state)
        state.reset (new spot_request_reply_state_t (spot_));
    spot->request_reply_state = state;

    refresh_spot_identity_index (spot, state);
    return state;
}

std::shared_ptr<zlink::spot_reqrep_internal::router_spot_request_reply_state_t>
zlink::spot_reqrep_internal::find_or_create_router_state (void *router_)
{
    const socket_handle_t handle = as_socket_handle (router_);
    if (!handle.socket)
        return std::shared_ptr<router_spot_request_reply_state_t> ();

    std::shared_ptr<router_spot_request_reply_state_t> state =
      handle.socket->router_spot_request_reply_state ();
    if (!state) {
        state.reset (new router_spot_request_reply_state_t (router_));
        handle.socket->set_router_spot_request_reply_state (state);
    }
    return state;
}
