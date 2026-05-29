/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_CONNECT_RESULT_INTERNAL_HPP_INCLUDED__
#define __ZLINK_CONNECT_RESULT_INTERNAL_HPP_INCLUDED__

#include "api/message/result_errno_internal.hpp"

namespace zlink
{
namespace connect_result_internal
{
inline zlink_connect_result_t from_errno (int err_)
{
    switch (err_) {
        case 0:
            return ZLINK_CONNECT_OK;
        case EINVAL:
            return ZLINK_CONNECT_INVALID_ARGUMENT;
        case ENOTSUP:
#if !defined(EOPNOTSUPP) || EOPNOTSUPP != ENOTSUP
        case EOPNOTSUPP:
#endif
            return ZLINK_CONNECT_NOT_SUPPORTED;
        case EFAULT:
            return ZLINK_CONNECT_INVALID_HANDLE;
        case ENOENT:
            return ZLINK_CONNECT_NOT_FOUND;
        case EADDRINUSE:
            return ZLINK_CONNECT_CONFLICT;
        case EBUSY:
            return ZLINK_CONNECT_BUSY;
        default:
            return ZLINK_CONNECT_INTERNAL_ERROR;
    }
}

inline zlink_connect_result_t from_rc (int rc_)
{
    if (rc_ == 0)
        return ZLINK_CONNECT_OK;

    return from_errno (zlink::result_errno_internal::rc_errno_or_io ());
}
}
}

#endif
