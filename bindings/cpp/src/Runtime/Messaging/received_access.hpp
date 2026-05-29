/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_RUNTIME_MESSAGING_RECEIVED_ACCESS_HPP_INCLUDED
#define ZLINK_CPP_RUNTIME_MESSAGING_RECEIVED_ACCESS_HPP_INCLUDED

#include <Runtime/Core/routing_id_access.hpp>
#include <Runtime/Native/message_access.hpp>

#include <zlink/Contracts/Messaging/received.hpp>

namespace zlink
{
namespace detail
{

struct received_access_t
{
    using reply_fn_t =
      std::function<void (std::vector<message_t> &, send_flags_t)>;
    using send_fn_t =
      std::function<bool (std::vector<message_t> &, send_flags_t)>;

    static received_t make (std::optional<routing_id_t> routing_id_,
                            std::optional<routing_id_t> spot_rid_,
                            std::optional<uint64_t> request_seq_,
                            std::vector<message_t> parts_,
                            reply_fn_t reply_fn_ = reply_fn_t (),
                            send_fn_t send_fn_ = send_fn_t ())
    {
        received_t received (std::move (routing_id_), std::move (spot_rid_),
                             std::move (request_seq_), std::move (parts_));
        set_reply_fn (received, std::move (reply_fn_));
        set_send_fn (received, std::move (send_fn_));
        return received;
    }

    static received_t make (std::optional<routing_id_t> routing_id_,
                            std::optional<routing_id_t> spot_rid_,
                            std::optional<uint64_t> request_seq_,
                            message_t part_,
                            reply_fn_t reply_fn_ = reply_fn_t (),
                            send_fn_t send_fn_ = send_fn_t ())
    {
        received_t received (std::move (routing_id_), std::move (spot_rid_),
                             std::move (request_seq_), std::move (part_));
        set_reply_fn (received, std::move (reply_fn_));
        set_send_fn (received, std::move (send_fn_));
        return received;
    }

    static void set_reply_fn (received_t &received_, reply_fn_t reply_fn_)
    {
        if (!reply_fn_)
            return;
        received_.ensure_runtime_state ();
        received_._runtime->_reply_fn = std::move (reply_fn_);
    }

    static void set_send_fn (received_t &received_, send_fn_t send_fn_)
    {
        if (!send_fn_)
            return;
        received_.ensure_runtime_state ();
        received_._runtime->_send_fn = std::move (send_fn_);
    }

    static void set_socket_rid_send_context (received_t &received_,
                                             void *handle_)
    {
        set_send_context (received_, handle_,
                          received_t::send_context_kind_t::socket_rid);
    }

    static void set_router_spot_send_context (received_t &received_,
                                              void *handle_)
    {
        set_send_context (received_, handle_,
                          received_t::send_context_kind_t::router_spot);
    }

    static bool has_reply_fn (const received_t &received_) noexcept
    {
        return received_._runtime
               && static_cast<bool> (received_._runtime->_reply_fn);
    }

    static bool has_send_fn (const received_t &received_) noexcept
    {
        return received_._runtime
               && static_cast<bool> (received_._runtime->_send_fn);
    }

    static void invoke_reply_fn (received_t &received_,
                                 std::vector<message_t> &parts_,
                                 send_flags_t flags_)
    {
        received_._runtime->_reply_fn (parts_, flags_);
    }

    static bool invoke_send_fn (received_t &received_,
                                std::vector<message_t> &parts_,
                                send_flags_t flags_)
    {
        return received_._runtime->_send_fn (parts_, flags_);
    }

    static bool submit_direct_send (received_t &received_,
                                    message_t &part_,
                                    send_flags_t flags_,
                                    submit_result_t &result_out_,
                                    int &errno_out_)
    {
        if (!received_._runtime || !received_._runtime->_send_context_handle
            || received_._runtime->_send_context_kind
                 == received_t::send_context_kind_t::none
            || !received_._routing_id.has_value ()) {
            return false;
        }

        void *handle =
          reinterpret_cast<void *> (received_._runtime->_send_context_handle);
        zlink_submit_result_t rc = ZLINK_SUBMIT_INVALID_ARGUMENT;
        if (received_._runtime->_send_context_kind
            == received_t::send_context_kind_t::socket_rid) {
            rc = zlink_send_part_rid (
              handle, zlink::detail::routing_id_native (*received_._routing_id),
              zlink::detail::native_handle (part_),
              static_cast<zlink_send_flags_t> (static_cast<int> (flags_)),
              ZLINK_PART_FINAL);
        } else if (received_._spot_rid.has_value ()) {
            rc = zlink_router_send_spot_part (
              handle, zlink::detail::routing_id_native (*received_._routing_id),
              zlink::detail::routing_id_native (*received_._spot_rid),
              zlink::detail::native_handle (part_),
              static_cast<zlink_send_flags_t> (static_cast<int> (flags_)),
              ZLINK_PART_FINAL);
        }

        result_out_ = static_cast<submit_result_t> (rc);
        errno_out_ = zlink_errno ();
        if (result_out_ == submit_result_t::ok)
            zlink::detail::mark_sent (part_);
        return true;
    }

  private:
    static void set_send_context (received_t &received_,
                                  void *handle_,
                                  received_t::send_context_kind_t kind_)
    {
        received_.ensure_runtime_state ();
        received_._runtime->_send_context_handle =
          reinterpret_cast<std::uintptr_t> (handle_);
        received_._runtime->_send_context_kind = kind_;
    }
};

} // namespace detail
} // namespace zlink

#endif
