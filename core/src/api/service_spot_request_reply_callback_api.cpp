/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "api/part_helper_internal.hpp"
#include "api/recv_result_internal.hpp"
#include "api/service_mode_internal.hpp"
#include "api/service_api_internal.hpp"
#include "api/service_spot_dispatch_context_internal.hpp"
#include "api/service_spot_request_reply_internal.hpp"
#include "api/socket_api_internal.hpp"
#include "api/handler_result_internal.hpp"
#include "services/control/service_control_runtime.hpp"
#include "services/spot/spot_node_access.hpp"

namespace
{
using zlink::spot_reqrep_internal::close_spot_subscribe_dispatch_queue;
using zlink::spot_reqrep_internal::erase_spot_owner_state;
using zlink::spot_reqrep_internal::find_or_create_spot_state;
using zlink::spot_reqrep_internal::install_spot_dispatch_event_task;
using zlink::spot_reqrep_internal::spot_request_reply_state_t;
using zlink::spot_reqrep_internal::try_find_spot_state;
using zlink::spot_reqrep_internal::router_spot_request_reply_state_t;
using zlink::spot_reqrep_internal::find_or_create_router_state;
using zlink::spot_reqrep_internal::g_router_state_identity_index;
using zlink::spot_reqrep_internal::g_spot_request_reply_index_mutex;
using zlink::spot_reqrep_internal::g_spot_state_identity_index;
using zlink::spot_reqrep_internal::router_state_identity_index_t;
using zlink::spot_reqrep_internal::spot_state_identity_index_t;
using zlink::spot_reqrep_internal::spot_state_spot_index_t;

zlink_recv_result_t spot_recv_impl (void *spot_,
                                    const zlink_routing_id_t **source_rid_out_,
                                    const zlink_routing_id_t **spot_rid_out_,
                                    uint64_t *request_seq_out_,
                                    zlink_msg_t **parts_out_,
                                    size_t *part_count_out_,
                                    zlink_recv_flags_t flags_)
{
    if (!source_rid_out_ || !spot_rid_out_ || !request_seq_out_ || !parts_out_
        || !part_count_out_) {
        errno = EFAULT;
        return ZLINK_RECV_INVALID_HANDLE;
    }
    if (validate_recv_flags (flags_) != 0)
        return ZLINK_RECV_NOT_SUPPORTED;
    if (!as_spot_handle (spot_)) {
        errno = EFAULT;
        return ZLINK_RECV_INVALID_HANDLE;
    }
    if (spot_require_recv_model (as_spot_handle (spot_)) != 0)
        return ZLINK_RECV_BUSY;

    std::shared_ptr<spot_request_reply_state_t> state =
      find_or_create_spot_state (spot_);
    if (!state)
        return zlink::recv_result_internal::from_errno (errno);
    if (zlink::spot_reqrep_internal::ensure_spot_recv_ready (state) != 0)
        return zlink::recv_result_internal::from_errno (errno);
    std::unique_lock<std::mutex> lock (state->mutex);
    if (state->recv.request_handler
        || (state->dispatch.handler
            && !in_spot_dispatch_event_callback (spot_))) {
        errno = EBUSY;
        return ZLINK_RECV_BUSY;
    }
    lock.unlock ();

    const zlink_recv_flags_t try_flags =
      static_cast<zlink_recv_flags_t> (flags_ | ZLINK_DONTWAIT);
    const bool blocking = (flags_ & ZLINK_DONTWAIT) == 0;
    while (true) {
        (void) zlink::spot_reqrep_internal::drain_spot_reply_completions (
          state, spot_);
        const int recv_rc = zlink::spot_reqrep_internal::recv_internal_spot_queue (
          state.get (), source_rid_out_, spot_rid_out_, request_seq_out_,
          parts_out_, part_count_out_, try_flags);
        if (recv_rc == 0)
            return ZLINK_RECV_OK;
        if (!blocking || errno != EAGAIN)
            return zlink::recv_result_internal::from_rc (recv_rc);

        bool input_ready = false;
        bool signal_ready = false;
        const int wait_rc = zlink::request_completion::wait_input_or_signal (
          zlink::spot_reqrep_internal::spot_routed_recv_socket (state),
          zlink::spot_reqrep_internal::spot_completion_signal_socket (state),
          -1, &input_ready, &signal_ready);
        if (wait_rc <= 0) {
            if (wait_rc == 0)
                errno = EAGAIN;
            return zlink::recv_result_internal::from_errno (errno);
        }
        if (signal_ready)
            (void) zlink::spot_reqrep_internal::drain_spot_reply_completions (
              state, spot_);
    }
}
}

zlink_handler_result_t zlink_spot_handler (void *spot_,
                                           zlink_spot_handler_fn handler_,
                                           void *userdata_)
{
    if (!handler_) {
        errno = EINVAL;
        return ZLINK_HANDLER_INVALID_ARGUMENT;
    }

    if (!as_spot_handle (spot_)) {
        errno = EFAULT;
        return ZLINK_HANDLER_INVALID_ARGUMENT;
    }
    if (spot_transition_to_callback_mode (as_spot_handle (spot_)) != 0)
        return zlink::handler_result_internal::from_rc (-1);

    std::shared_ptr<spot_request_reply_state_t> state =
      find_or_create_spot_state (spot_);
    std::lock_guard<std::mutex> lock (state->mutex);
    if (state->recv.request_handler || state->dispatch.handler) {
        spot_revert_callback_transition (as_spot_handle (spot_));
        errno = EBUSY;
        return ZLINK_HANDLER_BUSY;
    }

    state->recv.request_handler = handler_;
    state->recv.request_handler_userdata = userdata_;
    return ZLINK_HANDLER_OK;
}

zlink_handler_result_t zlink_spot_dispatch_event_handler (
  void *spot_,
  zlink_spot_dispatch_event_handler_fn handler_,
  void *userdata_)
{
    if (!handler_) {
        errno = EINVAL;
        return ZLINK_HANDLER_INVALID_ARGUMENT;
    }

    if (!as_spot_handle (spot_)) {
        errno = EFAULT;
        return ZLINK_HANDLER_INVALID_ARGUMENT;
    }
    if (spot_transition_to_callback_mode (as_spot_handle (spot_)) != 0)
        return zlink::handler_result_internal::from_rc (-1);

    std::shared_ptr<spot_request_reply_state_t> state =
      find_or_create_spot_state (spot_);
    {
        std::lock_guard<std::mutex> lock (state->mutex);
        if (state->recv.request_handler || state->dispatch.handler) {
            spot_revert_callback_transition (as_spot_handle (spot_));
            errno = EBUSY;
            return ZLINK_HANDLER_BUSY;
        }

        state->dispatch.handler = handler_;
        state->dispatch.handler_userdata = userdata_;
        if (install_spot_dispatch_event_task (state.get ()) != 0) {
            state->dispatch.handler = NULL;
            state->dispatch.handler_userdata = NULL;
            spot_revert_callback_transition (as_spot_handle (spot_));
            return zlink::handler_result_internal::from_rc (-1);
        }
    }

    if (spot_install_dispatch_event_sub_handler (as_spot_handle (spot_)) != 0) {
        zlink::service_control_runtime_t *dispatch_runtime = NULL;
        uint64_t dispatch_task_id = 0;
        {
            std::lock_guard<std::mutex> lock (state->mutex);
            state->dispatch.handler = NULL;
            state->dispatch.handler_userdata = NULL;
            dispatch_runtime = state->dispatch.runtime;
            dispatch_task_id = state->dispatch.task_id;
            state->dispatch.runtime = NULL;
            state->dispatch.task_id = 0;
        }
        if (dispatch_runtime && dispatch_task_id != 0)
            (void) dispatch_runtime->remove_task (dispatch_task_id);
        spot_revert_callback_transition (as_spot_handle (spot_));
        return zlink::handler_result_internal::from_rc (-1);
    }
    return ZLINK_HANDLER_OK;
}

zlink_recv_result_t zlink_spot_recv_part (
  void *spot_,
  const zlink_routing_id_t **source_node_rid_out_,
  const zlink_routing_id_t **source_spot_rid_out_,
  uint64_t *request_seq_out_,
  zlink_msg_t *part_out_,
  zlink_part_flag_t *has_more_out_,
  zlink_recv_flags_t flags_)
{
    if (!spot_ || !source_node_rid_out_ || !source_spot_rid_out_
        || !request_seq_out_ || !part_out_ || !has_more_out_) {
        errno = EFAULT;
        return zlink::recv_result_internal::from_errno (errno);
    }
    if (validate_recv_flags (flags_) != 0)
        return zlink::recv_result_internal::from_errno (errno);
    if (!as_spot_handle (spot_))
        return zlink::recv_result_internal::from_errno (EFAULT);

    std::shared_ptr<zlink::part_helper_internal::handle_state_t> helper_state =
      zlink::part_helper_internal::find_or_create_handle_state (spot_);
    if (!helper_state)
        return zlink::recv_result_internal::from_errno (errno);

    bool first_part = false;
    zlink::socket_base_t *source_socket = NULL;
    if (zlink::part_helper_internal::prepare_recv_step (
          spot_, zlink::part_helper_internal::recv_family_spot, source_socket,
          &helper_state, &first_part, &source_socket)
        != 0) {
        return zlink::recv_result_internal::from_errno (errno);
    }

    if (first_part) {
        const zlink_routing_id_t *source_node_rid = NULL;
        const zlink_routing_id_t *source_spot_rid = NULL;
        uint64_t request_seq = 0;
        zlink_msg_t *parts = NULL;
        size_t part_count = 0;
        if (spot_recv_impl (spot_, &source_node_rid, &source_spot_rid,
                            &request_seq, &parts, &part_count, flags_)
            != ZLINK_RECV_OK) {
            zlink::part_helper_internal::abort_recv_step (helper_state);
            return zlink::recv_result_internal::from_errno (errno);
        }

        bool move_failed = false;
        {
            std::lock_guard<std::mutex> lock (helper_state->mutex);
            helper_state->recv.return_source_rid_as_null = source_node_rid == NULL;
            helper_state->recv.return_source_spot_rid_as_null =
              source_spot_rid == NULL;
            zlink::part_helper_internal::copy_routing_id (
              source_node_rid, &helper_state->recv.source_node_rid);
            zlink::part_helper_internal::copy_routing_id (
              source_spot_rid, &helper_state->recv.source_spot_rid);
            helper_state->recv.request_seq = request_seq;
            helper_state->recv.buffered_parts.resize (part_count);
            helper_state->recv.next_part_index = 0;
            for (size_t i = 0; i < part_count; ++i) {
                zlink_msg_init (&helper_state->recv.buffered_parts[i]);
                if (zlink_msg_move (&helper_state->recv.buffered_parts[i],
                                    &parts[i])
                    != 0) {
                    move_failed = true;
                    break;
                }
            }
        }
        zlink_multipart_close (parts, part_count);
        if (move_failed) {
            zlink::part_helper_internal::abort_recv_step (helper_state);
            errno = EFAULT;
            return zlink::recv_result_internal::from_errno (errno);
        }
        if (helper_state->recv.buffered_parts.empty ()) {
            zlink::part_helper_internal::abort_recv_step (helper_state);
            errno = EPROTO;
            return zlink::recv_result_internal::from_errno (errno);
        }
        if (zlink_msg_move (part_out_, &helper_state->recv.buffered_parts[0]) != 0) {
            zlink::part_helper_internal::abort_recv_step (helper_state);
            errno = EFAULT;
            return zlink::recv_result_internal::from_errno (errno);
        }
        helper_state->recv.next_part_index = 1;
    } else {
        bool range_failed = false;
        bool move_failed = false;
        {
            std::lock_guard<std::mutex> lock (helper_state->mutex);
            if (helper_state->recv.next_part_index
                >= helper_state->recv.buffered_parts.size ()) {
                range_failed = true;
            } else {
                move_failed =
                  zlink_msg_move (
                    part_out_,
                    &helper_state
                       ->recv.buffered_parts[helper_state->recv.next_part_index])
                  != 0;
                if (!move_failed)
                    ++helper_state->recv.next_part_index;
            }
        }
        if (range_failed) {
            zlink::part_helper_internal::abort_recv_step (helper_state);
            errno = EPROTO;
            return zlink::recv_result_internal::from_errno (errno);
        }
        if (move_failed) {
            zlink::part_helper_internal::abort_recv_step (helper_state);
            errno = EFAULT;
            return zlink::recv_result_internal::from_errno (errno);
        }
    }

    {
        std::lock_guard<std::mutex> lock (helper_state->mutex);
        *source_node_rid_out_ =
          helper_state->recv.return_source_rid_as_null
            ? NULL
            : &helper_state->recv.source_node_rid;
        *source_spot_rid_out_ =
          helper_state->recv.return_source_spot_rid_as_null
            ? NULL
            : &helper_state->recv.source_spot_rid;
        *request_seq_out_ = helper_state->recv.request_seq;
        *has_more_out_ =
          (helper_state->recv.next_part_index
           < helper_state->recv.buffered_parts.size ())
            ? ZLINK_PART_MORE : ZLINK_PART_FINAL;
    }
    zlink::part_helper_internal::complete_recv_step (helper_state,
                                                     *has_more_out_);
    return ZLINK_RECV_OK;
}

extern "C" int zlink_spot_request_reply_set_default_timeout (
  void *spot_,
  const void *optval_,
  size_t optvallen_)
{
    if (!as_spot_handle (spot_)) {
        errno = EINVAL;
        return -1;
    }
    if (!optval_ || optvallen_ != sizeof (int)) {
        errno = EINVAL;
        return -1;
    }

    int timeout_ms = 0;
    memcpy (&timeout_ms, optval_, sizeof (timeout_ms));
    if (timeout_ms < 0) {
        errno = EINVAL;
        return -1;
    }

    std::shared_ptr<spot_request_reply_state_t> state =
      find_or_create_spot_state (spot_);
    std::lock_guard<std::mutex> lock (state->mutex);
    state->requests.default_timeout_ms = static_cast<uint32_t> (timeout_ms);
    return 0;
}

extern "C" int zlink_spot_request_reply_get_default_timeout (
  void *spot_,
  void *optval_,
  size_t *optvallen_)
{
    if (!as_spot_handle (spot_)) {
        errno = EINVAL;
        return -1;
    }
    if (!optval_ || !optvallen_ || *optvallen_ < sizeof (int)) {
        errno = EINVAL;
        return -1;
    }

    std::shared_ptr<spot_request_reply_state_t> state =
      find_or_create_spot_state (spot_);
    int timeout_ms = 0;
    {
        std::lock_guard<std::mutex> lock (state->mutex);
        timeout_ms = static_cast<int> (state->requests.default_timeout_ms);
    }

    memcpy (optval_, &timeout_ms, sizeof (timeout_ms));
    *optvallen_ = sizeof (timeout_ms);
    return 0;
}

extern "C" void zlink_spot_request_reply_cleanup_spot (void *spot_)
{
    spot_handle_t *spot = as_spot_handle (spot_);
    if (!spot)
        return;

    zlink::service_control_runtime_t *dispatch_runtime = NULL;
    uint64_t dispatch_task_id = 0;
    std::shared_ptr<spot_request_reply_state_t> state =
      try_find_spot_state (spot);
    zlink::internal_pair_queue::queue_t routed_recv_signal;
    if (state)
        (void) zlink::spot_reqrep_internal::drain_close_spot_request_reply_state (
          spot_);
    if (state) {
        {
            std::lock_guard<std::mutex> dispatch_lock (state->dispatch.mutex);
            state->dispatch.subscribe_pending.clear ();
            state->dispatch.routed_pending.clear ();
            state->dispatch.channel_reply_pending.clear ();
            state->dispatch.timer_pending.clear ();
            state->dispatch.queued_keys.clear ();
            state->dispatch.rearm_keys.clear ();
            state->dispatch.active_info_valid = false;
            state->dispatch.running = false;
        }

        std::lock_guard<std::mutex> state_lock (state->mutex);
        state->recv.request_handler = NULL;
        state->recv.request_handler_userdata = NULL;
        state->dispatch.handler = NULL;
        state->dispatch.handler_userdata = NULL;
        dispatch_runtime = state->dispatch.runtime;
        dispatch_task_id = state->dispatch.task_id;
        state->dispatch.runtime = NULL;
        state->dispatch.task_id = 0;
    }
    zlink::spot_reqrep_internal::close_spot_routed_recv_state (
      state, &routed_recv_signal);
    if (routed_recv_signal.rx || routed_recv_signal.tx) {
        if (routed_recv_signal.rx)
            zlink::spot_node_access_t::untrack_owned_socket (
              spot->node, routed_recv_signal.rx);
        if (routed_recv_signal.tx)
            zlink::spot_node_access_t::untrack_owned_socket (
              spot->node, routed_recv_signal.tx);
        zlink::internal_pair_queue::close (&routed_recv_signal);
    }
    if (dispatch_runtime && dispatch_task_id != 0)
        (void) dispatch_runtime->remove_task (dispatch_task_id);
    if (state) {
        zlink::spot_reqrep_internal::unregister_spot_channel_reply_observers (
          state);
        std::vector<std::shared_ptr<
          zlink::spot_reqrep_internal::spot_channel_reply_source_t> > sources;
        zlink::spot_reqrep_internal::clear_spot_channel_reply_sources (
          state, &sources);
        std::lock_guard<std::mutex> state_lock (state->mutex);
        close_spot_subscribe_dispatch_queue (&state->recv.subscribe_queue);
        zlink::request_completion::close (&state->completion_state.direct);
        for (size_t i = 0; i < sources.size (); ++i) {
            if (sources[i])
                zlink::request_completion::close (&sources[i]->completion);
        }
    }
    erase_spot_owner_state (spot_);
    std::lock_guard<std::mutex> lock (g_spot_request_reply_index_mutex);
    for (spot_state_identity_index_t::iterator it =
           g_spot_state_identity_index.begin ();
         it != g_spot_state_identity_index.end ();) {
        spot_state_spot_index_t &spot_index = it->second;
        for (spot_state_spot_index_t::iterator spot_it = spot_index.begin ();
             spot_it != spot_index.end ();) {
            std::shared_ptr<spot_request_reply_state_t> indexed =
              spot_it->second.lock ();
            if (!indexed || indexed == state)
                spot_it = spot_index.erase (spot_it);
            else
                ++spot_it;
        }
        if (spot_index.empty ())
            it = g_spot_state_identity_index.erase (it);
        else
            ++it;
    }
}

extern "C" void zlink_spot_request_reply_cleanup_router (void *router_)
{
    const socket_handle_t handle = as_socket_handle (router_);
    if (!handle.socket)
        return;

    std::shared_ptr<router_spot_request_reply_state_t> state =
      std::static_pointer_cast<router_spot_request_reply_state_t> (
        handle.socket->router_spot_request_reply_state ());
    if (state)
        (void)
          zlink::spot_reqrep_internal::drain_close_router_spot_request_reply_state (
            router_);
    if (state) {
        std::lock_guard<std::mutex> state_lock (state->mutex);
        zlink::request_completion::close (&state->completion);
    }
    handle.socket->clear_router_spot_request_reply_state ();
    std::lock_guard<std::mutex> lock (g_spot_request_reply_index_mutex);
    for (router_state_identity_index_t::iterator it =
           g_router_state_identity_index.begin ();
         it != g_router_state_identity_index.end ();) {
        std::shared_ptr<router_spot_request_reply_state_t> indexed =
          it->second.lock ();
        if (!indexed || indexed == state)
            it = g_router_state_identity_index.erase (it);
        else
            ++it;
    }
}
