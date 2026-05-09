/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_SOCKET_TYPES_HPP_INCLUDED
#define ZLINK_CPP_SOCKET_TYPES_HPP_INCLUDED

#include "async_result.hpp"
#include "message_socket.hpp"
#include "publisher_socket.hpp"
#include "subscriber_socket.hpp"
#include "error.hpp"

#include <chrono>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

namespace zlink
{

// Compatibility aggregate for the concrete socket classes. Public class
// entrypoint headers live under zlink/sockets/*.hpp and include this file.

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

inline std::vector<message_t>
take_parts (zlink_msg_t *parts_, size_t part_count_)
{
    std::vector<message_t> parts;
    parts.resize (part_count_);
    for (size_t i = 0; i < part_count_; ++i)
        (void) zlink_msg_move (detail::native_handle(parts[i]), &parts_[i]);
    return parts;
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

inline std::function<void()> make_socket_request_progress (void *socket_)
{
    return [socket_]() { (void) zlink_socket_request_progress_internal (socket_); };
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

    std::vector<message_t> parts = take_parts (parts_, part_count_);
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
      take_parts (parts_, part_count_), std::move (reply_fn_));
}

inline received_t recv_router_received (void *router_handle_,
                                        recv_flags_t flags_)
{
    const zlink_routing_id_t *source_node_rid = NULL;
    const zlink_routing_id_t *source_spot_rid = NULL;
    uint64_t request_seq = 0;
    zlink_msg_t *parts_native = NULL;
    size_t part_count = 0;
    const ::socket_handle_t handle = {
      static_cast<socket_base_t *> (router_handle_)};
    const recv_result_t rc = static_cast<recv_result_t> (
      detail::recv_result_from_rc (
        socket_reqrep_internal::recv_router_message_direct (
          handle, &source_node_rid, &source_spot_rid, &request_seq,
          &parts_native, &part_count, static_cast<int> (flags_))));
    if (rc != recv_result_t::ok)
        throw recv_error_t (rc, zlink_errno ());
    std::vector<message_t> parts;
    if (detail::assign_parts_from_native (parts_native, part_count, parts) != 0)
        throw last_error ();

    std::function<void(std::vector<message_t> &, send_flags_t)> reply_fn;
    if (source_node_rid && source_node_rid->size > 0 && request_seq != 0u) {
        const routing_id_t reply_node_rid =
          zlink::detail::native_routing_id (*source_node_rid);
        if (source_spot_rid && source_spot_rid->size > 0) {
            const routing_id_t reply_spot_rid =
              zlink::detail::native_routing_id (*source_spot_rid);
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
                         zlink_part_flag_t part_flag_,
                         bool) {
                        return zlink_router_reply_spot_part (
                          router_handle_, zlink::detail::routing_id_native (reply_node_rid),
                          zlink::detail::routing_id_native (reply_spot_rid), request_seq,
                          part_out_, part_flag_);
                    }));
                if (result != submit_result_t::ok) {
                    detail::restore_parts_from_native (
                      reply_parts_, native, failed_index);
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
                         zlink_part_flag_t part_flag_,
                         bool) {
                        return zlink_router_reply_part (
                          router_handle_, zlink::detail::routing_id_native (reply_node_rid),
                          request_seq, part_out_, part_flag_);
                    }));
                if (result != submit_result_t::ok) {
                    detail::restore_parts_from_native (
                      reply_parts_, native, failed_index);
                    throw submit_error_t (result, zlink_errno ());
                }
            };
        }
    }
    return received_t (
      (source_node_rid && source_node_rid->size > 0)
        ? std::optional<routing_id_t> (zlink::detail::native_routing_id (*source_node_rid))
        : std::nullopt,
      (source_spot_rid && source_spot_rid->size > 0)
        ? std::optional<routing_id_t> (zlink::detail::native_routing_id (*source_spot_rid))
        : std::nullopt,
      request_seq != 0u ? std::optional<uint64_t> (request_seq) : std::nullopt,
      std::move (parts), std::move (reply_fn));
}

} // namespace detail

class pair_socket_t : public message_socket_t
{
  public:
    explicit pair_socket_t (context_t &ctx_) : message_socket_t (ctx_, socket_type::pair) {}

    bool send (message_t &part_, send_flags_t flags_ = send_flags_t::none)
    {
        if (flags_ == send_flags_t::dontwait) {
            send_result_t result = send_result_t::sent;
            if (base_socket_t::send_no_wait_result (result, part_) != 0)
                detail::throw_submit_error_from_errno (zlink_errno ());
            if (result == send_result_t::not_ready)
                throw submit_error_t (submit_result_t::not_connected, zlink_errno ());
            return result == send_result_t::sent;
        }

        const submit_result_t rc = static_cast<submit_result_t> (
          base_socket_t::send (part_, flags_));
        if (rc != submit_result_t::ok)
            throw submit_error_t (rc, zlink_errno ());
        return true;
    }

    bool send (std::vector<message_t> &parts_, send_flags_t flags_ = send_flags_t::none)
    {
        if (flags_ == send_flags_t::dontwait) {
            send_result_t result = send_result_t::sent;
            if (base_socket_t::send_no_wait_result (result, parts_) != 0)
                detail::throw_submit_error_from_errno (zlink_errno ());
            if (result == send_result_t::not_ready)
                throw submit_error_t (submit_result_t::not_connected, zlink_errno ());
            return result == send_result_t::sent;
        }

        const submit_result_t rc = static_cast<submit_result_t> (
          base_socket_t::send (parts_, flags_));
        if (rc != submit_result_t::ok)
            throw submit_error_t (rc, zlink_errno ());
        return true;
    }

    std::optional<received_t> recv (recv_flags_t flags_ = recv_flags_t::none)
    {
        zlink_msg_t native_part;
        if (zlink_msg_init (&native_part) != 0)
            throw last_error ();

        const zlink_routing_id_t *source_rid = NULL;
        zlink_part_flag_t has_more = ZLINK_PART_FINAL;
        const zlink_recv_result_t rc = zlink_recv_part (
          handle (), &source_rid, &native_part, &has_more,
          static_cast<zlink_recv_flags_t> (flags_));
        if (rc == ZLINK_RECV_NO_DATA && flags_ == recv_flags_t::dontwait) {
            (void) zlink_msg_close (&native_part);
            return std::nullopt;
        }
        if (rc != ZLINK_RECV_OK) {
            const int err = zlink_errno ();
            (void) zlink_msg_close (&native_part);
            throw recv_error_t (static_cast<recv_result_t> (rc), err);
        }

        std::vector<message_t> parts;
        parts.reserve (1);
        message_t part;
        if (zlink_msg_move (detail::native_handle (part), &native_part) != 0) {
            const int err = zlink_errno ();
            (void) zlink_msg_close (&native_part);
            throw recv_error_t (recv_result_t::internal_error, err);
        }
        (void) zlink_msg_close (&native_part);
        parts.push_back (std::move (part));

        if (has_more != ZLINK_PART_FINAL) {
            std::vector<message_t> tail;
            const int tail_rc = detail::collect_parts_from_recv (
              [&] (zlink_msg_t *part_out_, zlink_part_flag_t *has_more_out_, bool) {
                  return zlink_recv_part (
                    handle (), &source_rid, part_out_, has_more_out_,
                    static_cast<zlink_recv_flags_t> (flags_));
              },
              tail);
            if (tail_rc != 0)
                throw recv_error_t (static_cast<recv_result_t> (tail_rc),
                                    zlink_errno ());
            parts.reserve (parts.size () + tail.size ());
            for (size_t i = 0; i < tail.size (); ++i)
                parts.push_back (std::move (tail[i]));
        }

        return std::optional<received_t> (
          received_t (
            source_rid && source_rid->size > 0
              ? std::optional<routing_id_t> (
                  zlink::detail::native_routing_id (*source_rid))
              : std::nullopt,
            std::nullopt,
            std::nullopt,
            std::move (parts)));
    }

    void on_send_ready (std::function<void()> handler_)
    {
        base_socket_t::on_send_ready (std::move (handler_));
    }

  private:
    using message_socket_t::recv;
    using message_socket_t::send;
};

class dealer_socket_t : public message_socket_t
{
  public:
    explicit dealer_socket_t (context_t &ctx_)
        : message_socket_t (ctx_, socket_type::dealer),
          _default_request_timeout (std::chrono::milliseconds ())
    {
    }

    bool send (message_t &part_, send_flags_t flags_ = send_flags_t::none)
    {
        if (flags_ == send_flags_t::dontwait) {
            send_result_t result = send_result_t::sent;
            if (base_socket_t::send_no_wait_result (result, part_) != 0)
                detail::throw_submit_error_from_errno (zlink_errno ());
            if (result == send_result_t::not_ready)
                throw submit_error_t (submit_result_t::not_connected, zlink_errno ());
            return result == send_result_t::sent;
        }

        const submit_result_t rc = static_cast<submit_result_t> (
          base_socket_t::send (part_, flags_));
        if (rc != submit_result_t::ok)
            throw submit_error_t (rc, zlink_errno ());
        return true;
    }

    bool send (std::vector<message_t> &parts_, send_flags_t flags_ = send_flags_t::none)
    {
        if (flags_ == send_flags_t::dontwait) {
            send_result_t result = send_result_t::sent;
            if (base_socket_t::send_no_wait_result (result, parts_) != 0)
                detail::throw_submit_error_from_errno (zlink_errno ());
            if (result == send_result_t::not_ready)
                throw submit_error_t (submit_result_t::not_connected, zlink_errno ());
            return result == send_result_t::sent;
        }

        const submit_result_t rc = static_cast<submit_result_t> (
          base_socket_t::send (parts_, flags_));
        if (rc != submit_result_t::ok)
            throw submit_error_t (rc, zlink_errno ());
        return true;
    }

    std::optional<received_t> recv (recv_flags_t flags_ = recv_flags_t::none)
    {
        zlink_msg_t native_part;
        if (zlink_msg_init (&native_part) != 0)
            throw last_error ();

        const zlink_routing_id_t *source_rid = NULL;
        zlink_part_flag_t has_more = ZLINK_PART_FINAL;
        const zlink_recv_result_t rc = zlink_recv_part (
          handle (), &source_rid, &native_part, &has_more,
          static_cast<zlink_recv_flags_t> (flags_));
        if (rc == ZLINK_RECV_NO_DATA && flags_ == recv_flags_t::dontwait) {
            (void) zlink_msg_close (&native_part);
            return std::nullopt;
        }
        if (rc != ZLINK_RECV_OK) {
            const int err = zlink_errno ();
            (void) zlink_msg_close (&native_part);
            throw recv_error_t (static_cast<recv_result_t> (rc), err);
        }

        std::vector<message_t> parts;
        parts.reserve (1);
        message_t part;
        if (zlink_msg_move (detail::native_handle (part), &native_part) != 0) {
            const int err = zlink_errno ();
            (void) zlink_msg_close (&native_part);
            throw recv_error_t (recv_result_t::internal_error, err);
        }
        (void) zlink_msg_close (&native_part);
        parts.push_back (std::move (part));

        if (has_more != ZLINK_PART_FINAL) {
            std::vector<message_t> tail;
            const int tail_rc = detail::collect_parts_from_recv (
              [&] (zlink_msg_t *part_out_, zlink_part_flag_t *has_more_out_, bool) {
                  return zlink_recv_part (
                    handle (), &source_rid, part_out_, has_more_out_,
                    static_cast<zlink_recv_flags_t> (flags_));
              },
              tail);
            if (tail_rc != 0)
                throw recv_error_t (static_cast<recv_result_t> (tail_rc),
                                    zlink_errno ());
            parts.reserve (parts.size () + tail.size ());
            for (size_t i = 0; i < tail.size (); ++i)
                parts.push_back (std::move (tail[i]));
        }

        return std::optional<received_t> (
          received_t (
            source_rid && source_rid->size > 0
              ? std::optional<routing_id_t> (
                  zlink::detail::native_routing_id (*source_rid))
              : std::nullopt,
            std::nullopt,
            std::nullopt,
            std::move (parts)));
    }

    void on_send_ready (std::function<void()> handler_)
    {
        base_socket_t::on_send_ready (std::move (handler_));
    }

    async_result_t<std::vector<message_t>> request (message_t &part_,
                                                    std::chrono::milliseconds timeout_ = {})
    {
        std::vector<message_t> parts;
        parts.push_back (std::move (part_));
        return request (parts, timeout_);
    }

    async_result_t<std::vector<message_t>>
    request (std::vector<message_t> &parts_,
             std::chrono::milliseconds timeout_ = {})
    {
        detail::request_state_t *state = detail::make_future_request_state ();
        std::future<std::vector<message_t>> future =
          state->promise->get_future ();
        std::vector<zlink_msg_t> native;
        if (detail::move_parts_to_native (parts_, native) != 0) {
            delete state;
            throw last_error ();
        }
        size_t failed_index = 0;
        const int rc = detail::submit_native_parts (
          native, failed_index,
          [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_, bool is_final_) {
              return zlink_dealer_request_part (
                handle (), part_out_, ZLINK_SEND_FLAGS_NONE, part_flag_,
                is_final_
                  ? static_cast<uint32_t> (
                      detail::resolve_timeout (timeout_, _default_request_timeout)
                        .count ())
                  : 0u,
                is_final_ ? &detail::request_callback_trampoline : NULL,
                is_final_ ? state : NULL);
          });
        if (rc != 0) {
            detail::close_native_parts (native, failed_index);
            delete state;
            throw last_error ();
        }
        return async_result_t<std::vector<message_t>> (
          std::move (future), detail::make_socket_request_progress (handle ()));
    }

    bool request (message_t &part_,
                  std::function<void(request_result_t, std::vector<message_t>)> callback_,
                  send_flags_t flags_ = send_flags_t::none,
                  std::chrono::milliseconds timeout_ = {})
    {
        std::vector<message_t> parts;
        parts.push_back (std::move (part_));
        const bool submitted =
          request (parts, std::move (callback_), flags_, timeout_);
        if (!submitted && !parts.empty ())
            part_ = std::move (parts.front ());
        return submitted;
    }

    bool request (std::vector<message_t> &parts_,
                  std::function<void(request_result_t, std::vector<message_t>)> callback_,
                  send_flags_t flags_ = send_flags_t::none,
                  std::chrono::milliseconds timeout_ = {})
    {
        detail::request_state_t *state =
          detail::make_callback_request_state (std::move (callback_));
        std::vector<zlink_msg_t> native;
        if (detail::move_parts_to_native (parts_, native) != 0) {
            delete state;
            throw last_error ();
        }
        size_t failed_index = 0;
        const int rc = detail::submit_native_parts (
          native, failed_index,
          [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_, bool is_final_) {
              return zlink_dealer_request_part (
                handle (), part_out_, static_cast<zlink_send_flags_t> (flags_),
                part_flag_,
                is_final_
                  ? static_cast<uint32_t> (
                      detail::resolve_timeout (timeout_, _default_request_timeout)
                        .count ())
                  : 0u,
                is_final_ ? &detail::request_callback_trampoline : NULL,
                is_final_ ? state : NULL);
          });
        if (rc != 0) {
            detail::close_native_parts (native, failed_index);
            delete state;
            const submit_error_t err (
              static_cast<submit_result_t> (rc), zlink_errno ());
            if (flags_ == send_flags_t::dontwait
                && err.result () == submit_result_t::backpressured)
                return false;
            throw err;
        }
        return true;
    }

    void set_routing_id (const routing_id_t &routing_id_)
    {
        if (base_socket_t::set_routing_id_raw (
              routing_id_.data (), routing_id_.size ())
            != 0)
            detail::throw_config_error_from_errno (zlink_errno ());
    }

    void get_routing_id (routing_id_t &routing_id_) const
    {
        if (base_socket_t::get_routing_id_raw (routing_id_) != 0)
            detail::throw_config_error_from_errno (zlink_errno ());
    }

    void channel_name (const std::string &value_)
    {
        detail::validate_bounded_c_string (value_, 255u, "channel_name");
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (
            zlink_socket_set_channel_name (handle (), value_.c_str ())));
    }

    std::string channel_name () const
    {
        size_t size = 0;
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (
            zlink_socket_get_channel_name (
              const_cast<void *> (handle ()), NULL, 0, &size)));
        std::vector<char> buffer (size);
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (
            zlink_socket_get_channel_name (
              const_cast<void *> (handle ()),
              buffer.empty () ? NULL : buffer.data (), buffer.size (),
              &size)));
        return std::string (buffer.data (), size);
    }

    dealer_socket_options_t options ()
    {
        return dealer_socket_options_t (handle ());
    }

    template<typename DiscoveryT>
    void attach_discovery (DiscoveryT &discovery_)
    {
        if (base_socket_t::attach_discovery (discovery_) != 0)
            detail::throw_config_error_from_errno (zlink_errno ());
    }

  private:
    std::chrono::milliseconds _default_request_timeout;
    using message_socket_t::recv;
    using message_socket_t::send;
};

class router_socket_t : public routed_message_socket_t
{
  public:
    explicit router_socket_t (context_t &ctx_)
        : routed_message_socket_t (ctx_, socket_type::router),
          _default_request_timeout (std::chrono::milliseconds ())
    {
    }

    bool send (const routing_id_t &target_rid_, message_t &part_,
               send_flags_t flags_ = send_flags_t::none)
    {
        if (flags_ == send_flags_t::dontwait) {
            if (!part_.valid ())
                throw submit_error_t (submit_result_t::invalid_argument, EINVAL);

            zlink_msg_t native_part;
            detail::move_to_native (part_, &native_part);

            const int rc = zlink_send_part_rid (
              handle (), zlink::detail::routing_id_native (target_rid_),
              &native_part, ZLINK_DONTWAIT, ZLINK_PART_FINAL);
            if (rc == 0)
                return true;

            const int err = zlink_errno ();
            part_.init ();
            if (part_.valid ())
                (void) zlink_msg_move (detail::native_handle (part_), &native_part);
            (void) zlink_msg_close (&native_part);

            send_result_t result = send_result_t::sent;
            if (detail::classify_nonblocking_send_errno (err, result)) {
                if (result == send_result_t::backpressured)
                    return false;
                if (result == send_result_t::not_ready)
                    throw submit_error_t (submit_result_t::not_connected, err);
            }
            throw submit_error_t (detail::submit_result_from_errno (err), err);
        }
        const submit_result_t rc = static_cast<submit_result_t> (
          base_socket_t::send (target_rid_, part_,
                                flags_));
        if (rc != submit_result_t::ok)
            throw submit_error_t (rc, zlink_errno ());
        return true;
    }

    bool send (const routing_id_t &target_rid_, std::vector<message_t> &parts_,
               send_flags_t flags_ = send_flags_t::none)
    {
        if (flags_ == send_flags_t::dontwait) {
            send_result_t result = send_result_t::sent;
            if (base_socket_t::send_no_wait_result (result, target_rid_, parts_) != 0)
                detail::throw_submit_error_from_errno (zlink_errno ());
            if (result == send_result_t::not_ready)
                throw submit_error_t (submit_result_t::not_connected, zlink_errno ());
            return result == send_result_t::sent;
        }
        const submit_result_t rc = static_cast<submit_result_t> (
          base_socket_t::send (target_rid_, parts_,
                                flags_));
        if (rc != submit_result_t::ok)
            throw submit_error_t (rc, zlink_errno ());
        return true;
    }

    std::optional<received_t> recv (recv_flags_t flags_ = recv_flags_t::none)
    {
        try {
            return std::optional<received_t> (
              detail::recv_router_received (handle (), flags_));
        }
        catch (const recv_error_t &err) {
            if (flags_ == recv_flags_t::dontwait
                && err.result () == recv_result_t::no_data)
                return std::nullopt;
            throw;
        }
    }

    void on_send_ready (std::function<void()> handler_)
    {
        base_socket_t::on_send_ready (std::move (handler_));
    }

    async_result_t<std::vector<message_t>> request (const routing_id_t &routing_id_,
                                                    message_t &part_,
                                                    std::chrono::milliseconds timeout_ = {})
    {
        std::vector<message_t> parts;
        parts.push_back (std::move (part_));
        return request (routing_id_, parts, timeout_);
    }

    async_result_t<std::vector<message_t>>
    request (const routing_id_t &routing_id_,
             std::vector<message_t> &parts_,
             std::chrono::milliseconds timeout_ = {})
    {
        detail::request_state_t *state = detail::make_future_request_state ();
        std::future<std::vector<message_t>> future =
          state->promise->get_future ();
        std::vector<zlink_msg_t> native;
        if (detail::move_parts_to_native (parts_, native) != 0) {
            delete state;
            throw last_error ();
        }
        size_t failed_index = 0;
        const int rc = detail::submit_native_parts (
          native, failed_index,
          [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_, bool is_final_) {
              return zlink_router_request_part (
                handle (), zlink::detail::routing_id_native (routing_id_), part_out_,
                ZLINK_SEND_FLAGS_NONE, part_flag_,
                is_final_
                  ? static_cast<uint32_t> (
                      detail::resolve_timeout (timeout_, _default_request_timeout)
                        .count ())
                  : 0u,
                is_final_ ? &detail::request_callback_trampoline : NULL,
                is_final_ ? state : NULL);
          });
        if (rc != 0) {
            detail::close_native_parts (native, failed_index);
            delete state;
            throw last_error ();
        }
        return async_result_t<std::vector<message_t>> (
          std::move (future), detail::make_socket_request_progress (handle ()));
    }

    bool request (const routing_id_t &routing_id_,
                  message_t &part_,
                  std::function<void(request_result_t, std::vector<message_t>)> callback_,
                  send_flags_t flags_ = send_flags_t::none,
                  std::chrono::milliseconds timeout_ = {})
    {
        std::vector<message_t> parts;
        parts.push_back (std::move (part_));
        const bool submitted =
          request (routing_id_, parts, std::move (callback_), flags_, timeout_);
        if (!submitted && !parts.empty ())
            part_ = std::move (parts.front ());
        return submitted;
    }

    bool request (const routing_id_t &routing_id_,
                  std::vector<message_t> &parts_,
                  std::function<void(request_result_t, std::vector<message_t>)> callback_,
                  send_flags_t flags_ = send_flags_t::none,
                  std::chrono::milliseconds timeout_ = {})
    {
        detail::request_state_t *state =
          detail::make_callback_request_state (std::move (callback_));
        std::vector<zlink_msg_t> native;
        if (detail::move_parts_to_native (parts_, native) != 0) {
            delete state;
            throw last_error ();
        }
        size_t failed_index = 0;
        const int rc = detail::submit_native_parts (
          native, failed_index,
          [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_, bool is_final_) {
              return zlink_router_request_part (
                handle (), zlink::detail::routing_id_native (routing_id_), part_out_,
                static_cast<zlink_send_flags_t> (flags_), part_flag_,
                is_final_
                  ? static_cast<uint32_t> (
                      detail::resolve_timeout (timeout_, _default_request_timeout)
                        .count ())
                  : 0u,
                is_final_ ? &detail::request_callback_trampoline : NULL,
                is_final_ ? state : NULL);
          });
        if (rc != 0) {
            detail::close_native_parts (native, failed_index);
            delete state;
            const submit_error_t err (
              static_cast<submit_result_t> (rc), zlink_errno ());
            if (flags_ == send_flags_t::dontwait
                && err.result () == submit_result_t::backpressured)
                return false;
            throw err;
        }
        return true;
    }

    void reply (const routing_id_t &routing_id_,
                uint64_t request_seq_,
                message_t &part_,
                send_flags_t flags_ = send_flags_t::none)
    {
        std::vector<message_t> parts;
        parts.push_back (std::move (part_));
        reply (routing_id_, request_seq_, parts, flags_);
    }

    void reply (const routing_id_t &routing_id_,
                uint64_t request_seq_,
                std::vector<message_t> &parts_,
                send_flags_t flags_ = send_flags_t::none)
    {
        detail::throw_if_reply_flags_unsupported (flags_);
        std::vector<zlink_msg_t> native;
        if (detail::move_parts_to_native (parts_, native) != 0)
            throw last_error ();
        size_t failed_index = 0;
        const int rc = detail::submit_native_parts (
          native, failed_index,
          [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_, bool) {
              return zlink_router_reply_part (
                handle (), zlink::detail::routing_id_native (routing_id_), request_seq_,
                part_out_, part_flag_);
          });
        if (rc != 0) {
            detail::restore_parts_from_native (parts_, native, failed_index);
            throw last_error ();
        }
    }

    void set_routing_id (const routing_id_t &routing_id_)
    {
        if (base_socket_t::set_routing_id_raw (
              routing_id_.data (), routing_id_.size ())
            != 0)
            detail::throw_config_error_from_errno (zlink_errno ());
    }

    void get_routing_id (routing_id_t &routing_id_) const
    {
        if (base_socket_t::get_routing_id_raw (routing_id_) != 0)
            detail::throw_config_error_from_errno (zlink_errno ());
    }

    bool send_to_spot (const routing_id_t &dest_node_rid_,
                       const routing_id_t &dest_spot_rid_,
                       message_t &part_,
                       send_flags_t flags_ = send_flags_t::none)
    {
        std::vector<message_t> parts;
        parts.push_back (std::move (part_));
        const bool submitted =
          send_to_spot (dest_node_rid_, dest_spot_rid_, parts, flags_);
        if (!submitted && !parts.empty ())
            part_ = std::move (parts.front ());
        return submitted;
    }

    bool send_to_spot (const routing_id_t &dest_node_rid_,
                       const routing_id_t &dest_spot_rid_,
                       std::vector<message_t> &parts_,
                       send_flags_t flags_ = send_flags_t::none)
    {
        std::vector<zlink_msg_t> native;
        if (detail::move_parts_to_native (parts_, native) != 0)
            throw last_error ();
        size_t failed_index = 0;
        const submit_result_t rc = static_cast<submit_result_t> (
          detail::submit_native_parts (
            native, failed_index,
            [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_, bool) {
                return zlink_router_send_spot_part (
                  handle (), zlink::detail::routing_id_native (dest_node_rid_),
                  zlink::detail::routing_id_native (dest_spot_rid_), part_out_,
                  static_cast<zlink_send_flags_t> (flags_), part_flag_);
            }));
        if (rc != submit_result_t::ok) {
            detail::restore_parts_from_native (parts_, native, failed_index);
            if (flags_ == send_flags_t::dontwait
                && rc == submit_result_t::backpressured)
                return false;
            throw submit_error_t (rc, zlink_errno ());
        }
        return true;
    }

    async_result_t<std::vector<message_t>>
    request_to_spot (const routing_id_t &dest_node_rid_,
                     const routing_id_t &dest_spot_rid_,
                     message_t message_,
                     std::chrono::milliseconds timeout_ = {})
    {
        detail::request_state_t *state = detail::make_future_request_state ();
        std::future<std::vector<message_t>> future =
          state->promise->get_future ();
        std::vector<message_t> parts;
        parts.push_back (std::move (message_));
        std::vector<zlink_msg_t> native;
        if (detail::move_parts_to_native (parts, native) != 0) {
            delete state;
            throw last_error ();
        }
        size_t failed_index = 0;
        const submit_result_t rc = static_cast<submit_result_t> (
          detail::submit_native_parts (
            native, failed_index,
            [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_, bool is_final_) {
                return zlink_router_request_spot_part (
                  handle (), zlink::detail::routing_id_native (dest_node_rid_),
                  zlink::detail::routing_id_native (dest_spot_rid_), part_out_,
                  is_final_ ? &detail::request_callback_trampoline : NULL,
                  is_final_ ? state : NULL, ZLINK_SEND_FLAGS_NONE, part_flag_,
                  is_final_
                    ? static_cast<uint32_t> (
                        detail::resolve_timeout (timeout_, _default_request_timeout)
                          .count ())
                    : 0u);
            }));
        if (rc != submit_result_t::ok) {
            detail::close_native_parts (native, failed_index);
            delete state;
            throw submit_error_t (rc, zlink_errno ());
        }
        return async_result_t<std::vector<message_t>> (
          std::move (future), detail::make_socket_request_progress (handle ()));
    }

    bool request_to_spot (
      const routing_id_t &dest_node_rid_,
      const routing_id_t &dest_spot_rid_,
      message_t message_,
      std::function<void(request_result_t, std::vector<message_t>)> callback_,
      send_flags_t flags_ = send_flags_t::none,
      std::chrono::milliseconds timeout_ = {})
    {
        detail::request_state_t *state =
          detail::make_callback_request_state (std::move (callback_));
        std::vector<message_t> parts;
        parts.push_back (std::move (message_));
        std::vector<zlink_msg_t> native;
        if (detail::move_parts_to_native (parts, native) != 0) {
            delete state;
            throw last_error ();
        }
        size_t failed_index = 0;
        const submit_result_t rc = static_cast<submit_result_t> (
          detail::submit_native_parts (
            native, failed_index,
            [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_, bool is_final_) {
                return zlink_router_request_spot_part (
                  handle (), zlink::detail::routing_id_native (dest_node_rid_),
                  zlink::detail::routing_id_native (dest_spot_rid_), part_out_,
                  is_final_ ? &detail::request_callback_trampoline : NULL,
                  is_final_ ? state : NULL,
                  static_cast<zlink_send_flags_t> (flags_), part_flag_,
                  is_final_
                    ? static_cast<uint32_t> (
                        detail::resolve_timeout (timeout_, _default_request_timeout)
                          .count ())
                    : 0u);
            }));
        if (rc != submit_result_t::ok) {
            detail::close_native_parts (native, failed_index);
            delete state;
            if (flags_ == send_flags_t::dontwait
                && rc == submit_result_t::backpressured)
                return false;
            throw submit_error_t (rc, zlink_errno ());
        }
        return true;
    }

    void reply_to_spot (const routing_id_t &dest_node_rid_,
                        const routing_id_t &dest_spot_rid_,
                        uint64_t request_seq_,
                        message_t message_,
                        send_flags_t flags_ = send_flags_t::none)
    {
        detail::throw_if_reply_flags_unsupported (flags_);
        std::vector<message_t> parts;
        parts.push_back (std::move (message_));
        std::vector<zlink_msg_t> native;
        if (detail::move_parts_to_native (parts, native) != 0)
            throw last_error ();
        size_t failed_index = 0;
        const submit_result_t rc = static_cast<submit_result_t> (
          detail::submit_native_parts (
            native, failed_index,
            [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_, bool) {
                return zlink_router_reply_spot_part (
                  handle (), zlink::detail::routing_id_native (dest_node_rid_),
                  zlink::detail::routing_id_native (dest_spot_rid_), request_seq_, part_out_,
                  part_flag_);
            }));
        if (rc != submit_result_t::ok) {
            detail::close_native_parts (native, failed_index);
            throw submit_error_t (rc, zlink_errno ());
        }
    }

    template<typename DiscoveryT>
    void attach_discovery (DiscoveryT &discovery_)
    {
        if (base_socket_t::attach_discovery (discovery_) != 0)
            detail::throw_config_error_from_errno (zlink_errno ());
    }

    router_socket_options_t options ()
    {
        return router_socket_options_t (handle ());
    }

  private:
    std::chrono::milliseconds _default_request_timeout;
    using routed_message_socket_t::recv;
    using routed_message_socket_t::send;
};

class stream_socket_t : public routed_message_socket_t
{
  public:
    explicit stream_socket_t (context_t &ctx_)
        : routed_message_socket_t (ctx_, socket_type::stream)
    {
    }

    bool send (const routing_id_t &target_rid_, message_t &part_,
               send_flags_t flags_ = send_flags_t::none)
    {
        if (flags_ == send_flags_t::dontwait) {
            if (!part_.valid ())
                throw submit_error_t (submit_result_t::invalid_argument, EINVAL);

            zlink_msg_t native_part;
            detail::move_to_native (part_, &native_part);

            const int rc = zlink_send_part_rid (
              handle (), zlink::detail::routing_id_native (target_rid_),
              &native_part, ZLINK_DONTWAIT, ZLINK_PART_FINAL);
            if (rc == 0)
                return true;

            const int err = zlink_errno ();
            part_.adopt (&native_part);

            send_result_t result = send_result_t::sent;
            if (detail::classify_nonblocking_send_errno (err, result)) {
                if (result == send_result_t::backpressured)
                    return false;
                if (result == send_result_t::not_ready)
                    throw submit_error_t (submit_result_t::not_connected, err);
            }
            throw submit_error_t (detail::submit_result_from_errno (err), err);
        }
        const submit_result_t rc = static_cast<submit_result_t> (
          base_socket_t::send (target_rid_, part_,
                                flags_));
        if (rc != submit_result_t::ok)
            throw submit_error_t (rc, zlink_errno ());
        return true;
    }

    bool send (const routing_id_t &target_rid_, std::vector<message_t> &parts_,
               send_flags_t flags_ = send_flags_t::none)
    {
        if (flags_ == send_flags_t::dontwait) {
            send_result_t result = send_result_t::sent;
            if (base_socket_t::send_no_wait_result (result, target_rid_, parts_) != 0)
                detail::throw_submit_error_from_errno (zlink_errno ());
            if (result == send_result_t::not_ready)
                throw submit_error_t (submit_result_t::not_connected, zlink_errno ());
            return result == send_result_t::sent;
        }
        const submit_result_t rc = static_cast<submit_result_t> (
          base_socket_t::send (target_rid_, parts_,
                                flags_));
        if (rc != submit_result_t::ok)
            throw submit_error_t (rc, zlink_errno ());
        return true;
    }

    std::optional<received_t> recv (recv_flags_t flags_ = recv_flags_t::none)
    {
        received_t received;
        const recv_result_t rc = static_cast<recv_result_t> (
          base_socket_t::receive (received, flags_));
        if (rc == recv_result_t::no_data && flags_ == recv_flags_t::dontwait)
            return std::nullopt;
        if (rc != recv_result_t::ok)
            throw recv_error_t (rc, zlink_errno ());
        return std::optional<received_t> (std::move (received));
    }

    template<class Handler>
    void on_packet (Handler &&handler_)
    {
        _packet_handler.reset (
          new packet_handler_model_t<typename std::decay<Handler>::type> (
            std::forward<Handler> (handler_)));
        if (base_socket_t::on_packet (
              &stream_socket_t::packet_trampoline, this)
            != 0)
            detail::throw_handler_error_from_errno (zlink_errno ());
    }

    void on_send_ready (std::function<void()> handler_)
    {
        base_socket_t::on_send_ready (std::move (handler_));
    }

    void set_routing_id (const routing_id_t &routing_id_)
    {
        if (base_socket_t::set_routing_id_raw (
              routing_id_.data (), routing_id_.size ())
            != 0)
            detail::throw_config_error_from_errno (zlink_errno ());
    }

    void get_routing_id (routing_id_t &routing_id_) const
    {
        if (base_socket_t::get_routing_id_raw (routing_id_) != 0)
            detail::throw_config_error_from_errno (zlink_errno ());
    }

    stream_socket_options_t options ()
    {
        return stream_socket_options_t (handle ());
    }

    template<typename SpotNodeT>
    void bind_actor (SpotNodeT &node_,
                     const routing_id_t &session_rid_,
                     const actor_ref_t &actor_,
                     std::chrono::milliseconds timeout_ = {})
    {
        detail::throw_if_failed<request_error_t> (
          static_cast<request_result_t> (
            zlink_stream_bind_actor (
              zlink::detail::native_handle (node_), handle (), zlink::detail::routing_id_native (session_rid_),
              zlink::detail::actor_ref_native (actor_),
              static_cast<uint32_t> (timeout_.count ()))));
    }

    template<typename SpotNodeT>
    void unbind_actor (SpotNodeT &node_,
                       const routing_id_t &session_rid_,
                       const std::string &actor_id_,
                       std::chrono::milliseconds timeout_ = {})
    {
        detail::validate_bounded_c_string (actor_id_, ZLINK_ACTOR_ID_MAX - 1u,
                                   "actor_id");
        detail::throw_if_failed<request_error_t> (
          static_cast<request_result_t> (
            zlink_stream_unbind_actor (
              zlink::detail::native_handle (node_), handle (), zlink::detail::routing_id_native (session_rid_),
              actor_id_.c_str (), static_cast<uint32_t> (timeout_.count ()))));
    }

    template<typename SpotNodeT>
    bool send_bound_actor (SpotNodeT &node_,
                           const routing_id_t &session_rid_,
                           const std::string &actor_id_,
                           message_t &part_,
                           send_flags_t flags_ = send_flags_t::none)
    {
        std::vector<message_t> parts;
        parts.push_back (std::move (part_));
        const bool submitted =
          send_bound_actor (node_, session_rid_, actor_id_, parts, flags_);
        if (!submitted && !parts.empty ())
            part_ = std::move (parts.front ());
        return submitted;
    }

    template<typename SpotNodeT>
    bool send_bound_actor (SpotNodeT &node_,
                           const routing_id_t &session_rid_,
                           const std::string &actor_id_,
                           std::vector<message_t> &parts_,
                           send_flags_t flags_ = send_flags_t::none)
    {
        detail::validate_bounded_c_string (actor_id_, ZLINK_ACTOR_ID_MAX - 1u,
                                   "actor_id");
        std::vector<zlink_msg_t> native;
        if (detail::move_parts_to_native (parts_, native) != 0)
            throw last_error ();
        size_t failed_index = 0;
        const submit_result_t rc = static_cast<submit_result_t> (
          detail::submit_native_parts (
            native, failed_index,
            [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_, bool) {
                return zlink_stream_send_bound_actor_part (
                  zlink::detail::native_handle (node_), handle (),
                  zlink::detail::routing_id_native (session_rid_),
                  actor_id_.c_str (), part_out_,
                  static_cast<zlink_send_flags_t> (flags_), part_flag_);
            }));
        if (rc != submit_result_t::ok) {
            detail::restore_parts_from_native (parts_, native, failed_index);
            if (flags_ == send_flags_t::dontwait
                && rc == submit_result_t::backpressured)
                return false;
            throw submit_error_t (rc, zlink_errno ());
        }
        return true;
    }

  private:
    static void packet_trampoline (void *,
                                   const zlink_routing_id_t *source_rid_,
                                   zlink_msg_t *header_,
                                   zlink_msg_t *body_,
                                   void *userdata_)
    {
        stream_socket_t *self = static_cast<stream_socket_t *> (userdata_);
        if (!self || !self->_packet_handler)
            return;
        const routing_id_t source =
          (source_rid_ && source_rid_->size > 0)
            ? zlink::detail::borrowed_routing_id (*source_rid_)
            : zlink::detail::unchecked_empty_routing_id ();
        message_t header {message_t::no_init_t ()};
        message_t body {message_t::no_init_t ()};
        header.adopt (header_);
        body.adopt (body_);
        self->_packet_handler->call (
          source, std::move (header), std::move (body));
    }

    struct packet_handler_base_t
    {
        virtual ~packet_handler_base_t () {}
        virtual void call (const routing_id_t &source_,
                           message_t &&header_,
                           message_t &&body_) = 0;
    };

    template<class Handler>
    struct packet_handler_model_t : packet_handler_base_t
    {
        explicit packet_handler_model_t (Handler &&handler_)
            : handler (std::move (handler_))
        {
        }

        explicit packet_handler_model_t (const Handler &handler_)
            : handler (handler_)
        {
        }

        void call (const routing_id_t &source_,
                   message_t &&header_,
                   message_t &&body_) override
        {
            handler (
              source_, std::move (header_), std::move (body_));
        }

        Handler handler;
    };

    std::unique_ptr<packet_handler_base_t> _packet_handler;
    using routed_message_socket_t::recv;
    using routed_message_socket_t::send;
    using base_socket_t::connect;
    using base_socket_t::disconnect;
    using base_socket_t::disconnect_rid;
};

class pub_socket_t : public publisher_socket_t
{
  public:
    explicit pub_socket_t (context_t &ctx_)
        : publisher_socket_t (ctx_, socket_type::pub)
    {
    }

    bool publish (const std::string &topic_id_, message_t &part_,
                  send_flags_t flags_ = send_flags_t::none)
    {
        const submit_result_t rc = static_cast<submit_result_t> (
          base_socket_t::publish (topic_id_, part_,
                                  flags_));
        if (rc != submit_result_t::ok)
        {
            if (flags_ == send_flags_t::dontwait
                && rc == submit_result_t::backpressured)
                return false;
            throw submit_error_t (rc, zlink_errno ());
        }
        return true;
    }

    bool publish (const std::string &topic_id_, std::vector<message_t> &parts_,
                  send_flags_t flags_ = send_flags_t::none)
    {
        const submit_result_t rc = static_cast<submit_result_t> (
          base_socket_t::publish (topic_id_, parts_,
                                  flags_));
        if (rc != submit_result_t::ok)
        {
            if (flags_ == send_flags_t::dontwait
                && rc == submit_result_t::backpressured)
                return false;
            throw submit_error_t (rc, zlink_errno ());
        }
        return true;
    }

    void on_send_ready (std::function<void()> handler_)
    {
        base_socket_t::on_send_ready (std::move (handler_));
    }

    template<typename DiscoveryT>
    void attach_discovery (DiscoveryT &discovery_)
    {
        if (base_socket_t::attach_discovery (discovery_) != 0)
            detail::throw_config_error_from_errno (zlink_errno ());
    }

    pub_socket_options_t options ()
    {
        return pub_socket_options_t (handle ());
    }

  private:
    using publisher_socket_t::on_send_ready;
    using publisher_socket_t::publish;
};

class xpub_socket_t : public publisher_socket_t
{
  public:
    explicit xpub_socket_t (context_t &ctx_)
        : publisher_socket_t (ctx_, socket_type::xpub)
    {
    }

    bool publish (const std::string &topic_id_, message_t &part_,
                  send_flags_t flags_ = send_flags_t::none)
    {
        const submit_result_t rc = static_cast<submit_result_t> (
          base_socket_t::publish (topic_id_, part_,
                                  flags_));
        if (rc != submit_result_t::ok)
        {
            if (flags_ == send_flags_t::dontwait
                && rc == submit_result_t::backpressured)
                return false;
            throw submit_error_t (rc, zlink_errno ());
        }
        return true;
    }

    bool publish (const std::string &topic_id_, std::vector<message_t> &parts_,
                  send_flags_t flags_ = send_flags_t::none)
    {
        const submit_result_t rc = static_cast<submit_result_t> (
          base_socket_t::publish (topic_id_, parts_,
                                  flags_));
        if (rc != submit_result_t::ok)
        {
            if (flags_ == send_flags_t::dontwait
                && rc == submit_result_t::backpressured)
                return false;
            throw submit_error_t (rc, zlink_errno ());
        }
        return true;
    }

    void on_send_ready (std::function<void()> handler_)
    {
        base_socket_t::on_send_ready (std::move (handler_));
    }

    std::optional<subscription_event_t> receive_subscription_event (
      recv_flags_t flags_ = recv_flags_t::none)
    {
        subscription_event_t event;
        std::vector<char> topic_buffer (256);
        size_t topic_size = topic_buffer.size ();
        const zlink_routing_id_t *source_rid = NULL;
        int subscribed = 0;
        zlink_recv_result_t rc = ZLINK_RECV_INTERNAL_ERROR;

        while (true) {
            rc = zlink_xpub_recv_part (
              handle (), &source_rid, &subscribed, topic_buffer.data (),
              topic_buffer.size (), &topic_size,
              static_cast<zlink_recv_flags_t> (flags_));
            if (rc == ZLINK_RECV_OK)
                break;
            if (zlink_errno () != EMSGSIZE)
                break;
            topic_buffer.resize (topic_size);
        }

        if (rc == ZLINK_RECV_OK) {
            if (source_rid && source_rid->size > 0)
                event.routing_id = zlink::detail::native_routing_id (*source_rid);
            event.subscribed = subscribed != 0;
            event.topic.assign (topic_buffer.data (), topic_size);
        }

        const recv_result_t result = static_cast<recv_result_t> (rc);
        if (result == recv_result_t::no_data && flags_ == recv_flags_t::dontwait)
            return std::nullopt;
        if (result != recv_result_t::ok)
            throw recv_error_t (result, zlink_errno ());
        return std::optional<subscription_event_t> (std::move (event));
    }

    pub_socket_options_t options ()
    {
        return pub_socket_options_t (handle ());
    }

  private:
    using publisher_socket_t::on_send_ready;
    using publisher_socket_t::publish;
};

class sub_socket_t : public subscriber_socket_t
{
  public:
    explicit sub_socket_t (context_t &ctx_)
        : subscriber_socket_t (ctx_, socket_type::sub)
    {
    }

    void set_subscription (const std::string &filter_)
    {
        if (base_socket_t::set_subscription (filter_) != 0)
            detail::throw_config_error_from_errno (zlink_errno ());
    }

    void unset_subscription (const std::string &filter_)
    {
        if (base_socket_t::unset_subscription (filter_) != 0)
            detail::throw_config_error_from_errno (zlink_errno ());
    }

    void subscription_at (size_t index_, std::string &filter_out_,
                          bool *is_pattern_out_ = NULL)
    {
        if (base_socket_t::subscription_at (index_, filter_out_, is_pattern_out_) != 0)
            detail::throw_config_error_from_errno (zlink_errno ());
    }

    subscription_filter_t subscription_at (size_t index_)
    {
        subscription_filter_t filter;
        subscription_at (index_, filter.filter, &filter.is_pattern);
        return filter;
    }

    std::optional<topic_message_t> subscribe (recv_flags_t flags_ = recv_flags_t::none)
    {
        topic_message_t message;
        const recv_result_t rc = static_cast<recv_result_t> (
          base_socket_t::subscribe (message, flags_));
        if (rc == recv_result_t::no_data && flags_ == recv_flags_t::dontwait)
            return std::nullopt;
        if (rc != recv_result_t::ok)
            throw recv_error_t (rc, zlink_errno ());
        return std::optional<topic_message_t> (std::move (message));
    }

    template<typename DiscoveryT>
    void attach_discovery (DiscoveryT &discovery_)
    {
        if (base_socket_t::attach_discovery (discovery_) != 0)
            detail::throw_config_error_from_errno (zlink_errno ());
    }

    sub_socket_options_t options ()
    {
        return sub_socket_options_t (handle ());
    }

  private:
    using subscriber_socket_t::set_subscription;
    using subscriber_socket_t::subscribe;
    using subscriber_socket_t::subscription_at;
    using subscriber_socket_t::unset_subscription;
};

class xsub_socket_t : public subscriber_socket_t
{
  public:
    explicit xsub_socket_t (context_t &ctx_)
        : subscriber_socket_t (ctx_, socket_type::xsub)
    {
    }

    void set_subscription (const std::string &filter_)
    {
        if (base_socket_t::set_subscription (filter_) != 0)
            detail::throw_config_error_from_errno (zlink_errno ());
    }

    void unset_subscription (const std::string &filter_)
    {
        if (base_socket_t::unset_subscription (filter_) != 0)
            detail::throw_config_error_from_errno (zlink_errno ());
    }

    void subscription_at (size_t index_, std::string &filter_out_,
                          bool *is_pattern_out_ = NULL)
    {
        if (base_socket_t::subscription_at (index_, filter_out_, is_pattern_out_) != 0)
            detail::throw_config_error_from_errno (zlink_errno ());
    }

    subscription_filter_t subscription_at (size_t index_)
    {
        subscription_filter_t filter;
        subscription_at (index_, filter.filter, &filter.is_pattern);
        return filter;
    }

    std::optional<topic_message_t> subscribe (recv_flags_t flags_ = recv_flags_t::none)
    {
        topic_message_t message;
        const recv_result_t rc = static_cast<recv_result_t> (
          base_socket_t::subscribe (message, flags_));
        if (rc == recv_result_t::no_data && flags_ == recv_flags_t::dontwait)
            return std::nullopt;
        if (rc != recv_result_t::ok)
            throw recv_error_t (rc, zlink_errno ());
        return std::optional<topic_message_t> (std::move (message));
    }

    sub_socket_options_t options ()
    {
        return sub_socket_options_t (handle ());
    }

  private:
    using subscriber_socket_t::set_subscription;
    using subscriber_socket_t::subscribe;
    using subscriber_socket_t::subscription_at;
    using subscriber_socket_t::unset_subscription;
};

} // namespace zlink

#endif
