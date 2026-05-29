/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_RUNTIME_NATIVE_SEND_HPP_INCLUDED
#define ZLINK_CPP_RUNTIME_NATIVE_SEND_HPP_INCLUDED

#include "native_message_parts.hpp"
#include "../Core/routing_id_access.hpp"

#include <zlink/Contracts/Errors/errors.hpp>
#include <zlink/Contracts/Sockets/results.hpp>

#include <functional>
#include <optional>
#include <utility>
#include <vector>

namespace zlink
{
namespace detail
{

template <typename NativeSend>
inline int send_single_no_wait_result (send_result_t &result_,
                                       message_t &part_,
                                       NativeSend send_)
{
    if (!part_.valid ()) {
        errno = EINVAL;
        return -1;
    }

    const int rc = send_ (detail::native_handle (part_), ZLINK_PART_FINAL);
    if (rc == 0) {
        detail::mark_sent (part_);
        result_ = send_result_t::sent;
        return 0;
    }

    const int err = errno;
    if (detail::classify_nonblocking_send_errno (err, result_))
        return 0;
    errno = err;
    return -1;
}

template <typename NativeSend>
inline int send_parts_no_wait_result (send_result_t &result_,
                                      std::vector<message_t> &parts_,
                                      NativeSend send_)
{
    std::vector<zlink_msg_t> native_parts;
    if (detail::move_parts_to_native (parts_, native_parts) != 0)
        return -1;

    size_t failed_index = 0;
    const int rc = detail::submit_native_parts (
      native_parts, failed_index,
      [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_, bool) {
          return send_ (part_out_, part_flag_);
      });
    if (rc == 0) {
        result_ = send_result_t::sent;
        return 0;
    }

    const int err = errno;
    if (detail::classify_nonblocking_send_errno (err, result_)) {
        if (result_ != send_result_t::sent)
            detail::restore_parts_from_native (parts_, native_parts,
                                               failed_index);
        return 0;
    }

    detail::restore_parts_from_native (parts_, native_parts, failed_index);
    errno = err;
    return -1;
}

inline std::function<bool (std::vector<message_t> &, send_flags_t)>
make_routed_send_fn (void *socket_, const routing_id_t &target_rid_)
{
    return [socket_, target_rid_] (std::vector<message_t> &send_parts_,
                                   send_flags_t flags_) {
        std::vector<zlink_msg_t> native;
        if (detail::move_parts_to_native (send_parts_, native) != 0)
            throw last_error ();

        size_t failed_index = 0;
        const submit_result_t result =
          static_cast<submit_result_t> (detail::submit_native_parts (
            native, failed_index,
            [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_, bool) {
                return zlink_send_part_rid (
                  socket_, zlink::detail::routing_id_native (target_rid_),
                  part_out_,
                  static_cast<zlink_send_flags_t> (static_cast<int> (flags_)),
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

template <typename NativeSubmit>
inline bool submit_received_send_parts (std::vector<message_t> &send_parts_,
                                        send_flags_t flags_,
                                        NativeSubmit submit_)
{
    std::vector<zlink_msg_t> native;
    if (detail::move_parts_to_native (send_parts_, native) != 0)
        throw last_error ();

    size_t failed_index = 0;
    const submit_result_t result =
      static_cast<submit_result_t> (detail::submit_native_parts (
        native, failed_index,
        [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_, bool) {
            return submit_ (part_out_, part_flag_);
        }));
    if (result == submit_result_t::ok)
        return true;

    detail::restore_parts_from_native (send_parts_, native, failed_index);
    if (flags_ == send_flags_t::dontwait
        && result == submit_result_t::backpressured)
        return false;
    throw submit_error_t (result, zlink_errno ());
}

template <typename NativeSubmit>
inline void submit_received_reply_parts (std::vector<message_t> &reply_parts_,
                                         send_flags_t flags_,
                                         NativeSubmit submit_)
{
    detail::throw_if_reply_flags_unsupported (flags_);
    std::vector<zlink_msg_t> native;
    if (detail::move_parts_to_native (reply_parts_, native) != 0)
        throw last_error ();

    size_t failed_index = 0;
    const submit_result_t result =
      static_cast<submit_result_t> (detail::submit_native_parts (
        native, failed_index,
        [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_, bool) {
            return submit_ (part_out_, part_flag_);
        }));
    if (result != submit_result_t::ok) {
        detail::restore_parts_from_native (reply_parts_, native, failed_index);
        throw submit_error_t (result, zlink_errno ());
    }
}

inline std::function<bool (std::vector<message_t> &, send_flags_t)>
make_router_send_fn (void *router_handle_,
                     const routing_id_t &node_rid_,
                     const std::optional<routing_id_t> &spot_rid_)
{
    if (spot_rid_) {
        const routing_id_t spot_rid = *spot_rid_;
        return [router_handle_, node_rid_, spot_rid] (
                 std::vector<message_t> &send_parts_, send_flags_t flags_) {
            return detail::submit_received_send_parts (
              send_parts_, flags_,
              [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_) {
                  return zlink_router_send_spot_part (
                    router_handle_,
                    zlink::detail::routing_id_native (node_rid_),
                    zlink::detail::routing_id_native (spot_rid), part_out_,
                    static_cast<zlink_send_flags_t> (static_cast<int> (flags_)),
                    part_flag_);
              });
        };
    }

    return [router_handle_, node_rid_] (std::vector<message_t> &send_parts_,
                                        send_flags_t flags_) {
        return detail::submit_received_send_parts (
          send_parts_, flags_,
          [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_) {
              return zlink_send_part_rid (
                router_handle_, zlink::detail::routing_id_native (node_rid_),
                part_out_,
                static_cast<zlink_send_flags_t> (static_cast<int> (flags_)),
                part_flag_);
          });
    };
}

inline std::function<void (std::vector<message_t> &, send_flags_t)>
make_router_reply_fn (void *router_handle_,
                      const routing_id_t &node_rid_,
                      const std::optional<routing_id_t> &spot_rid_,
                      uint64_t request_seq_)
{
    if (spot_rid_) {
        const routing_id_t spot_rid = *spot_rid_;
        return [router_handle_, node_rid_, spot_rid, request_seq_] (
                 std::vector<message_t> &reply_parts_, send_flags_t flags_) {
            detail::submit_received_reply_parts (
              reply_parts_, flags_,
              [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_) {
                  return zlink_router_reply_spot_part (
                    router_handle_,
                    zlink::detail::routing_id_native (node_rid_),
                    zlink::detail::routing_id_native (spot_rid), request_seq_,
                    part_out_, part_flag_);
              });
        };
    }

    return [router_handle_, node_rid_, request_seq_] (
             std::vector<message_t> &reply_parts_, send_flags_t flags_) {
        detail::submit_received_reply_parts (
          reply_parts_, flags_,
          [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_) {
              return zlink_router_reply_part (
                router_handle_, zlink::detail::routing_id_native (node_rid_),
                request_seq_, part_out_, part_flag_);
          });
    };
}

} // namespace detail
} // namespace zlink

#endif
