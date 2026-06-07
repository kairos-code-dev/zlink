/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_RUNTIME_NATIVE_SEND_HPP_INCLUDED
#define ZLINK_CPP_RUNTIME_NATIVE_SEND_HPP_INCLUDED

#include "native_message_parts.hpp"

#include <zlink/Contracts/Errors/errors.hpp>
#include <zlink/Contracts/Sockets/results.hpp>

#include <utility>
#include <vector>

namespace zlink
{
namespace detail
{

template <typename NativeSend>
inline int send_single_no_wait_result (send_result_t &result_, message_t &part_, NativeSend send_)
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
inline int submit_single_message_part_restore (message_t &part_, NativeSend send_)
{
    if (!part_.valid ()) {
        errno = EINVAL;
        return -1;
    }

    zlink_msg_t native;
    detail::move_to_native (part_, &native);
    if (part_.valid ())
        return -1;

    const int rc = send_ (&native);
    if (rc != 0) {
        const int err = errno;
        detail::restore_part_from_native (part_, native);
        errno = err;
    }
    return rc;
}

template <typename NativeSend>
inline int submit_single_message_part_no_wait_result (send_result_t &result_,
                                                      message_t &part_,
                                                      NativeSend send_)
{
    if (!part_.valid ()) {
        errno = EINVAL;
        return -1;
    }

    zlink_msg_t native;
    detail::move_to_native (part_, &native);
    if (part_.valid ())
        return -1;

    const int rc = send_ (&native);
    if (rc == 0) {
        result_ = send_result_t::sent;
        return 0;
    }

    if (rc == ZLINK_SUBMIT_BACKPRESSURED || rc == ZLINK_SUBMIT_NOT_CONNECTED) {
        result_ = zlink::detail::to_send_result (rc);
        detail::restore_part_from_native (part_, native);
        return 0;
    }

    const int err = errno;
    if (detail::classify_nonblocking_send_errno (err, result_)) {
        if (result_ != send_result_t::sent)
            detail::restore_part_from_native (part_, native);
        errno = err;
        return 0;
    }

    detail::restore_part_from_native (part_, native);
    errno = err;
    return -1;
}

template <typename NativeSend>
inline int
send_parts_no_wait_result (send_result_t &result_, std::vector<message_t> &parts_, NativeSend send_)
{
    return detail::submit_message_parts_no_wait (
      result_, parts_, [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_, bool) {
          return send_ (part_out_, part_flag_);
      });
}

template <typename NativeSubmit>
inline submit_result_t submit_received_parts_restore (std::vector<message_t> &parts_,
                                                      NativeSubmit submit_)
{
    if (parts_.size () <= native_part_stack_capacity) {
        std::array<zlink_msg_t, native_part_stack_capacity> native_parts;
        if (detail::move_parts_to_native (parts_, native_parts.data (), parts_.size ()) != 0)
            throw last_error ();

        size_t failed_index = 0;
        const submit_result_t result = static_cast<submit_result_t> (detail::submit_native_parts (
          native_parts.data (), parts_.size (), failed_index,
          [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_, bool) {
              return submit_ (part_out_, part_flag_, part_flag_ == ZLINK_PART_FINAL);
          }));
        if (result != submit_result_t::ok)
            detail::restore_parts_from_native (parts_, native_parts.data (), parts_.size (),
                                               failed_index);
        return result;
    }

    std::vector<zlink_msg_t> native;
    if (detail::move_parts_to_native (parts_, native) != 0)
        throw last_error ();

    size_t failed_index = 0;
    const submit_result_t result = static_cast<submit_result_t> (detail::submit_native_parts (
      native, failed_index, [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_, bool) {
          return submit_ (part_out_, part_flag_, part_flag_ == ZLINK_PART_FINAL);
      }));
    if (result != submit_result_t::ok)
        detail::restore_parts_from_native (parts_, native, failed_index);
    return result;
}

template <typename NativeSubmit>
inline bool submit_received_send_parts (std::vector<message_t> &send_parts_,
                                        send_flags_t flags_,
                                        NativeSubmit submit_)
{
    const submit_result_t result = detail::submit_received_parts_restore (
      send_parts_, [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_, bool) {
          return submit_ (part_out_, part_flag_);
      });
    if (result == submit_result_t::ok)
        return true;

    if (flags_ == send_flags_t::dontwait && result == submit_result_t::backpressured)
        return false;
    throw submit_error_t (result, zlink_errno ());
}

template <typename NativeSubmit>
inline void submit_received_reply_parts (std::vector<message_t> &reply_parts_,
                                         send_flags_t flags_,
                                         NativeSubmit submit_)
{
    detail::throw_if_reply_flags_unsupported (flags_);
    const submit_result_t result = detail::submit_received_parts_restore (
      reply_parts_, [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_, bool) {
          return submit_ (part_out_, part_flag_);
      });
    if (result != submit_result_t::ok)
        throw submit_error_t (result, zlink_errno ());
}

} // namespace detail
} // namespace zlink

#endif
