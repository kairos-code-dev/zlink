/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include <chrono>
#include <condition_variable>
#include <memory>
#include <thread>
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
}

void zlink::spot_reqrep_internal::unregister_spot_channel_reply_observers (
  const std::shared_ptr<spot_request_reply_state_t> &state_)
{
    if (!state_)
        return;

    std::vector<void *> dealers;
    snapshot_spot_channel_reply_dealers (state_, &dealers);

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

void zlink::spot_reqrep_internal::clear_spot_channel_reply_sources (
  const std::shared_ptr<spot_request_reply_state_t> &state_,
  std::vector<std::shared_ptr<spot_channel_reply_source_t> > *sources_out_)
{
    if (!state_)
        return;

    std::lock_guard<std::mutex> lock (state_->mutex);
    if (sources_out_) {
        sources_out_->clear ();
        for (std::map<void *, std::shared_ptr<spot_channel_reply_source_t> >::const_iterator
               it = state_->completion_state.channel_reply_sources.begin ();
             it != state_->completion_state.channel_reply_sources.end (); ++it) {
            sources_out_->push_back (it->second);
        }
    }
    state_->completion_state.channel_reply_sources.clear ();
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
    return !state_->requests.pending_replies.empty ()
           || state_->completion_state.pending_channel_requests > 0;
}

bool zlink::spot_reqrep_internal::has_pending_router_spot_request_work (
  const std::shared_ptr<router_spot_request_reply_state_t> &state_)
{
    if (!state_)
        return false;
    if (has_router_reply_completions (state_))
        return true;
    std::lock_guard<std::mutex> lock (state_->mutex);
    return !state_->requests.pending_replies.empty ();
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
        for (std::unordered_map<
               pending_spot_key_t,
               pending_reply_t,
               pending_spot_key_hash_t>::iterator it =
               state->requests.pending_replies.begin ();
             it != state->requests.pending_replies.end (); ++it) {
            pending.push_back (it->second);
        }
        state->requests.pending_replies.clear ();
        state->requests.pending_sequences.clear ();
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
    snapshot_spot_channel_reply_dealers (state, &dealers);

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
      handle.socket->router_spot_request_reply_state ();
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
        if (queue_router_reply_completion (state, pending[i].handler,
                                           pending[i].userdata, ETERM, NULL,
                                           0)
            != 0) {
            return -1;
        }
    }

    return drain_router_reply_completions (state, router_);
}
