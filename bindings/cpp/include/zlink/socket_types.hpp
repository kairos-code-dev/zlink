/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_SOCKET_TYPES_HPP_INCLUDED
#define ZLINK_CPP_SOCKET_TYPES_HPP_INCLUDED

#include "async_result.hpp"
#include "message_socket.hpp"
#include "publisher_socket.hpp"
#include "subscriber_socket.hpp"
#include "error.hpp"

namespace zlink
{

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
        (void) zlink_msg_move (parts[i].handle (), &parts_[i]);
    return parts;
}

struct request_state_t
{
    std::promise<std::vector<message_t>> promise;
    std::function<void(request_result_t, std::vector<message_t>)> on_complete;
};

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
        holder->promise.set_exception (
          std::make_exception_ptr (
            request_error_t (static_cast<request_result_t> (result_))));
        return;
    }

    std::vector<message_t> parts = take_parts (parts_, part_count_);
    if (holder->on_complete)
        holder->on_complete (request_result_t::ok, parts);
    holder->promise.set_value (std::move (parts));
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
        ? std::optional<routing_id_t> (routing_id_t (*routing_id_))
        : std::nullopt,
      (spot_rid_ && spot_rid_->size > 0)
        ? std::optional<routing_id_t> (routing_id_t (*spot_rid_))
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
    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    const recv_result_t rc = static_cast<recv_result_t> (zlink_router_recv (
      router_handle_, &source_node_rid, &source_spot_rid, &request_seq, &parts,
      &part_count, static_cast<zlink_recv_flags_t> (flags_)));
    if (rc != recv_result_t::ok)
        throw recv_error_t (rc, zlink_errno ());

    std::function<void(std::vector<message_t> &, send_flags_t)> reply_fn;
    if (source_node_rid && source_node_rid->size > 0 && request_seq != 0u) {
        const routing_id_t reply_node_rid (*source_node_rid);
        if (source_spot_rid && source_spot_rid->size > 0) {
            const routing_id_t reply_spot_rid (*source_spot_rid);
            reply_fn = [router_handle_, reply_node_rid, reply_spot_rid,
                        request_seq] (
                         std::vector<message_t> &reply_parts_,
                         send_flags_t flags_) {
                detail::throw_if_reply_flags_unsupported (flags_);
                std::vector<zlink_msg_t> native;
                if (detail::move_parts_to_native (reply_parts_, native) != 0)
                    throw last_error ();
                const submit_result_t result = static_cast<submit_result_t> (
                  zlink_router_reply_spot (
                    router_handle_, routing_id_native (reply_node_rid),
                    routing_id_native (reply_spot_rid), request_seq,
                    native.data (), native.size ()));
                if (result != submit_result_t::ok)
                    throw submit_error_t (result, zlink_errno ());
            };
        } else {
            reply_fn = [router_handle_, reply_node_rid, request_seq] (
                         std::vector<message_t> &reply_parts_,
                         send_flags_t flags_) {
                detail::throw_if_reply_flags_unsupported (flags_);
                std::vector<zlink_msg_t> native;
                if (detail::move_parts_to_native (reply_parts_, native) != 0)
                    throw last_error ();
                const submit_result_t result = static_cast<submit_result_t> (
                  zlink_router_reply (
                    router_handle_, routing_id_native (reply_node_rid),
                    request_seq, native.data (), native.size ()));
                if (result != submit_result_t::ok)
                    throw submit_error_t (result, zlink_errno ());
            };
        }
    }
    return make_received (
      source_node_rid, source_spot_rid, request_seq, request_seq != 0u, parts,
      part_count, std::move (reply_fn));
}

} // namespace detail

class pair_socket_t : public message_socket_t
{
  public:
    explicit pair_socket_t (context_t &ctx_) : message_socket_t (ctx_, socket_type::pair) {}

    void send (message_t &part_, send_flags_t flags_ = send_flags_t::none)
    {
        const submit_result_t rc = static_cast<submit_result_t> (
          base_socket_t::send (part_, flags_));
        if (rc != submit_result_t::ok)
            throw submit_error_t (rc, zlink_errno ());
    }

    void send (std::vector<message_t> &parts_, send_flags_t flags_ = send_flags_t::none)
    {
        const submit_result_t rc = static_cast<submit_result_t> (
          base_socket_t::send (parts_, flags_));
        if (rc != submit_result_t::ok)
            throw submit_error_t (rc, zlink_errno ());
    }

    received_t recv (recv_flags_t flags_ = recv_flags_t::none)
    {
        received_t received;
        const recv_result_t rc = static_cast<recv_result_t> (
          base_socket_t::receive (received, flags_));
        if (rc != recv_result_t::ok)
            throw recv_error_t (rc, zlink_errno ());
        return received;
    }

    void on_receive (zlink_socket_msg_handler_fn handler_, void *userdata_ = NULL)
    {
        if (base_socket_t::on_receive (handler_, userdata_) != 0)
            throw handler_error_t (handler_result_t::invalid_handle, zlink_errno ());
    }

    void on_send_ready (zlink_send_ready_handler_fn handler_, void *userdata_ = NULL)
    {
        if (base_socket_t::on_send_ready (handler_, userdata_) != 0)
            throw handler_error_t (handler_result_t::invalid_handle, zlink_errno ());
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

    void send (message_t &part_, send_flags_t flags_ = send_flags_t::none)
    {
        const submit_result_t rc = static_cast<submit_result_t> (
          base_socket_t::send (part_, flags_));
        if (rc != submit_result_t::ok)
            throw submit_error_t (rc, zlink_errno ());
    }

    void send (std::vector<message_t> &parts_, send_flags_t flags_ = send_flags_t::none)
    {
        const submit_result_t rc = static_cast<submit_result_t> (
          base_socket_t::send (parts_, flags_));
        if (rc != submit_result_t::ok)
            throw submit_error_t (rc, zlink_errno ());
    }

    received_t recv (recv_flags_t flags_ = recv_flags_t::none)
    {
        received_t received;
        const recv_result_t rc = static_cast<recv_result_t> (
          base_socket_t::receive (received, flags_));
        if (rc != recv_result_t::ok)
            throw recv_error_t (rc, zlink_errno ());
        return received;
    }

    void on_receive (zlink_socket_msg_handler_fn handler_, void *userdata_ = NULL)
    {
        if (base_socket_t::on_receive (handler_, userdata_) != 0)
            throw handler_error_t (handler_result_t::invalid_handle, zlink_errno ());
    }

    void on_send_ready (zlink_send_ready_handler_fn handler_, void *userdata_ = NULL)
    {
        if (base_socket_t::on_send_ready (handler_, userdata_) != 0)
            throw handler_error_t (handler_result_t::invalid_handle, zlink_errno ());
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
        detail::request_state_t *state = new detail::request_state_t ();
        std::future<std::vector<message_t>> future = state->promise.get_future ();
        std::vector<zlink_msg_t> native;
        if (detail::move_parts_to_native (parts_, native) != 0) {
            delete state;
            throw last_error ();
        }
        const int rc = zlink_dealer_request (
          handle (), native.data (), native.size (),
          &detail::request_callback_trampoline, state,
          ZLINK_SEND_FLAGS_NONE,
          static_cast<uint32_t> (
            detail::resolve_timeout (timeout_, _default_request_timeout)
              .count ()));
        if (rc != 0) {
            delete state;
            throw last_error ();
        }
        return async_result_t<std::vector<message_t>> (std::move (future));
    }

    void request (message_t &part_,
                  std::function<void(request_result_t, std::vector<message_t>)> callback_,
                  send_flags_t flags_ = send_flags_t::none,
                  std::chrono::milliseconds timeout_ = {})
    {
        std::vector<message_t> parts;
        parts.push_back (std::move (part_));
        request (parts, std::move (callback_), flags_, timeout_);
    }

    void request (std::vector<message_t> &parts_,
                  std::function<void(request_result_t, std::vector<message_t>)> callback_,
                  send_flags_t flags_ = send_flags_t::none,
                  std::chrono::milliseconds timeout_ = {})
    {
        detail::request_state_t *state = new detail::request_state_t ();
        state->on_complete = std::move (callback_);
        std::vector<zlink_msg_t> native;
        if (detail::move_parts_to_native (parts_, native) != 0) {
            delete state;
            throw last_error ();
        }
        const int rc = zlink_dealer_request (
          handle (), native.data (), native.size (),
          &detail::request_callback_trampoline, state,
          static_cast<zlink_send_flags_t> (flags_),
          static_cast<uint32_t> (
            detail::resolve_timeout (timeout_, _default_request_timeout)
              .count ()));
        if (rc != 0) {
            delete state;
            throw last_error ();
        }
    }

    void set_default_request_timeout (std::chrono::milliseconds timeout_)
    {
        _default_request_timeout = timeout_;
    }

    std::chrono::milliseconds get_default_request_timeout () const
    {
        return _default_request_timeout;
    }

    void set_routing_id (const routing_id_t &routing_id_)
    {
        if (base_socket_t::set_routing_id_raw (
              routing_id_.data (), routing_id_.size ())
            != 0)
            throw config_error_t (config_result_t::invalid_argument, zlink_errno ());
    }

    void get_routing_id (routing_id_t &routing_id_) const
    {
        if (base_socket_t::get_routing_id_raw (routing_id_) != 0)
            throw config_error_t (config_result_t::invalid_handle, zlink_errno ());
    }

    template<typename DiscoveryT>
    void attach_discovery (DiscoveryT &discovery_)
    {
        if (base_socket_t::attach_discovery (discovery_) != 0)
            throw config_error_t (config_result_t::invalid_handle, zlink_errno ());
    }

    dealer_socket_options_t dealer_options ()
    {
        return dealer_socket_options_t (handle ());
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

    void send (const routing_id_t &target_rid_, message_t &part_,
               send_flags_t flags_ = send_flags_t::none)
    {
        const submit_result_t rc = static_cast<submit_result_t> (
          base_socket_t::send (target_rid_, part_,
                                flags_));
        if (rc != submit_result_t::ok)
            throw submit_error_t (rc, zlink_errno ());
    }

    void send (const routing_id_t &target_rid_, std::vector<message_t> &parts_,
               send_flags_t flags_ = send_flags_t::none)
    {
        const submit_result_t rc = static_cast<submit_result_t> (
          base_socket_t::send (target_rid_, parts_,
                                flags_));
        if (rc != submit_result_t::ok)
            throw submit_error_t (rc, zlink_errno ());
    }

    received_t recv (recv_flags_t flags_ = recv_flags_t::none)
    {
        return detail::recv_router_received (handle (), flags_);
    }

    void on_send_ready (zlink_send_ready_handler_fn handler_, void *userdata_ = NULL)
    {
        if (base_socket_t::on_send_ready (handler_, userdata_) != 0)
            throw handler_error_t (handler_result_t::invalid_handle, zlink_errno ());
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
        detail::request_state_t *state = new detail::request_state_t ();
        std::future<std::vector<message_t>> future = state->promise.get_future ();
        std::vector<zlink_msg_t> native;
        if (detail::move_parts_to_native (parts_, native) != 0) {
            delete state;
            throw last_error ();
        }
        const int rc = zlink_router_request (
          handle (), routing_id_native (routing_id_), native.data (),
          native.size (), &detail::request_callback_trampoline, state,
          ZLINK_SEND_FLAGS_NONE,
          static_cast<uint32_t> (
            detail::resolve_timeout (timeout_, _default_request_timeout)
              .count ()));
        if (rc != 0) {
            delete state;
            throw last_error ();
        }
        return async_result_t<std::vector<message_t>> (std::move (future));
    }

    void request (const routing_id_t &routing_id_,
                  message_t &part_,
                  std::function<void(request_result_t, std::vector<message_t>)> callback_,
                  send_flags_t flags_ = send_flags_t::none,
                  std::chrono::milliseconds timeout_ = {})
    {
        std::vector<message_t> parts;
        parts.push_back (std::move (part_));
        request (routing_id_, parts, std::move (callback_), flags_, timeout_);
    }

    void request (const routing_id_t &routing_id_,
                  std::vector<message_t> &parts_,
                  std::function<void(request_result_t, std::vector<message_t>)> callback_,
                  send_flags_t flags_ = send_flags_t::none,
                  std::chrono::milliseconds timeout_ = {})
    {
        detail::request_state_t *state = new detail::request_state_t ();
        state->on_complete = std::move (callback_);
        std::vector<zlink_msg_t> native;
        if (detail::move_parts_to_native (parts_, native) != 0) {
            delete state;
            throw last_error ();
        }
        const int rc = zlink_router_request (
          handle (), routing_id_native (routing_id_), native.data (),
          native.size (), &detail::request_callback_trampoline, state,
          static_cast<zlink_send_flags_t> (flags_),
          static_cast<uint32_t> (
            detail::resolve_timeout (timeout_, _default_request_timeout)
              .count ()));
        if (rc != 0) {
            delete state;
            throw last_error ();
        }
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
        const int rc = zlink_router_reply (
          handle (), routing_id_native (routing_id_), request_seq_,
          native.data (), native.size ());
        if (rc != 0)
            throw last_error ();
    }

    void set_default_request_timeout (std::chrono::milliseconds timeout_)
    {
        _default_request_timeout = timeout_;
    }

    std::chrono::milliseconds get_default_request_timeout () const
    {
        return _default_request_timeout;
    }

    void set_routing_id (const routing_id_t &routing_id_)
    {
        if (base_socket_t::set_routing_id_raw (
              routing_id_.data (), routing_id_.size ())
            != 0)
            throw config_error_t (config_result_t::invalid_argument, zlink_errno ());
    }

    void get_routing_id (routing_id_t &routing_id_) const
    {
        if (base_socket_t::get_routing_id_raw (routing_id_) != 0)
            throw config_error_t (config_result_t::invalid_handle, zlink_errno ());
    }

    void send_to_spot (const routing_id_t &dest_node_rid_,
                       const routing_id_t &dest_spot_rid_,
                       message_t &part_,
                       send_flags_t flags_ = send_flags_t::none)
    {
        std::vector<message_t> parts;
        parts.push_back (std::move (part_));
        send_to_spot (dest_node_rid_, dest_spot_rid_, parts, flags_);
        if (!parts.empty ())
            part_ = std::move (parts.front ());
    }

    void send_to_spot (const routing_id_t &dest_node_rid_,
                       const routing_id_t &dest_spot_rid_,
                       std::vector<message_t> &parts_,
                       send_flags_t flags_ = send_flags_t::none)
    {
        std::vector<zlink_msg_t> native;
        if (detail::move_parts_to_native (parts_, native) != 0)
            throw last_error ();
        const submit_result_t rc = static_cast<submit_result_t> (
          zlink_router_send_spot (
            handle (), routing_id_native (dest_node_rid_),
            routing_id_native (dest_spot_rid_), native.data (), native.size (),
            static_cast<zlink_send_flags_t> (flags_)));
        if (rc != submit_result_t::ok) {
            detail::restore_parts_from_native (parts_, native);
            throw submit_error_t (rc, zlink_errno ());
        }
    }

    async_result_t<std::vector<message_t>>
    request_to_spot (const routing_id_t &dest_node_rid_,
                     const routing_id_t &dest_spot_rid_,
                     message_t message_,
                     std::chrono::milliseconds timeout_ = {})
    {
        detail::request_state_t *state = new detail::request_state_t ();
        std::future<std::vector<message_t>> future = state->promise.get_future ();
        std::vector<message_t> parts;
        parts.push_back (std::move (message_));
        std::vector<zlink_msg_t> native;
        if (detail::move_parts_to_native (parts, native) != 0) {
            delete state;
            throw last_error ();
        }
        const submit_result_t rc = static_cast<submit_result_t> (
          zlink_router_request_spot (
            handle (), routing_id_native (dest_node_rid_),
            routing_id_native (dest_spot_rid_), native.data (), native.size (),
            &detail::request_callback_trampoline, state, ZLINK_SEND_FLAGS_NONE,
            static_cast<uint32_t> (
              detail::resolve_timeout (timeout_, _default_request_timeout)
                .count ())));
        if (rc != submit_result_t::ok) {
            delete state;
            throw submit_error_t (rc, zlink_errno ());
        }
        return async_result_t<std::vector<message_t>> (std::move (future));
    }

    void request_to_spot (
      const routing_id_t &dest_node_rid_,
      const routing_id_t &dest_spot_rid_,
      message_t message_,
      std::function<void(request_result_t, std::vector<message_t>)> callback_,
      send_flags_t flags_ = send_flags_t::none,
      std::chrono::milliseconds timeout_ = {})
    {
        detail::request_state_t *state = new detail::request_state_t ();
        state->on_complete = std::move (callback_);
        std::vector<message_t> parts;
        parts.push_back (std::move (message_));
        std::vector<zlink_msg_t> native;
        if (detail::move_parts_to_native (parts, native) != 0) {
            delete state;
            throw last_error ();
        }
        const submit_result_t rc = static_cast<submit_result_t> (
          zlink_router_request_spot (
            handle (), routing_id_native (dest_node_rid_),
            routing_id_native (dest_spot_rid_), native.data (), native.size (),
            &detail::request_callback_trampoline, state,
            static_cast<zlink_send_flags_t> (flags_),
            static_cast<uint32_t> (
              detail::resolve_timeout (timeout_, _default_request_timeout)
                .count ())));
        if (rc != submit_result_t::ok) {
            delete state;
            throw submit_error_t (rc, zlink_errno ());
        }
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
        const submit_result_t rc = static_cast<submit_result_t> (
          zlink_router_reply_spot (
            handle (), routing_id_native (dest_node_rid_),
            routing_id_native (dest_spot_rid_), request_seq_, native.data (),
            native.size ()));
        if (rc != submit_result_t::ok)
            throw submit_error_t (rc, zlink_errno ());
    }

    template<typename DiscoveryT>
    void attach_discovery (DiscoveryT &discovery_)
    {
        if (base_socket_t::attach_discovery (discovery_) != 0)
            throw config_error_t (config_result_t::invalid_handle, zlink_errno ());
    }

    router_socket_options_t router_options ()
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

    void connect (const std::string &) = delete;

    void send (const routing_id_t &target_rid_, message_t &part_,
               send_flags_t flags_ = send_flags_t::none)
    {
        const submit_result_t rc = static_cast<submit_result_t> (
          base_socket_t::send (target_rid_, part_,
                                flags_));
        if (rc != submit_result_t::ok)
            throw submit_error_t (rc, zlink_errno ());
    }

    void send (const routing_id_t &target_rid_, std::vector<message_t> &parts_,
               send_flags_t flags_ = send_flags_t::none)
    {
        const submit_result_t rc = static_cast<submit_result_t> (
          base_socket_t::send (target_rid_, parts_,
                                flags_));
        if (rc != submit_result_t::ok)
            throw submit_error_t (rc, zlink_errno ());
    }

    received_t recv (recv_flags_t flags_ = recv_flags_t::none)
    {
        received_t received;
        const recv_result_t rc = static_cast<recv_result_t> (
          base_socket_t::receive (received, flags_));
        if (rc != recv_result_t::ok)
            throw recv_error_t (rc, zlink_errno ());
        return received;
    }

    void on_receive (zlink_socket_msg_handler_fn handler_, void *userdata_ = NULL)
    {
        if (base_socket_t::on_receive (handler_, userdata_) != 0)
            throw handler_error_t (handler_result_t::invalid_handle, zlink_errno ());
    }

    void on_send_ready (zlink_send_ready_handler_fn handler_, void *userdata_ = NULL)
    {
        if (base_socket_t::on_send_ready (handler_, userdata_) != 0)
            throw handler_error_t (handler_result_t::invalid_handle, zlink_errno ());
    }

    void set_routing_id (const routing_id_t &routing_id_)
    {
        if (base_socket_t::set_routing_id_raw (
              routing_id_.data (), routing_id_.size ())
            != 0)
            throw config_error_t (config_result_t::invalid_argument, zlink_errno ());
    }

    void get_routing_id (routing_id_t &routing_id_) const
    {
        if (base_socket_t::get_routing_id_raw (routing_id_) != 0)
            throw config_error_t (config_result_t::invalid_handle, zlink_errno ());
    }

    stream_socket_options_t stream_options ()
    {
        return stream_socket_options_t (handle ());
    }

  private:
    using routed_message_socket_t::recv;
    using routed_message_socket_t::send;
    using base_socket_t::connect;
    using base_socket_t::disconnect;
};

class pub_socket_t : public publisher_socket_t
{
  public:
    explicit pub_socket_t (context_t &ctx_)
        : publisher_socket_t (ctx_, socket_type::pub)
    {
    }

    void publish (const std::string &topic_id_, message_t &part_,
                  send_flags_t flags_ = send_flags_t::none)
    {
        const submit_result_t rc = static_cast<submit_result_t> (
          base_socket_t::publish (topic_id_, part_,
                                  flags_));
        if (rc != submit_result_t::ok)
            throw submit_error_t (rc, zlink_errno ());
    }

    void publish (const std::string &topic_id_, std::vector<message_t> &parts_,
                  send_flags_t flags_ = send_flags_t::none)
    {
        const submit_result_t rc = static_cast<submit_result_t> (
          base_socket_t::publish (topic_id_, parts_,
                                  flags_));
        if (rc != submit_result_t::ok)
            throw submit_error_t (rc, zlink_errno ());
    }

    void on_send_ready (zlink_send_ready_handler_fn handler_, void *userdata_ = NULL)
    {
        if (base_socket_t::on_send_ready (handler_, userdata_) != 0)
            throw handler_error_t (handler_result_t::invalid_handle, zlink_errno ());
    }

    template<typename DiscoveryT>
    void attach_discovery (DiscoveryT &discovery_)
    {
        if (base_socket_t::attach_discovery (discovery_) != 0)
            throw config_error_t (config_result_t::invalid_handle, zlink_errno ());
    }

    pub_socket_options_t pub_options ()
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

    void publish (const std::string &topic_id_, message_t &part_,
                  send_flags_t flags_ = send_flags_t::none)
    {
        const submit_result_t rc = static_cast<submit_result_t> (
          base_socket_t::publish (topic_id_, part_,
                                  flags_));
        if (rc != submit_result_t::ok)
            throw submit_error_t (rc, zlink_errno ());
    }

    void publish (const std::string &topic_id_, std::vector<message_t> &parts_,
                  send_flags_t flags_ = send_flags_t::none)
    {
        const submit_result_t rc = static_cast<submit_result_t> (
          base_socket_t::publish (topic_id_, parts_,
                                  flags_));
        if (rc != submit_result_t::ok)
            throw submit_error_t (rc, zlink_errno ());
    }

    void on_send_ready (zlink_send_ready_handler_fn handler_, void *userdata_ = NULL)
    {
        if (base_socket_t::on_send_ready (handler_, userdata_) != 0)
            throw handler_error_t (handler_result_t::invalid_handle, zlink_errno ());
    }

    subscription_event_t receive_subscription_event (
      recv_flags_t flags_ = recv_flags_t::none)
    {
        subscription_event_t event;
        const recv_result_t rc = static_cast<recv_result_t> (
          base_socket_t::subscription_event (event, flags_));
        if (rc != recv_result_t::ok)
            throw recv_error_t (rc, zlink_errno ());
        return event;
    }

    pub_socket_options_t pub_options ()
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
            throw config_error_t (config_result_t::invalid_argument, zlink_errno ());
    }

    void unset_subscription (const std::string &filter_)
    {
        if (base_socket_t::unset_subscription (filter_) != 0)
            throw config_error_t (config_result_t::invalid_argument, zlink_errno ());
    }

    void subscription_at (size_t index_, std::string &filter_out_,
                          bool *is_pattern_out_ = NULL)
    {
        if (base_socket_t::subscription_at (index_, filter_out_, is_pattern_out_) != 0)
            throw config_error_t (config_result_t::invalid_argument, zlink_errno ());
    }

    topic_message_t subscribe (recv_flags_t flags_ = recv_flags_t::none)
    {
        topic_message_t message;
        const recv_result_t rc = static_cast<recv_result_t> (
          base_socket_t::subscribe (message, flags_));
        if (rc != recv_result_t::ok)
            throw recv_error_t (rc, zlink_errno ());
        return message;
    }

    template<typename DiscoveryT>
    void attach_discovery (DiscoveryT &discovery_)
    {
        if (base_socket_t::attach_discovery (discovery_) != 0)
            throw config_error_t (config_result_t::invalid_handle, zlink_errno ());
    }

    sub_socket_options_t sub_options ()
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
            throw config_error_t (config_result_t::invalid_argument, zlink_errno ());
    }

    void unset_subscription (const std::string &filter_)
    {
        if (base_socket_t::unset_subscription (filter_) != 0)
            throw config_error_t (config_result_t::invalid_argument, zlink_errno ());
    }

    void subscription_at (size_t index_, std::string &filter_out_,
                          bool *is_pattern_out_ = NULL)
    {
        if (base_socket_t::subscription_at (index_, filter_out_, is_pattern_out_) != 0)
            throw config_error_t (config_result_t::invalid_argument, zlink_errno ());
    }

    topic_message_t subscribe (recv_flags_t flags_ = recv_flags_t::none)
    {
        topic_message_t message;
        const recv_result_t rc = static_cast<recv_result_t> (
          base_socket_t::subscribe (message, flags_));
        if (rc != recv_result_t::ok)
            throw recv_error_t (rc, zlink_errno ());
        return message;
    }

    sub_socket_options_t sub_options ()
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
