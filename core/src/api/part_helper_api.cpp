/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include <memory>
#include <mutex>
#include <new>
#include <unordered_map>

#include "api/part_helper_internal.hpp"
#include "core/msg.hpp"
#include "sockets/socket_base.hpp"
#include "utils/err.hpp"

namespace
{
std::mutex g_part_helper_mutex;
std::unordered_map<void *, std::shared_ptr<zlink::part_helper_internal::handle_state_t> >
  g_part_helper_state;
thread_local bool g_part_helper_aggregate_send_mode = false;

bool send_family_requires_routed_scope (
  zlink::part_helper_internal::send_family_t family_)
{
    using namespace zlink::part_helper_internal;
    return family_ == send_family_send_rid
           || family_ == send_family_router_request
           || family_ == send_family_dealer_request
           || family_ == send_family_router_reply
           || family_ == send_family_spot_send_router
           || family_ == send_family_spot_send_channel
           || family_ == send_family_spot_request_router
           || family_ == send_family_spot_request_channel
           || family_ == send_family_spot_reply_spot
           || family_ == send_family_spot_reply_router
           || family_ == send_family_router_request_spot
           || family_ == send_family_router_reply_spot
           || family_ == send_family_router_send_spot;
}

std::unique_ptr<zlink::socket_public_send_scope_t> create_send_scope (
  zlink::socket_base_t *sink_socket_,
  const zlink::part_helper_internal::send_sequence_spec_t &spec_)
{
    if (!sink_socket_) {
        errno = EFAULT;
        return std::unique_ptr<zlink::socket_public_send_scope_t> ();
    }

    const bool needs_sync =
      send_family_requires_routed_scope (spec_.family);
    return sink_socket_->begin_public_send_scope (needs_sync);
}
}

zlink::part_helper_internal::send_sequence_spec_t::send_sequence_spec_t () :
    family (send_family_none),
    flags (ZLINK_SEND_FLAGS_NONE),
    timeout_ms (0),
    request_seq (0),
    handler (NULL),
    userdata (NULL),
    has_rid1 (false),
    has_rid2 (false),
    has_text1 (false),
    has_text2 (false),
    request_like (false)
{
    memset (&rid1, 0, sizeof (rid1));
    memset (&rid2, 0, sizeof (rid2));
}

zlink::part_helper_internal::send_sequence_state_t::send_sequence_state_t () :
    active (false),
    sink_socket (NULL)
{
}

zlink::part_helper_internal::recv_sequence_state_t::recv_sequence_state_t () :
    active (false),
    family (recv_family_none),
    source_socket (NULL),
    owner_thread (),
    return_source_rid_as_null (true),
    return_source_spot_rid_as_null (true),
    request_seq (0),
    next_part_index (0)
{
    memset (&source_node_rid, 0, sizeof (source_node_rid));
    memset (&source_spot_rid, 0, sizeof (source_spot_rid));
}

int zlink::part_helper_internal::validate_send_flags (zlink_send_flags_t flags_)
{
    if (flags_ != 0 && flags_ != ZLINK_DONTWAIT) {
        errno = ENOTSUP;
        return -1;
    }
    return 0;
}

int zlink::part_helper_internal::validate_part_flag (zlink_part_flag_t part_flag_)
{
    if (part_flag_ != ZLINK_PART_FINAL && part_flag_ != ZLINK_PART_MORE) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

bool zlink::part_helper_internal::has_valid_routing_id (
  const zlink_routing_id_t *rid_)
{
    return rid_ && rid_->size > 0 && rid_->size <= sizeof (rid_->data);
}

bool zlink::part_helper_internal::routing_id_equals (
  const zlink_routing_id_t &lhs_,
  const zlink_routing_id_t &rhs_)
{
    return lhs_.size == rhs_.size
           && memcmp (lhs_.data, rhs_.data, lhs_.size) == 0;
}

void zlink::part_helper_internal::copy_routing_id (
  const zlink_routing_id_t *src_,
  zlink_routing_id_t *dest_)
{
    if (!dest_)
        return;

    memset (dest_, 0, sizeof (*dest_));
    if (!src_)
        return;

    dest_->size = src_->size;
    if (src_->size > 0)
        memcpy (dest_->data, src_->data, src_->size);
}

void zlink::part_helper_internal::consume_send_part (zlink_msg_t *part_)
{
    if (!part_)
        return;

    zlink::msg_t *msg = reinterpret_cast<zlink::msg_t *> (part_);
    if (!msg->check ())
        return;

    const int close_rc = msg->close ();
    errno_assert (close_rc == 0);
    const int init_rc = msg->init ();
    errno_assert (init_rc == 0);
}

bool zlink::part_helper_internal::send_spec_equals (
  const send_sequence_spec_t &lhs_,
  const send_sequence_spec_t &rhs_)
{
    if (lhs_.family != rhs_.family || lhs_.flags != rhs_.flags
        || lhs_.timeout_ms != rhs_.timeout_ms
        || lhs_.request_seq != rhs_.request_seq || lhs_.handler != rhs_.handler
        || lhs_.userdata != rhs_.userdata || lhs_.has_rid1 != rhs_.has_rid1
        || lhs_.has_rid2 != rhs_.has_rid2 || lhs_.has_text1 != rhs_.has_text1
        || lhs_.has_text2 != rhs_.has_text2
        || lhs_.request_like != rhs_.request_like) {
        return false;
    }

    if (lhs_.has_rid1 && !routing_id_equals (lhs_.rid1, rhs_.rid1))
        return false;
    if (lhs_.has_rid2 && !routing_id_equals (lhs_.rid2, rhs_.rid2))
        return false;
    if (lhs_.has_text1 && lhs_.text1 != rhs_.text1)
        return false;
    if (lhs_.has_text2 && lhs_.text2 != rhs_.text2)
        return false;

    return true;
}

std::shared_ptr<zlink::part_helper_internal::handle_state_t>
zlink::part_helper_internal::find_or_create_handle_state (void *handle_)
{
    if (!handle_) {
        errno = EFAULT;
        return std::shared_ptr<handle_state_t> ();
    }

    std::lock_guard<std::mutex> lock (g_part_helper_mutex);
    std::unordered_map<void *, std::shared_ptr<handle_state_t> >::iterator it =
      g_part_helper_state.find (handle_);
    if (it != g_part_helper_state.end ())
        return it->second;

    std::shared_ptr<handle_state_t> state (new (std::nothrow) handle_state_t ());
    if (!state) {
        errno = ENOMEM;
        return std::shared_ptr<handle_state_t> ();
    }

    g_part_helper_state[handle_] = state;
    return state;
}

std::shared_ptr<zlink::part_helper_internal::handle_state_t>
zlink::part_helper_internal::find_handle_state (void *handle_)
{
    std::lock_guard<std::mutex> lock (g_part_helper_mutex);
    std::unordered_map<void *, std::shared_ptr<handle_state_t> >::iterator it =
      g_part_helper_state.find (handle_);
    return it != g_part_helper_state.end () ? it->second
                                            : std::shared_ptr<handle_state_t> ();
}

void zlink::part_helper_internal::reset_send_sequence (send_sequence_state_t *state_)
{
    if (!state_)
        return;

    for (size_t i = 0; i < state_->buffered_parts.size (); ++i)
        zlink_msg_close (&state_->buffered_parts[i]);
    state_->buffered_parts.clear ();

    state_->spec = send_sequence_spec_t ();
    state_->send_scope.reset ();
    state_->active = false;
    state_->sink_socket = NULL;
    state_->owner_thread = std::thread::id ();
}

void zlink::part_helper_internal::reset_recv_sequence (recv_sequence_state_t *state_)
{
    if (!state_)
        return;

    for (size_t i = 0; i < state_->buffered_parts.size (); ++i)
        zlink_msg_close (&state_->buffered_parts[i]);
    state_->buffered_parts.clear ();
    state_->next_part_index = 0;

    state_->active = false;
    state_->family = recv_family_none;
    state_->source_socket = NULL;
    state_->owner_thread = std::thread::id ();
}

bool zlink::part_helper_internal::aggregate_send_mode_active ()
{
    return g_part_helper_aggregate_send_mode;
}

void zlink::part_helper_internal::set_aggregate_send_mode (bool active_)
{
    g_part_helper_aggregate_send_mode = active_;
}

int zlink::part_helper_internal::prepare_send_step (
  void *handle_,
  const send_sequence_spec_t &spec_,
  zlink::socket_base_t *sink_socket_,
  std::shared_ptr<handle_state_t> *state_out_,
  bool *first_part_out_)
{
    if (!state_out_ || !first_part_out_) {
        errno = EFAULT;
        return -1;
    }

    std::shared_ptr<handle_state_t> state = find_or_create_handle_state (handle_);
    if (!state)
        return -1;

    std::unique_lock<std::mutex> lock (state->mutex);
    const std::thread::id current_thread = std::this_thread::get_id ();
    while (state->send.active && state->send.owner_thread != current_thread) {
        if (!aggregate_send_mode_active ()) {
            errno = EINVAL;
            return -1;
        }
        state->cv.wait (lock);
    }

    if (!state->send.active) {
        std::unique_ptr<zlink::socket_public_send_scope_t> send_scope =
          create_send_scope (sink_socket_, spec_);
        if (!send_scope)
            return -1;

        state->send.active = true;
        state->send.spec = spec_;
        state->send.sink_socket = sink_socket_;
        state->send.send_scope = std::move (send_scope);
        state->send.owner_thread = current_thread;
        *first_part_out_ = true;
    } else {
        if (!send_spec_equals (state->send.spec, spec_)) {
            errno = EINVAL;
            return -1;
        }
        *first_part_out_ = false;
    }

    *state_out_ = state;
    return 0;
}

int zlink::part_helper_internal::prepare_recv_step (
  void *handle_,
  recv_family_t family_,
  zlink::socket_base_t *source_socket_,
  std::shared_ptr<handle_state_t> *state_out_,
  bool *first_part_out_,
  zlink::socket_base_t **active_source_socket_out_)
{
    if (!state_out_ || !first_part_out_ || !active_source_socket_out_) {
        errno = EFAULT;
        return -1;
    }

    std::shared_ptr<handle_state_t> state = find_or_create_handle_state (handle_);
    if (!state)
        return -1;

    std::lock_guard<std::mutex> lock (state->mutex);
    const std::thread::id current_thread = std::this_thread::get_id ();

    if (!state->recv.active) {
        state->recv.active = true;
        state->recv.family = family_;
        state->recv.source_socket = source_socket_;
        state->recv.owner_thread = current_thread;
        state->recv.return_source_rid_as_null = true;
        state->recv.return_source_spot_rid_as_null = true;
        state->recv.request_seq = 0;
        state->recv.service_name.clear ();
        state->recv.topic_id.clear ();
        memset (&state->recv.source_node_rid, 0, sizeof (state->recv.source_node_rid));
        memset (&state->recv.source_spot_rid, 0, sizeof (state->recv.source_spot_rid));
        *first_part_out_ = true;
    } else {
        if (state->recv.family != family_
            || state->recv.owner_thread != current_thread) {
            errno = EINVAL;
            return -1;
        }
        *first_part_out_ = false;
    }

    *active_source_socket_out_ = state->recv.source_socket;
    *state_out_ = state;
    return 0;
}

void zlink::part_helper_internal::complete_send_step (
  const std::shared_ptr<handle_state_t> &state_,
  zlink_part_flag_t part_flag_)
{
    if (!state_ || part_flag_ != ZLINK_PART_FINAL)
        return;

    std::lock_guard<std::mutex> lock (state_->mutex);
    reset_send_sequence (&state_->send);
    state_->cv.notify_all ();
}

void zlink::part_helper_internal::complete_recv_step (
  const std::shared_ptr<handle_state_t> &state_,
  int has_more_)
{
    if (!state_ || has_more_ != 0)
        return;

    std::lock_guard<std::mutex> lock (state_->mutex);
    reset_recv_sequence (&state_->recv);
}

void zlink::part_helper_internal::abort_send_step (
  const std::shared_ptr<handle_state_t> &state_)
{
    if (!state_)
        return;

    std::lock_guard<std::mutex> lock (state_->mutex);
    if (state_->send.sink_socket && state_->send.send_scope)
        (void) state_->send.sink_socket->rollback_scoped (
          *state_->send.send_scope);
    reset_send_sequence (&state_->send);
    state_->cv.notify_all ();
}

void zlink::part_helper_internal::abort_recv_step (
  const std::shared_ptr<handle_state_t> &state_)
{
    if (!state_)
        return;

    std::lock_guard<std::mutex> lock (state_->mutex);
    reset_recv_sequence (&state_->recv);
}

int zlink::part_helper_internal::reject_if_send_sequence_open (void *handle_)
{
    std::shared_ptr<handle_state_t> state = find_handle_state (handle_);
    if (!state)
        return 0;

    std::lock_guard<std::mutex> lock (state->mutex);
    if (state->send.active) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

void zlink::part_helper_internal::invalidate_recv_sequence (void *handle_)
{
    std::shared_ptr<handle_state_t> state = find_handle_state (handle_);
    if (!state)
        return;

    std::lock_guard<std::mutex> lock (state->mutex);
    reset_recv_sequence (&state->recv);
}

void zlink::part_helper_internal::cleanup_handle (void *handle_)
{
    std::shared_ptr<handle_state_t> state;
    {
        std::lock_guard<std::mutex> lock (g_part_helper_mutex);
        std::unordered_map<void *, std::shared_ptr<handle_state_t> >::iterator it =
          g_part_helper_state.find (handle_);
        if (it == g_part_helper_state.end ())
            return;
        state = it->second;
        g_part_helper_state.erase (it);
    }

    std::lock_guard<std::mutex> lock (state->mutex);
    reset_send_sequence (&state->send);
    reset_recv_sequence (&state->recv);
}
