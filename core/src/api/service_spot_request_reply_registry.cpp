/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "api/service_api_internal.hpp"
#include "api/service_spot_dispatch_context_internal.hpp"
#include "api/service_spot_request_reply_internal.hpp"
#include "api/socket_api_internal.hpp"
#include "core/ctx.hpp"
#include "services/control/service_control_runtime.hpp"
#include "services/spot/spot_node_access.hpp"
#include "services/spot/spot_runtime.hpp"
#include "services/spot/spot_pub.hpp"

namespace
{
using zlink::spot_reqrep_internal::g_router_state_identity_index;
using zlink::spot_reqrep_internal::g_spot_request_reply_index_mutex;
using zlink::spot_reqrep_internal::g_spot_state_identity_index;
using zlink::spot_reqrep_internal::make_spot_identity_key;
using zlink::spot_reqrep_internal::router_spot_request_reply_state_t;
using zlink::spot_reqrep_internal::router_state_identity_index_t;
using zlink::spot_reqrep_internal::spot_request_reply_state_t;
using zlink::spot_reqrep_internal::spot_state_identity_index_t;

struct routing_pair_t
{
    std::string node_rid;
    std::string spot_rid;
};

std::unordered_map<void *, std::shared_ptr<spot_request_reply_state_t> >
  g_spot_owner_states;

zlink::ctx_t *resolve_spot_ctx (void *spot_)
{
    spot_handle_t *spot = as_spot_handle (spot_);
    if (!spot || !spot->node) {
        errno = EFAULT;
        return NULL;
    }
    return zlink::spot_node_access_t::ctx (spot->node);
}

zlink::spot_runtime_t *resolve_spot_runtime (void *spot_)
{
    spot_handle_t *spot = as_spot_handle (spot_);
    if (!spot || !spot->node) {
        errno = EFAULT;
        return NULL;
    }
    return zlink::spot_node_access_t::runtime (spot->node);
}

zlink::spot_runtime_t *resolve_active_spot_runtime (void *spot_)
{
    zlink::spot_runtime_t *runtime = resolve_spot_runtime (spot_);
    if (!runtime || !runtime->execution.data_plane_running
        || !runtime->route_ingress
        || !runtime->node_router)
        return NULL;
    return runtime;
}

bool has_valid_routing_id (const zlink_routing_id_t *peer_rid_)
{
    return peer_rid_ && peer_rid_->size > 0
           && peer_rid_->size <= sizeof (peer_rid_->data);
}

uint32_t dispatch_event_bit (zlink_spot_dispatch_event_t event_)
{
    switch (event_) {
    case ZLINK_SPOT_DISPATCH_EVENT_SUBSCRIBE_READABLE:
        return 1u << 0;
    case ZLINK_SPOT_DISPATCH_EVENT_ROUTED_READABLE:
        return 1u << 1;
    case ZLINK_SPOT_DISPATCH_EVENT_TIMER_READABLE:
        return 1u << 2;
    default:
        return 0;
    }
}

zlink_spot_dispatch_event_t next_dispatch_event (uint32_t mask_)
{
    if ((mask_ & (1u << 0)) != 0)
        return ZLINK_SPOT_DISPATCH_EVENT_SUBSCRIBE_READABLE;
    if ((mask_ & (1u << 1)) != 0)
        return ZLINK_SPOT_DISPATCH_EVENT_ROUTED_READABLE;
    return ZLINK_SPOT_DISPATCH_EVENT_TIMER_READABLE;
}

void run_pending_spot_dispatch_events (
  const std::shared_ptr<spot_request_reply_state_t> &state_)
{
    while (true) {
        zlink::spot_reqrep_internal::spot_dispatch_state_t &dispatch =
          state_->dispatch;
        zlink_spot_dispatch_event_t event =
          ZLINK_SPOT_DISPATCH_EVENT_SUBSCRIBE_READABLE;
        {
            std::lock_guard<std::mutex> dispatch_lock (dispatch.mutex);
            if (dispatch.pending_event_mask == 0) {
                dispatch.running = false;
                return;
            }
            event = next_dispatch_event (dispatch.pending_event_mask);
            dispatch.pending_event_mask &= ~dispatch_event_bit (event);
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
            dispatch.pending_event_mask = 0;
            dispatch.running = false;
            return;
        }

        const zlink::spot_dispatch_event_callback_context_t dispatch_scope (
          owner);
        handler (owner, event, userdata);
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

    run_pending_spot_dispatch_events (state);
}

std::string routing_id_key (const zlink_routing_id_t *peer_rid_)
{
    if (!has_valid_routing_id (peer_rid_))
        return std::string ();

    return std::string (reinterpret_cast<const char *> (peer_rid_->data),
                        peer_rid_->size);
}

bool resolve_spot_identity (void *spot_, routing_pair_t *out_)
{
    if (!out_) {
        errno = EFAULT;
        return false;
    }

    if (spot_handle_t *spot = as_spot_handle (spot_)) {
        zlink::service_public_api_scope_t admission (spot->public_api);
        if (!admission.acquired ())
            return false;

        zlink::spot_pub_t *spot_pub = ensure_spot_pub (spot);
        zlink::spot_pub_t *node_pub =
          spot->node ? spot->node->ensure_default_pub () : NULL;
        if (!spot_pub || !node_pub)
            return false;

        zlink_routing_id_t node_rid;
        zlink_routing_id_t spot_rid;
        memset (&node_rid, 0, sizeof (node_rid));
        memset (&spot_rid, 0, sizeof (spot_rid));
        if (node_pub->routing_id (&node_rid) != 0
            || spot_pub->routing_id (&spot_rid) != 0) {
            return false;
        }

        out_->node_rid = routing_id_key (&node_rid);
        out_->spot_rid = routing_id_key (&spot_rid);
        return !out_->node_rid.empty () && !out_->spot_rid.empty ();
    }

    errno = EFAULT;
    return false;
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
    g_spot_state_identity_index[make_spot_identity_key (identity.node_rid,
                                                        identity.spot_rid)] =
      state_;
}
}

std::shared_ptr<zlink::spot_reqrep_internal::spot_request_reply_state_t>
zlink::spot_reqrep_internal::try_find_spot_state (void *spot_)
{
    spot_handle_t *spot = as_spot_handle (spot_);
    if (!spot)
        return std::shared_ptr<spot_request_reply_state_t> ();

    std::lock_guard<std::mutex> lock (g_spot_request_reply_index_mutex);
    std::unordered_map<void *, std::shared_ptr<spot_request_reply_state_t> >::iterator it =
      g_spot_owner_states.find (spot_);
    return it != g_spot_owner_states.end ()
             ? it->second
             : std::shared_ptr<spot_request_reply_state_t> ();
}

void zlink::spot_reqrep_internal::erase_spot_owner_state (void *spot_)
{
    spot_handle_t *spot = as_spot_handle (spot_);
    if (!spot)
        return;

    std::lock_guard<std::mutex> lock (g_spot_request_reply_index_mutex);
    g_spot_owner_states.erase (spot_);
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
      &spot_dispatch_event_task_main, state_->owner, 24u * 60u * 60u * 1000u,
      false);
    if (task_id == 0)
        return -1;

    state_->dispatch.runtime = runtime;
    state_->dispatch.task_id = task_id;
    return 0;
}

void zlink::spot_reqrep_internal::maybe_dispatch_spot_event (
  spot_request_reply_state_t *state_,
  zlink_spot_dispatch_event_t event_)
{
    if (!state_)
        return;

    {
        std::lock_guard<std::mutex> lock (state_->mutex);
        if (!state_->dispatch.handler)
            return;
    }

    bool should_run = false;
    {
        std::lock_guard<std::mutex> dispatch_lock (state_->dispatch.mutex);
        state_->dispatch.pending_event_mask |= dispatch_event_bit (event_);
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

    std::shared_ptr<spot_request_reply_state_t> state;
    {
        std::lock_guard<std::mutex> lock (g_spot_request_reply_index_mutex);
        std::unordered_map<void *, std::shared_ptr<spot_request_reply_state_t> >::iterator
          it = g_spot_owner_states.find (spot_);
        if (it != g_spot_owner_states.end ())
            state = it->second;
        if (!state) {
            state.reset (new spot_request_reply_state_t (spot_));
            g_spot_owner_states[spot_] = state;
        }
    }
    if (!state)
        state.reset (new spot_request_reply_state_t (spot_));

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
      std::static_pointer_cast<router_spot_request_reply_state_t> (
        handle.socket->router_spot_request_reply_state ());
    if (!state) {
        state.reset (new router_spot_request_reply_state_t (router_));
        handle.socket->set_router_spot_request_reply_state (state);
    }
    return state;
}

std::shared_ptr<zlink::spot_reqrep_internal::spot_request_reply_state_t>
zlink::spot_reqrep_internal::find_spot_state_by_identity (
  const std::string &node_rid_,
  const std::string &spot_rid_)
{
    const std::string key = make_spot_identity_key (node_rid_, spot_rid_);
    std::lock_guard<std::mutex> lock (g_spot_request_reply_index_mutex);
    spot_state_identity_index_t::iterator it =
      g_spot_state_identity_index.find (key);
    if (it == g_spot_state_identity_index.end ())
        return std::shared_ptr<spot_request_reply_state_t> ();
    std::shared_ptr<spot_request_reply_state_t> state = it->second.lock ();
    if (!state) {
        g_spot_state_identity_index.erase (it);
        return std::shared_ptr<spot_request_reply_state_t> ();
    }
    return state;
}

std::shared_ptr<zlink::spot_reqrep_internal::router_spot_request_reply_state_t>
zlink::spot_reqrep_internal::find_router_state_by_rid (
  const std::string &router_rid_)
{
    std::lock_guard<std::mutex> lock (g_spot_request_reply_index_mutex);
    router_state_identity_index_t::iterator it =
      g_router_state_identity_index.find (router_rid_);
    if (it == g_router_state_identity_index.end ())
        return std::shared_ptr<router_spot_request_reply_state_t> ();
    std::shared_ptr<router_spot_request_reply_state_t> state = it->second.lock ();
    if (!state) {
        g_router_state_identity_index.erase (it);
        return std::shared_ptr<router_spot_request_reply_state_t> ();
    }
    return state;
}

zlink::spot_runtime_t *
zlink::spot_reqrep_internal::resolve_runtime_for_spot_destination (
  const std::string &node_rid_,
  const std::string &spot_rid_)
{
    std::shared_ptr<spot_request_reply_state_t> state =
      find_spot_state_by_identity (node_rid_, spot_rid_);
    if (!state)
        return NULL;
    return resolve_active_spot_runtime (state->owner);
}

void zlink::spot_reqrep_internal::bind_router_state_rid (
  void *router_,
  const std::string &router_rid_,
  const std::shared_ptr<router_spot_request_reply_state_t> &state_)
{
    if (!router_ || !state_)
        return;

    {
        std::lock_guard<std::mutex> state_lock (state_->mutex);
        state_->router_rid = router_rid_;
    }

    std::lock_guard<std::mutex> lock (g_spot_request_reply_index_mutex);
    g_router_state_identity_index[router_rid_] = state_;
}
