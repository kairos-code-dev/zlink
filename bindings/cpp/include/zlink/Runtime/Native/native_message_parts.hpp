/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_RUNTIME_NATIVE_MESSAGE_PARTS_HPP_INCLUDED
#define ZLINK_CPP_RUNTIME_NATIVE_MESSAGE_PARTS_HPP_INCLUDED

#include "../../Contracts/Core/types.hpp"
#include "../../Contracts/Messaging/message.hpp"
#include "native_send_result.hpp"

#include <cerrno>
#include <utility>
#include <vector>

namespace zlink
{
namespace detail
{

inline void close_message_array (zlink_msg_t *parts_, size_t part_count_) noexcept
{
    if (!parts_)
        return;
    zlink_multipart_close (parts_, part_count_);
}

inline void close_native_parts (std::vector<zlink_msg_t> &parts_,
                                size_t start_index_ = 0) noexcept
{
    if (start_index_ >= parts_.size ())
        return;

    for (size_t i = start_index_; i < parts_.size (); ++i)
        (void) zlink_msg_close (&parts_[i]);
}

inline int move_parts_to_native (std::vector<message_t> &parts_,
                                 std::vector<zlink_msg_t> &native_)
{
    native_.clear ();
    native_.resize (parts_.size ());

    size_t moved = 0;
    for (; moved < parts_.size (); ++moved) {
        if (!parts_[moved].valid ()) {
            errno = EINVAL;
            break;
        }
        detail::move_to_native (parts_[moved], &native_[moved]);
        if (parts_[moved].valid ())
            break;
    }

    if (moved == parts_.size ())
        return 0;

    for (size_t i = 0; i < moved; ++i) {
        parts_[i].init ();
        if (parts_[i].valid ())
            (void) zlink_msg_move (detail::native_handle (parts_[i]), &native_[i]);
        (void) zlink_msg_close (&native_[i]);
    }

    native_.clear ();
    return -1;
}

inline void restore_part_from_native (message_t &part_,
                                      zlink_msg_t &native_) noexcept
{
    part_.init ();
    if (part_.valid ())
        (void) zlink_msg_move (detail::native_handle (part_), &native_);
    (void) zlink_msg_close (&native_);
}

inline void restore_parts_from_native (std::vector<message_t> &parts_,
                                       std::vector<zlink_msg_t> &native_,
                                       size_t start_index_ = 0) noexcept
{
    const size_t count =
      native_.size () < parts_.size () ? native_.size () : parts_.size ();
    for (size_t i = start_index_; i < count; ++i) {
        parts_[i].init ();
        if (parts_[i].valid ())
            (void) zlink_msg_move (detail::native_handle (parts_[i]), &native_[i]);
        (void) zlink_msg_close (&native_[i]);
    }
    native_.clear ();
}

inline int assign_parts_from_native (zlink_msg_t *parts_native_,
                                     size_t part_count_,
                                     std::vector<message_t> &parts_)
{
    parts_.clear ();
    parts_.resize (part_count_);
    for (size_t i = 0; i < part_count_; ++i) {
        if (zlink_msg_move (detail::native_handle (parts_[i]), &parts_native_[i]) != 0) {
            parts_.clear ();
            close_message_array (parts_native_, part_count_);
            return -1;
        }
    }
    close_message_array (parts_native_, part_count_);
    return 0;
}

inline int assign_parts_from_native (std::vector<zlink_msg_t> &parts_native_,
                                     std::vector<message_t> &parts_)
{
    parts_.clear ();
    parts_.resize (parts_native_.size ());
    for (size_t i = 0; i < parts_native_.size (); ++i) {
        if (zlink_msg_move (detail::native_handle (parts_[i]), &parts_native_[i]) != 0) {
            parts_.clear ();
            close_native_parts (parts_native_, i);
            parts_native_.clear ();
            return -1;
        }
    }
    parts_native_.clear ();
    return 0;
}

inline std::vector<message_t>
take_parts_from_native (zlink_msg_t *parts_, size_t part_count_)
{
    std::vector<message_t> parts;
    parts.resize (part_count_);
    for (size_t i = 0; i < part_count_; ++i)
        (void) zlink_msg_move (detail::native_handle (parts[i]), &parts_[i]);
    close_message_array (parts_, part_count_);
    return parts;
}

template<typename SubmitFn>
inline int submit_native_parts (std::vector<zlink_msg_t> &parts_native_,
                                size_t &failed_index_out_,
                                SubmitFn submit_)
{
    failed_index_out_ = 0;
    if (parts_native_.empty ()) {
        errno = EFAULT;
        return ZLINK_SUBMIT_INVALID_HANDLE;
    }

    for (size_t i = 0; i < parts_native_.size (); ++i) {
        const zlink_part_flag_t part_flag =
          i + 1 < parts_native_.size () ? ZLINK_PART_MORE : ZLINK_PART_FINAL;
        const int rc = submit_ (&parts_native_[i], part_flag, i + 1 == parts_native_.size ());
        if (rc != ZLINK_SUBMIT_OK) {
            failed_index_out_ = i;
            return rc;
        }
    }

    return ZLINK_SUBMIT_OK;
}

template<typename SubmitFn>
inline int submit_one_message_part (message_t &part_, SubmitFn submit_)
{
    if (!part_.valid ()) {
        errno = EINVAL;
        return -1;
    }

    zlink_msg_t native_part;
    detail::move_to_native (part_, &native_part);
    if (part_.valid ())
        return -1;

    const int rc = submit_ (&native_part, ZLINK_PART_FINAL);
    if (rc != 0)
        restore_part_from_native (part_, native_part);
    return rc;
}

template<typename SubmitFn>
inline int submit_message_parts (std::vector<message_t> &parts_,
                                 SubmitFn submit_)
{
    std::vector<zlink_msg_t> native_parts;
    if (detail::move_parts_to_native (parts_, native_parts) != 0)
        return -1;

    size_t failed_index = 0;
    const int rc = detail::submit_native_parts (
      native_parts, failed_index, std::move (submit_));
    if (rc != 0)
        detail::restore_parts_from_native (parts_, native_parts, failed_index);
    return rc;
}

template<typename SubmitFn>
inline int submit_message_parts_no_wait (send_result_t &result_,
                                         std::vector<message_t> &parts_,
                                         SubmitFn submit_)
{
    std::vector<zlink_msg_t> native_parts;
    if (detail::move_parts_to_native (parts_, native_parts) != 0)
        return -1;

    size_t failed_index = 0;
    const int rc = detail::submit_native_parts (
      native_parts, failed_index, std::move (submit_));
    if (rc == 0) {
        result_ = send_result_t::sent;
        return 0;
    }

    const int err = errno;
    if (detail::classify_nonblocking_send_errno (err, result_)) {
        if (result_ != send_result_t::sent)
            detail::restore_parts_from_native (
              parts_, native_parts, failed_index);
        return 0;
    }

    detail::restore_parts_from_native (parts_, native_parts, failed_index);
    errno = err;
    return -1;
}

} // namespace detail
} // namespace zlink

#endif
