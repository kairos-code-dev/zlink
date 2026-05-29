/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_REQUEST_RESULT_INTERNAL_HPP_INCLUDED__
#define __ZLINK_REQUEST_RESULT_INTERNAL_HPP_INCLUDED__

#include "api/message/result_errno_internal.hpp"

namespace zlink
{
namespace request_result_internal
{
inline zlink_request_result_t from_errno (int err_)
{
    switch (err_) {
        case EACCES:
        case ECONNREFUSED:
            return ZLINK_REQUEST_REJECTED;
        case ESTALE:
            return ZLINK_REQUEST_CONFLICT;
        case EBUSY:
            return ZLINK_REQUEST_BUSY;
        case ENOTCONN:
        case EHOSTUNREACH:
            return ZLINK_REQUEST_NOT_CONNECTED;
        case EINVAL:
        case EFAULT:
            return ZLINK_REQUEST_INVALID_ARGUMENT;
        case EFSM:
            return ZLINK_REQUEST_INVALID_STATE;
        case ENOTSUP:
#if !defined(EOPNOTSUPP) || EOPNOTSUPP != ENOTSUP
        case EOPNOTSUPP:
#endif
            return ZLINK_REQUEST_NOT_SUPPORTED;
        default:
            break;
    }
    switch (zlink::internal_errno::classify_request_completion (err_)) {
        case zlink::internal_errno::request_completion_class::success:
            return ZLINK_REQUEST_OK;
        case zlink::internal_errno::request_completion_class::timeout:
            return ZLINK_REQUEST_TIMED_OUT;
        case zlink::internal_errno::request_completion_class::target_not_found:
            return ZLINK_REQUEST_NOT_FOUND;
        case zlink::internal_errno::request_completion_class::terminated:
            return ZLINK_REQUEST_TERMINATED;
        case zlink::internal_errno::request_completion_class::protocol_error:
            return ZLINK_REQUEST_PROTOCOL_ERROR;
        case zlink::internal_errno::request_completion_class::unknown:
        default:
            return ZLINK_REQUEST_INTERNAL_ERROR;
    }
}
}
}

#endif
