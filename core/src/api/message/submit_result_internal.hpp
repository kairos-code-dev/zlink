/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_SUBMIT_RESULT_INTERNAL_HPP_INCLUDED__
#define __ZLINK_SUBMIT_RESULT_INTERNAL_HPP_INCLUDED__

#include "core/internal_errno.hpp"

namespace zlink
{
namespace submit_result_internal
{
inline zlink_submit_result_t from_errno (int err_)
{
    switch (zlink::internal_errno::classify_submit (err_)) {
        case zlink::internal_errno::submit_error_class::none:
            return ZLINK_SUBMIT_OK;
        case zlink::internal_errno::submit_error_class::control_flow:
            switch (err_) {
                case EAGAIN:
                    return ZLINK_SUBMIT_BACKPRESSURED;
                case ENOTCONN:
                case EHOSTUNREACH:
                    return ZLINK_SUBMIT_NOT_CONNECTED;
                case ECONNREFUSED:
                    return ZLINK_SUBMIT_NOT_ADMITTED;
                case ENOENT:
                    return ZLINK_SUBMIT_NOT_FOUND;
                default:
                    return ZLINK_SUBMIT_INTERNAL_ERROR;
            }
        case zlink::internal_errno::submit_error_class::runtime_failure:
            return ZLINK_SUBMIT_TERMINATED;
        case zlink::internal_errno::submit_error_class::contract_failure:
            switch (err_) {
                case EFAULT:
                    return ZLINK_SUBMIT_INVALID_HANDLE;
                case EINVAL:
                    return ZLINK_SUBMIT_INVALID_ARGUMENT;
                case ENOTSUP:
#if !defined(EOPNOTSUPP) || EOPNOTSUPP != ENOTSUP
                case EOPNOTSUPP:
#endif
                    return ZLINK_SUBMIT_NOT_SUPPORTED;
                case EFSM:
                case EBUSY:
                    return ZLINK_SUBMIT_INVALID_STATE;
                case EMTHREAD:
                    return ZLINK_SUBMIT_THREAD_VIOLATION;
                default:
                    return ZLINK_SUBMIT_INTERNAL_ERROR;
            }
        case zlink::internal_errno::submit_error_class::internal_failure:
            switch (err_) {
                case ENOMEM:
                case ENOBUFS:
                    return ZLINK_SUBMIT_OUT_OF_MEMORY;
                default:
                    return ZLINK_SUBMIT_INTERNAL_ERROR;
            }
        case zlink::internal_errno::submit_error_class::unknown:
        default:
            return ZLINK_SUBMIT_INTERNAL_ERROR;
    }
}

inline zlink_submit_result_t from_rc (int rc_)
{
    if (rc_ == 0)
        return ZLINK_SUBMIT_OK;

    const int saved_errno = errno;
    return from_errno (saved_errno != 0 ? saved_errno : EIO);
}

inline zlink_submit_result_t from_request_submit_errno (int err_)
{
    // Request submit uses EBUSY specifically when the pending request sequence
    // space is exhausted.
    if (err_ == EBUSY)
        return ZLINK_SUBMIT_SEQ_EXHAUSTED;

    return from_errno (err_);
}

inline zlink_submit_result_t from_request_submit_rc (int rc_)
{
    if (rc_ == 0)
        return ZLINK_SUBMIT_OK;

    const int saved_errno = errno;
    return from_request_submit_errno (saved_errno != 0 ? saved_errno : EIO);
}
}
}

#endif
