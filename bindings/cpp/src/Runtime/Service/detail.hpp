/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_RUNTIME_SERVICE_DETAIL_HPP_INCLUDED
#define ZLINK_CPP_RUNTIME_SERVICE_DETAIL_HPP_INCLUDED

#include "../Native/native_parts.hpp"
#include "../Core/operation_detail.hpp"
#include <zlink/Contracts/Core/routing_id.hpp>
#include <zlink/Contracts/Errors/errors.hpp>
#include <zlink/Contracts/Messaging/message.hpp>
#include <zlink/Contracts/Sockets/results.hpp>

#include <cerrno>
#include <functional>
#include <future>
#include <memory>
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
using zlink::detail::collect_parts_from_recv;
using zlink::detail::get_string_option;
using zlink::detail::last_error;
using zlink::detail::move_parts_to_native;
using zlink::detail::restore_parts_from_native;
using zlink::detail::submit_native_parts;
using zlink::detail::take_parts_from_native;
using zlink::detail::throw_if_failed;

inline void request_progress_spot (void *spot_) noexcept
{
    void *poller = zlink_poller_new ();
    if (!poller)
        return;
    if (zlink_poller_add (poller, spot_, NULL, ZLINK_POLLCOMPLETION)
        == ZLINK_CONFIG_OK) {
        zlink_poller_event_t event;
        (void) zlink_poller_wait (poller, &event, 1, 0, NULL);
        (void) zlink_poller_remove (poller, spot_);
    }
    (void) zlink_poller_destroy (&poller);
}

inline void request_progress_spot_channel (
  void *spot_,
  const std::string &channel_name_) noexcept
{
    (void) channel_name_;
    request_progress_spot (spot_);
}

inline std::function<void()> make_spot_request_progress (void *spot_)
{
    return [spot_]() { request_progress_spot (spot_); };
}

inline std::function<void()>
make_spot_request_progress (void *spot_, const std::string &channel_name_)
{
    return [spot_, channel_name_]() {
        request_progress_spot_channel (spot_, channel_name_);
    };
}

inline send_result_t to_send_result (int result_) noexcept
{
    switch (result_) {
    case ZLINK_SUBMIT_OK:
        return send_result_t::sent;
    case ZLINK_SUBMIT_BACKPRESSURED:
        return send_result_t::backpressured;
    case ZLINK_SUBMIT_NOT_CONNECTED:
        return send_result_t::not_ready;
    default:
        return send_result_t::sent;
    }
}

inline bool classify_nonblocking_send_errno (int err_,
                                             send_result_t &result_) noexcept
{
    switch (err_) {
    case EAGAIN:
        result_ = send_result_t::backpressured;
        return true;
    case ENOTCONN:
    case EHOSTUNREACH:
        result_ = send_result_t::not_ready;
        return true;
    default:
        return false;
    }
}

struct request_state_t
{
    std::unique_ptr<std::promise<std::vector<message_t>>> promise;
    std::function<void(request_result_t, std::vector<message_t>)> on_complete;
};

inline request_state_t *make_future_request_state ()
{
    request_state_t *state = new request_state_t ();
    state->promise.reset (new std::promise<std::vector<message_t>> ());
    return state;
}

inline request_state_t *
make_callback_request_state (
  std::function<void(request_result_t, std::vector<message_t>)> callback_)
{
    request_state_t *state = new request_state_t ();
    state->on_complete = std::move (callback_);
    return state;
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
            holder->on_complete (
              static_cast<request_result_t> (result_),
              std::vector<message_t> ());
        if (holder->promise) {
            holder->promise->set_exception (
              std::make_exception_ptr (
                request_error_t (static_cast<request_result_t> (result_))));
        }
        return;
    }
    std::vector<message_t> parts =
      detail::take_parts_from_native (parts_, part_count_);
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
    complete_request_state (
      static_cast<request_state_t *> (userdata_), result_, parts_, part_count_);
}

inline void actor_join_callback_trampoline (
  const zlink_actor_join_result_t *result_,
  zlink_msg_t *parts_,
  size_t part_count_,
  void *userdata_)
{
    const zlink_request_result_t result =
      result_ ? result_->result : ZLINK_REQUEST_INTERNAL_ERROR;
    complete_request_state (
      static_cast<request_state_t *> (userdata_), result, parts_, part_count_);
}

} // namespace detail


} // namespace service
} // namespace zlink

#endif
