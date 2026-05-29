/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_SOCKETS_DETAIL_HPP_INCLUDED
#define ZLINK_CPP_SOCKETS_DETAIL_HPP_INCLUDED

#include <zlink/Contracts/Sockets/message_socket_contracts.hpp>
#include <zlink/Contracts/Sockets/pubsub_socket_contracts.hpp>
#include <zlink/Contracts/Errors/errors.hpp>
#include "../Core/operation_detail.hpp"
#include "../Service/spot_access.hpp"

#include <memory>
#include <string>
#include <type_traits>
#include <utility>

namespace zlink
{

// Shared implementation helpers for concrete socket entrypoint headers.

namespace detail
{

inline received_t recv_router_received (void *router_handle_,
                                        recv_flags_t flags_)
{
    const zlink_routing_id_t *source_node_rid = nullptr;
    const zlink_routing_id_t *source_spot_rid = nullptr;
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
                          part_out_, static_cast<zlink_send_flags_t> (static_cast<int> (flags_)),
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
                          part_out_, static_cast<zlink_send_flags_t> (static_cast<int> (flags_)),
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

class recv_part_out_guard_t
{
  public:
    explicit recv_part_out_guard_t (message_t &part_) noexcept
        : _part (part_), _has_saved (false), _committed (false)
    {
        if (_part.valid ()) {
            move_to_native (_part, &_saved);
            _has_saved = true;
        }
    }

    ~recv_part_out_guard_t ()
    {
        if (_committed)
            return;
        _part.close ();
        if (_has_saved)
            adopt_native_message (_part, &_saved);
    }

    bool prepare ()
    {
        _part.init ();
        return _part.valid ();
    }

    void commit () noexcept
    {
        if (_has_saved)
            (void) zlink_msg_close (&_saved);
        _has_saved = false;
        _committed = true;
    }

  private:
    message_t &_part;
    zlink_msg_t _saved;
    bool _has_saved;
    bool _committed;
};

inline int recv_single_part_message (void *handle_,
                                     routing_id_t *source_rid_out_,
                                     message_t &part_out_,
                                     recv_flags_t flags_)
{
    recv_part_out_guard_t part_guard (part_out_);
    if (!part_guard.prepare ())
        return -1;

    const zlink_routing_id_t *source_rid = nullptr;
    zlink_part_flag_t has_more = ZLINK_PART_FINAL;
    const int rc = zlink_recv_part (
      handle_, &source_rid, detail::native_handle (part_out_), &has_more,
      static_cast<zlink_recv_flags_t> (static_cast<int> (flags_)));
    if (rc != 0)
        return rc;
    if (has_more != ZLINK_PART_FINAL) {
        errno = EMSGSIZE;
        return -1;
    }

    if (source_rid_out_) {
        if (source_rid && source_rid->size > 0)
            assign_routing_id_native (*source_rid_out_, *source_rid);
        else
            *source_rid_out_ = unchecked_empty_routing_id ();
    }
    part_guard.commit ();
    return 0;
}

inline int recv_single_part_routed_message (void *handle_,
                                            routing_id_t &source_rid_out_,
                                            message_t &part_out_,
                                            recv_flags_t flags_)
{
    recv_part_out_guard_t part_guard (part_out_);
    if (!part_guard.prepare ())
        return -1;

    const zlink_routing_id_t *source_node_rid = nullptr;
    const zlink_routing_id_t *source_spot_rid = nullptr;
    uint64_t request_seq = 0;
    zlink_part_flag_t has_more = ZLINK_PART_FINAL;
    const int rc = zlink_router_recv_part (
      handle_, &source_node_rid, &source_spot_rid, &request_seq,
      detail::native_handle (part_out_), &has_more,
      static_cast<zlink_recv_flags_t> (static_cast<int> (flags_)));
    if (rc != 0)
        return rc;
    if (has_more != ZLINK_PART_FINAL || request_seq != 0
        || (source_spot_rid && source_spot_rid->size > 0)
        || !source_node_rid || source_node_rid->size == 0) {
        errno = has_more != ZLINK_PART_FINAL ? EMSGSIZE : EPROTO;
        return -1;
    }

    assign_routing_id_native (source_rid_out_, *source_node_rid);
    part_guard.commit ();
    return 0;
}

} // namespace detail

} // namespace zlink

#endif
