/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_SOCKETS_DETAIL_HPP_INCLUDED
#define ZLINK_CPP_SOCKETS_DETAIL_HPP_INCLUDED

#include "../../Contracts/Core/async_result.hpp"
#include "../../Contracts/Sockets/message_socket.hpp"
#include "../../Contracts/Sockets/publisher_socket.hpp"
#include "../../Contracts/Sockets/subscriber_socket.hpp"
#include "../../Contracts/Errors/error.hpp"

#include <chrono>
#include <future>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

namespace zlink
{

// Shared implementation helpers for concrete socket entrypoint headers.

namespace service
{
class spot_node_t;
} // namespace service
namespace detail
{
inline void *native_handle (service::spot_node_t &node_) noexcept;
inline const void *native_handle (const service::spot_node_t &node_) noexcept;
} // namespace detail

namespace detail
{

template<typename ErrorT, typename ResultT>
inline void throw_if_failed_result (ResultT result_)
{
    detail::throw_if_failed<ErrorT> (result_);
}

struct request_state_t
{
    std::unique_ptr<std::promise<std::vector<message_t>>> promise;
    std::function<void(request_result_t, std::vector<message_t>)> on_complete;
};

inline std::function<void()> make_socket_request_progress (void *socket_)
{
    return [socket_]() { zlink::detail::request_progress_socket (socket_); };
}

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

[[noreturn]] inline void throw_submit_error_from_errno (int err_)
{
    throw submit_error_t (zlink::detail::submit_result_from_errno (err_), err_);
}

[[noreturn]] inline void throw_config_error_from_errno (int err_)
{
    throw config_error_t (zlink::detail::config_result_from_errno (err_), err_);
}

[[noreturn]] inline void throw_handler_error_from_errno (int err_)
{
    throw handler_error_t (zlink::detail::handler_result_from_errno (err_), err_);
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

inline std::chrono::milliseconds
resolve_timeout (std::chrono::milliseconds requested_,
                 std::chrono::milliseconds fallback_) noexcept
{
    return requested_ == std::chrono::milliseconds () ? fallback_ : requested_;
}

inline received_t make_received (
  const zlink_routing_id_t *routing_id_,
  const zlink_routing_id_t *spot_rid_,
  uint64_t request_seq_,
  bool has_request_seq_,
  zlink_msg_t *parts_,
  size_t part_count_,
  std::function<void(std::vector<message_t> &, send_flags_t)> reply_fn_ =
    std::function<void(std::vector<message_t> &, send_flags_t)> ())
{
    return received_t (
      (routing_id_ && routing_id_->size > 0)
        ? std::optional<routing_id_t> (zlink::detail::native_routing_id (*routing_id_))
        : std::nullopt,
      (spot_rid_ && spot_rid_->size > 0)
        ? std::optional<routing_id_t> (zlink::detail::native_routing_id (*spot_rid_))
        : std::nullopt,
      has_request_seq_ ? std::optional<uint64_t> (request_seq_) : std::nullopt,
      detail::take_parts_from_native (parts_, part_count_),
      std::move (reply_fn_));
}

inline received_t recv_router_received (void *router_handle_,
                                        recv_flags_t flags_)
{
    const zlink_routing_id_t *source_node_rid = NULL;
    const zlink_routing_id_t *source_spot_rid = NULL;
    uint64_t request_seq = 0;
    std::vector<message_t> parts;
    const recv_result_t rc = static_cast<recv_result_t> (
      detail::recv_router_parts (
        router_handle_, flags_, &source_node_rid, &source_spot_rid,
        &request_seq, parts));
    if (rc != recv_result_t::ok)
        throw recv_error_t (rc, zlink_errno ());
    std::function<void(std::vector<message_t> &, send_flags_t)> reply_fn;
    std::function<bool(std::vector<message_t> &, send_flags_t)> send_fn;
    std::optional<routing_id_t> routing_id =
      (source_node_rid && source_node_rid->size > 0)
        ? std::optional<routing_id_t> (zlink::detail::native_routing_id (*source_node_rid))
        : std::nullopt;
    std::optional<routing_id_t> spot_rid =
      (source_spot_rid && source_spot_rid->size > 0)
        ? std::optional<routing_id_t> (zlink::detail::native_routing_id (*source_spot_rid))
        : std::nullopt;

    if (routing_id) {
        const routing_id_t send_node_rid = *routing_id;
        if (spot_rid) {
            const routing_id_t send_spot_rid = *spot_rid;
            send_fn = [router_handle_, send_node_rid, send_spot_rid] (
                        std::vector<message_t> &send_parts_,
                        send_flags_t flags_) {
                std::vector<zlink_msg_t> native;
                if (detail::move_parts_to_native (send_parts_, native) != 0)
                    throw last_error ();
                size_t failed_index = 0;
                const submit_result_t result = static_cast<submit_result_t> (
                  detail::submit_native_parts (
                    native, failed_index,
                    [&] (zlink_msg_t *part_out_,
                         zlink_part_flag_t part_flag_, bool) {
                        return zlink_router_send_spot_part (
                          router_handle_,
                          zlink::detail::routing_id_native (send_node_rid),
                          zlink::detail::routing_id_native (send_spot_rid),
                          part_out_, static_cast<zlink_send_flags_t> (flags_),
                          part_flag_);
                    }));
                if (result == submit_result_t::ok)
                    return true;
                detail::restore_parts_from_native (send_parts_, native, failed_index);
                if (flags_ == send_flags_t::dontwait
                    && result == submit_result_t::backpressured)
                    return false;
                throw submit_error_t (result, zlink_errno ());
            };
        } else {
            send_fn = [router_handle_, send_node_rid] (
                        std::vector<message_t> &send_parts_,
                        send_flags_t flags_) {
                std::vector<zlink_msg_t> native;
                if (detail::move_parts_to_native (send_parts_, native) != 0)
                    throw last_error ();
                size_t failed_index = 0;
                const submit_result_t result = static_cast<submit_result_t> (
                  detail::submit_native_parts (
                    native, failed_index,
                    [&] (zlink_msg_t *part_out_,
                         zlink_part_flag_t part_flag_, bool) {
                        return zlink_send_part_rid (
                          router_handle_,
                          zlink::detail::routing_id_native (send_node_rid),
                          part_out_, static_cast<zlink_send_flags_t> (flags_),
                          part_flag_);
                    }));
                if (result == submit_result_t::ok)
                    return true;
                detail::restore_parts_from_native (send_parts_, native, failed_index);
                if (flags_ == send_flags_t::dontwait
                    && result == submit_result_t::backpressured)
                    return false;
                throw submit_error_t (result, zlink_errno ());
            };
        }
    }

    if (routing_id && request_seq != 0u) {
        const routing_id_t reply_node_rid = *routing_id;
        if (spot_rid) {
            const routing_id_t reply_spot_rid = *spot_rid;
            reply_fn = [router_handle_, reply_node_rid, reply_spot_rid,
                        request_seq] (
                         std::vector<message_t> &reply_parts_,
                         send_flags_t flags_) {
                detail::throw_if_reply_flags_unsupported (flags_);
                std::vector<zlink_msg_t> native;
                if (detail::move_parts_to_native (reply_parts_, native) != 0)
                    throw last_error ();
                size_t failed_index = 0;
                const submit_result_t result = static_cast<submit_result_t> (
                  detail::submit_native_parts (
                    native, failed_index,
                    [&] (zlink_msg_t *part_out_,
                         zlink_part_flag_t part_flag_, bool) {
                        return zlink_router_reply_spot_part (
                          router_handle_, zlink::detail::routing_id_native (reply_node_rid),
                          zlink::detail::routing_id_native (reply_spot_rid), request_seq,
                          part_out_, part_flag_);
                    }));
                if (result != submit_result_t::ok) {
                    detail::restore_parts_from_native (reply_parts_, native, failed_index);
                    throw submit_error_t (result, zlink_errno ());
                }
            };
        } else {
            reply_fn = [router_handle_, reply_node_rid, request_seq] (
                         std::vector<message_t> &reply_parts_,
                         send_flags_t flags_) {
                detail::throw_if_reply_flags_unsupported (flags_);
                std::vector<zlink_msg_t> native;
                if (detail::move_parts_to_native (reply_parts_, native) != 0)
                    throw last_error ();
                size_t failed_index = 0;
                const submit_result_t result = static_cast<submit_result_t> (
                  detail::submit_native_parts (
                    native, failed_index,
                    [&] (zlink_msg_t *part_out_,
                         zlink_part_flag_t part_flag_, bool) {
                        return zlink_router_reply_part (
                          router_handle_, zlink::detail::routing_id_native (reply_node_rid),
                          request_seq, part_out_, part_flag_);
                    }));
                if (result != submit_result_t::ok) {
                    detail::restore_parts_from_native (reply_parts_, native, failed_index);
                    throw submit_error_t (result, zlink_errno ());
                }
            };
        }
    }
    std::optional<uint64_t> maybe_request_seq =
      request_seq != 0u ? std::optional<uint64_t> (request_seq) : std::nullopt;

    if (parts.size () == 1u) {
        message_t part = std::move (parts[0]);
        return received_t (
	          std::move (routing_id), std::move (spot_rid), maybe_request_seq,
	          std::move (part), std::move (reply_fn), std::move (send_fn));
    }

    return received_t (
	      std::move (routing_id), std::move (spot_rid), maybe_request_seq,
	      std::move (parts), std::move (reply_fn), std::move (send_fn));
}

} // namespace detail

namespace detail
{

inline int recv_single_part_message (void *handle_,
                                     routing_id_t *source_rid_out_,
                                     message_t &part_out_,
                                     recv_flags_t flags_)
{
    zlink_msg_t part;
    if (zlink_msg_init (&part) != 0)
        return -1;

    const zlink_routing_id_t *source_rid = NULL;
    zlink_part_flag_t has_more = ZLINK_PART_FINAL;
    const int rc = zlink_recv_part (
      handle_, &source_rid, &part, &has_more,
      static_cast<zlink_recv_flags_t> (flags_));
    if (rc != 0) {
        (void) zlink_msg_close (&part);
        return rc;
    }
    if (has_more != ZLINK_PART_FINAL) {
        (void) zlink_msg_close (&part);
        errno = EMSGSIZE;
        return -1;
    }

    if (source_rid_out_) {
        if (source_rid && source_rid->size > 0)
            assign_routing_id_native (*source_rid_out_, *source_rid);
        else
            *source_rid_out_ = unchecked_empty_routing_id ();
    }
    adopt_native_message (part_out_, &part);
    return part_out_.valid () ? 0 : -1;
}

inline int recv_single_part_routed_message (void *handle_,
                                            routing_id_t &source_rid_out_,
                                            message_t &part_out_,
                                            recv_flags_t flags_)
{
    zlink_msg_t part;
    if (zlink_msg_init (&part) != 0)
        return -1;

    const zlink_routing_id_t *source_node_rid = NULL;
    const zlink_routing_id_t *source_spot_rid = NULL;
    uint64_t request_seq = 0;
    zlink_part_flag_t has_more = ZLINK_PART_FINAL;
    const int rc = zlink_router_recv_part (
      handle_, &source_node_rid, &source_spot_rid, &request_seq, &part,
      &has_more, static_cast<zlink_recv_flags_t> (flags_));
    if (rc != 0) {
        (void) zlink_msg_close (&part);
        return rc;
    }
    if (has_more != ZLINK_PART_FINAL || request_seq != 0
        || (source_spot_rid && source_spot_rid->size > 0)
        || !source_node_rid || source_node_rid->size == 0) {
        (void) zlink_msg_close (&part);
        errno = has_more != ZLINK_PART_FINAL ? EMSGSIZE : EPROTO;
        return -1;
    }

    assign_routing_id_native (source_rid_out_, *source_node_rid);
    adopt_native_message (part_out_, &part);
    return part_out_.valid () ? 0 : -1;
}

} // namespace detail

} // namespace zlink

#endif
