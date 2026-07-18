/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_RUNTIME_SERVICE_DETAIL_HPP_INCLUDED
#define ZLINK_CPP_RUNTIME_SERVICE_DETAIL_HPP_INCLUDED

#include "../Native/native_message_parts.hpp"
#include "../Native/native_options.hpp"
#include "../Native/request_progress.hpp"
#include "../Core/operation_detail.hpp"
#include <zlink/Contracts/Core/routing_id.hpp>
#include <zlink/Contracts/Errors/errors.hpp>
#include <zlink/Contracts/Errors/results.hpp>
#include <zlink/Contracts/Messaging/message.hpp>
#include <zlink/Contracts/Service/mesh_node_models.hpp>
#include <zlink/Contracts/Sockets/results.hpp>

#include <cassert>
#include <cerrno>
#include <cstdio>
#include <functional>
#include <future>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace zlink
{
namespace service
{

namespace detail
{

using zlink::detail::assign_parts_from_native;
using zlink::detail::close_message_array;
using zlink::detail::close_native_parts;
using zlink::detail::last_error;
using zlink::detail::move_parts_to_native;
using zlink::detail::restore_parts_from_native;
using zlink::detail::submit_borrowed_message_array;
using zlink::detail::submit_message_parts;
using zlink::detail::submit_message_parts_close_on_failure;
using zlink::detail::submit_native_parts;
using zlink::detail::take_parts_from_native;
using zlink::detail::throw_if_failed;

// Moves message ownership into the native slot, rejecting the submission when
// the move does not consume the message (i.e. the message was invalid).
// Centralizes the ownership-transfer protocol shared by every single-part
// submit path; the native slot is closed before the rejection so it never
// leaks on the error edge.
inline void move_to_native_or_reject (message_t &message_, zlink_msg_t *native_)
{
    zlink::detail::move_to_native (message_, native_);
    if (message_.valid ()) {
        (void) zlink_msg_close (native_);
        throw submit_error_t (submit_result_t::invalid_argument, EINVAL);
    }
}

// Builds a borrowed native metadata view from an immutable byte span. Returns
// nullptr (Core's "no metadata") for an empty span so callers need no branch.
inline const zlink_mesh_metadata_view_t *
make_metadata_view (zlink_mesh_metadata_view_t &view_, mesh_metadata_t metadata_) noexcept
{
    if (metadata_.empty ())
        return nullptr;
    view_.data = metadata_.data ();
    view_.size = metadata_.size ();
    return &view_;
}

inline void store_publish_detail (publish_detail_t *out_,
                                  const zlink_mesh_publish_detail_t &native_) noexcept
{
    if (!out_)
        return;
    out_->snapshot_remote_target_count = native_.snapshot_remote_target_count;
    out_->admitted_remote_target_count = native_.admitted_remote_target_count;
    out_->dropped_remote_target_count = native_.dropped_remote_target_count;
    out_->unreachable_remote_target_count = native_.unreachable_remote_target_count;
    out_->snapshot_local_spot_count = native_.snapshot_local_spot_count;
    out_->admitted_local_spot_count = native_.admitted_local_spot_count;
    out_->dropped_local_spot_count = native_.dropped_local_spot_count;
}

// Ownership contract for the RAII service handles (mesh_node/publisher/spot/
// stream-session): a parent must outlive the children it creates. Core rejects
// a parent's destroy with close_result_t::busy while any child handle or
// in-flight callback is still open, and it retains the native handle in that
// case. A destructor is noexcept and cannot retry or throw, so it cannot
// honour a busy close the way close()/move-assign do (which keep the object
// alive for a later retry). Rather than discard the busy result and silently
// leak the retained handle, the destructors route the close outcome here:
// non-ok teardown emits a diagnostic (fail-loud in every build) and, in debug
// builds, trips an assert so the offending destruction order is caught during
// development. Declaring the parent before its children (so reverse-order
// destruction closes children first) keeps this path unreachable.
inline void report_close_on_destroy (const char *type_name_,
                                     close_result_t result_) noexcept
{
    if (result_ == close_result_t::ok)
        return;
    std::fprintf (
      stderr,
      "zlink: %s destroyed before Core could release it (close_result=%d); native "
      "handle leaked -- close child handles before the owning parent\n",
      type_name_, static_cast<int> (result_));
    assert (false && "zlink service handle destroyed while still busy; close children first");
}

struct request_state_t
{
    std::unique_ptr<std::promise<std::vector<message_t>>> promise;
    std::function<void (request_result_t, std::vector<message_t>)> on_complete;
};

inline request_state_t *make_future_request_state ()
{
    std::unique_ptr<request_state_t> state = std::make_unique<request_state_t> ();
    state->promise = std::make_unique<std::promise<std::vector<message_t>>> ();
    return state.release ();
}

inline request_state_t *make_callback_request_state (
  std::function<void (request_result_t, std::vector<message_t>)> callback_)
{
    std::unique_ptr<request_state_t> state = std::make_unique<request_state_t> ();
    state->on_complete = std::move (callback_);
    return state.release ();
}

inline void complete_request_state (request_state_t *state_,
                                    zlink_request_result_t result_,
                                    zlink_msg_t *parts_,
                                    size_t part_count_)
{
    if (!state_)
        return;
    std::unique_ptr<request_state_t> holder (state_);
    if (result_ != ZLINK_REQUEST_OK) {
        if (holder->on_complete)
            holder->on_complete (static_cast<request_result_t> (result_),
                                 std::vector<message_t> ());
        if (holder->promise) {
            holder->promise->set_exception (
              std::make_exception_ptr (request_error_t (static_cast<request_result_t> (result_))));
        }
        return;
    }
    std::vector<message_t> parts = detail::take_parts_from_native (parts_, part_count_);
    if (holder->on_complete) {
        holder->on_complete (request_result_t::ok, std::move (parts));
        return;
    }
    if (holder->promise)
        holder->promise->set_value (std::move (parts));
}

inline void request_callback_trampoline (zlink_request_result_t result_,
                                         zlink_msg_t *parts_,
                                         size_t part_count_,
                                         void *userdata_)
{
    complete_request_state (static_cast<request_state_t *> (userdata_), result_, parts_,
                            part_count_);
}

} // namespace detail


} // namespace service
} // namespace zlink

#endif
