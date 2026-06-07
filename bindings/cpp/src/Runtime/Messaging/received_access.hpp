/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_RUNTIME_MESSAGING_RECEIVED_ACCESS_HPP_INCLUDED
#define ZLINK_CPP_RUNTIME_MESSAGING_RECEIVED_ACCESS_HPP_INCLUDED

#include <Runtime/Core/routing_id_access.hpp>
#include <Runtime/Native/message_access.hpp>
#include <Runtime/Native/native_send.hpp>

#include <zlink/Contracts/Messaging/received.hpp>

namespace zlink
{
namespace detail
{

struct received_access_t
{
    static received_t make (std::optional<routing_id_t> routing_id_,
                            std::optional<routing_id_t> spot_rid_,
                            std::optional<uint64_t> request_seq_,
                            std::vector<message_t> parts_)
    {
        return received_t (std::move (routing_id_), std::move (spot_rid_), std::move (request_seq_),
                           std::move (parts_));
    }

    static received_t make (std::optional<routing_id_t> routing_id_,
                            std::optional<routing_id_t> spot_rid_,
                            std::optional<uint64_t> request_seq_,
                            message_t part_)
    {
        return received_t (std::move (routing_id_), std::move (spot_rid_), std::move (request_seq_),
                           std::move (part_));
    }

    static void set_socket_rid_send_context (received_t &received_, void *handle_)
    {
        set_send_context (received_, handle_, received_t::send_context_kind_t::socket_rid);
    }

    static void set_router_spot_send_context (received_t &received_, void *handle_)
    {
        set_send_context (received_, handle_, received_t::send_context_kind_t::router_spot);
    }

    static void set_spot_spot_send_context (received_t &received_, void *handle_)
    {
        set_send_context (received_, handle_, received_t::send_context_kind_t::spot_spot);
    }

    static bool has_send_context (const received_t &received_) noexcept
    {
        return received_._send_context_handle != 0
               && received_._send_context_kind != received_t::send_context_kind_t::none
               && received_._routing_id.has_value ();
    }

    static bool has_reply_context (const received_t &received_) noexcept
    {
        return has_send_context (received_) && received_._request_seq.has_value ();
    }

    // Single-part send fast path: one native call, no intermediate vector.
    static bool submit_direct_send (received_t &received_,
                                    message_t &part_,
                                    send_flags_t flags_,
                                    submit_result_t &result_out_,
                                    int &errno_out_)
    {
        if (!has_send_context (received_))
            return false;

        void *handle = reinterpret_cast<void *> (received_._send_context_handle);
        const zlink_send_flags_t native_flags =
          static_cast<zlink_send_flags_t> (static_cast<int> (flags_));
        zlink_submit_result_t rc = ZLINK_SUBMIT_INVALID_ARGUMENT;
        switch (received_._send_context_kind) {
            case received_t::send_context_kind_t::socket_rid:
                rc = zlink_send_part_rid (
                  handle, zlink::detail::routing_id_native (*received_._routing_id),
                  zlink::detail::native_handle (part_), native_flags, ZLINK_PART_FINAL);
                break;
            case received_t::send_context_kind_t::router_spot:
                if (received_._spot_rid.has_value ())
                    rc = zlink_router_send_spot_part (
                      handle, zlink::detail::routing_id_native (*received_._routing_id),
                      zlink::detail::routing_id_native (*received_._spot_rid),
                      zlink::detail::native_handle (part_), native_flags, ZLINK_PART_FINAL);
                break;
            case received_t::send_context_kind_t::spot_spot:
                if (received_._spot_rid.has_value ())
                    rc = zlink_spot_send_spot_part (
                      handle, zlink::detail::routing_id_native (*received_._routing_id),
                      zlink::detail::routing_id_native (*received_._spot_rid),
                      zlink::detail::native_handle (part_), native_flags, ZLINK_PART_FINAL);
                break;
            default:
                break;
        }

        result_out_ = static_cast<submit_result_t> (rc);
        errno_out_ = zlink_errno ();
        if (result_out_ == submit_result_t::ok)
            zlink::detail::mark_sent (part_);
        return true;
    }

    // Multipart send: reconstructs the native submit from the stored context.
    static bool
    submit_send (received_t &received_, std::vector<message_t> &parts_, send_flags_t flags_)
    {
        void *handle = reinterpret_cast<void *> (received_._send_context_handle);
        const zlink_send_flags_t native_flags =
          static_cast<zlink_send_flags_t> (static_cast<int> (flags_));
        if (received_._send_context_kind == received_t::send_context_kind_t::router_spot
            && received_._spot_rid.has_value ()) {
            return zlink::detail::submit_received_send_parts (
              parts_, flags_, [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_) {
                  return zlink_router_send_spot_part (
                    handle, zlink::detail::routing_id_native (*received_._routing_id),
                    zlink::detail::routing_id_native (*received_._spot_rid), part_out_,
                    native_flags, part_flag_);
              });
        }
        if (received_._send_context_kind == received_t::send_context_kind_t::spot_spot
            && received_._spot_rid.has_value ()) {
            return zlink::detail::submit_received_send_parts (
              parts_, flags_, [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_) {
                  return zlink_spot_send_spot_part (
                    handle, zlink::detail::routing_id_native (*received_._routing_id),
                    zlink::detail::routing_id_native (*received_._spot_rid), part_out_,
                    native_flags, part_flag_);
              });
        }
        return zlink::detail::submit_received_send_parts (
          parts_, flags_, [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_) {
              return zlink_send_part_rid (handle,
                                          zlink::detail::routing_id_native (*received_._routing_id),
                                          part_out_, native_flags, part_flag_);
          });
    }

    // Reply: reconstructs the native reply submit from the stored context and
    // request sequence.
    static void
    submit_reply (received_t &received_, std::vector<message_t> &parts_, send_flags_t flags_)
    {
        void *handle = reinterpret_cast<void *> (received_._send_context_handle);
        const uint64_t request_seq = *received_._request_seq;
        if (received_._send_context_kind == received_t::send_context_kind_t::router_spot
            && received_._spot_rid.has_value ()) {
            zlink::detail::submit_received_reply_parts (
              parts_, flags_, [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_) {
                  return zlink_router_reply_spot_part (
                    handle, zlink::detail::routing_id_native (*received_._routing_id),
                    zlink::detail::routing_id_native (*received_._spot_rid), request_seq, part_out_,
                    part_flag_);
              });
            return;
        }
        if (received_._send_context_kind == received_t::send_context_kind_t::spot_spot
            && received_._spot_rid.has_value ()) {
            zlink::detail::submit_received_reply_parts (
              parts_, flags_, [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_) {
                  return zlink_spot_reply_spot_part (
                    handle, zlink::detail::routing_id_native (*received_._routing_id),
                    zlink::detail::routing_id_native (*received_._spot_rid), request_seq, part_out_,
                    part_flag_);
              });
            return;
        }
        zlink::detail::submit_received_reply_parts (
          parts_, flags_, [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_) {
              return zlink_router_reply_part (
                handle, zlink::detail::routing_id_native (*received_._routing_id), request_seq,
                part_out_, part_flag_);
          });
    }

  private:
    static void
    set_send_context (received_t &received_, void *handle_, received_t::send_context_kind_t kind_)
    {
        received_._send_context_handle = reinterpret_cast<std::uintptr_t> (handle_);
        received_._send_context_kind = kind_;
    }
};

} // namespace detail
} // namespace zlink

#endif
