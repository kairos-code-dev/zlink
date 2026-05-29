/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_RUNTIME_NATIVE_SEND_HPP_INCLUDED
#define ZLINK_CPP_RUNTIME_NATIVE_SEND_HPP_INCLUDED

#include "native_message_parts.hpp"
#include "../Core/routing_id_access.hpp"

#include <zlink/Contracts/Errors/errors.hpp>
#include <zlink/Contracts/Sockets/results.hpp>

#include <functional>
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

} // namespace detail
} // namespace zlink

#endif
