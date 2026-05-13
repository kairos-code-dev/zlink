/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_SERVICES_SPOT_COMMON_HPP_INCLUDED
#define ZLINK_CPP_SERVICES_SPOT_COMMON_HPP_INCLUDED

#include "../context.hpp"
#include "../async_result.hpp"
#include "../message.hpp"
#include "../socket_types.hpp"
#include "../types.hpp"
#include "discovery.hpp"

#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <future>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

zlink_recv_result_t spot_subscribe_impl (void *spot_,
                                         zlink_routing_id_t *source_rid_out_,
                                         zlink_msg_t **parts_out_,
                                         size_t *part_count_out_,
                                         char *topic_id_out_,
                                         size_t *topic_id_len_out_,
                                         zlink_recv_flags_t flags_);

zlink_recv_result_t spot_recv_impl (void *spot_,
                                    const zlink_routing_id_t **source_rid_out_,
                                    const zlink_routing_id_t **spot_rid_out_,
                                    uint64_t *request_seq_out_,
                                    zlink_msg_t **parts_out_,
                                    size_t *part_count_out_,
                                    zlink_recv_flags_t flags_);

namespace zlink
{
extern "C" zlink_recv_result_t zlink_spot_subscription_event_recv (
  void *spot_,
  const zlink_routing_id_t **source_rid_out_,
  int *subscribed_out_,
  char *topic_id_buf_,
  size_t topic_id_capacity_,
  size_t *topic_id_len_out_,
  zlink_recv_flags_t flags_);
namespace service
{

class spot_node_t;
class spot_t;
class actor_t;
class send_op_t;
class send_ready_op_t;
class request_op_t;
class request_ready_op_t;
class request_callback_ready_op_t;
class reply_op_t;
class reply_ready_op_t;
class actor_join_op_t;
class actor_join_ready_op_t;
class actor_join_callback_ready_op_t;
class actor_join_reply_op_t;
class actor_leave_op_t;
class actor_destroy_op_t;
class actor_lookup_op_t;
class actor_bind_op_t;
class actor_unbind_op_t;

} // namespace service
namespace detail
{
inline void *native_handle (service::spot_node_t &node_) noexcept;
inline const void *native_handle (const service::spot_node_t &node_) noexcept;
inline void *native_handle (service::spot_t &spot_) noexcept;
inline const void *native_handle (const service::spot_t &spot_) noexcept;
} // namespace detail
namespace service
{

namespace detail
{

extern "C" int zlink_spot_request_progress_internal (void *spot_);
extern "C" int zlink_spot_request_channel_progress_internal (
  void *spot_, const char *channel_name_);
extern "C" int zlink_socket_request_progress_internal (void *socket_);

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

inline std::function<void()> make_spot_request_progress (void *spot_)
{
    return [spot_]() { (void) zlink_spot_request_progress_internal (spot_); };
}

inline std::function<void()> make_socket_request_progress (void *socket_)
{
    return [socket_]() { (void) zlink_socket_request_progress_internal (socket_); };
}

inline std::function<void()> make_spot_request_progress (void *spot_,
                                                         const std::string &channel_name_)
{
    return [spot_, channel_name_]() {
        (void) zlink_spot_request_channel_progress_internal (
          spot_, channel_name_.c_str ());
    };
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
