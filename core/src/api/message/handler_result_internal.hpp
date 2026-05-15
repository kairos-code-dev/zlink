/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_HANDLER_RESULT_INTERNAL_HPP_INCLUDED__
#define __ZLINK_HANDLER_RESULT_INTERNAL_HPP_INCLUDED__

#include "core/internal_errno.hpp"

namespace zlink
{
namespace handler_result_internal
{
inline zlink_handler_result_t from_errno (int err_)
{
    switch (err_) {
        case 0:
            return ZLINK_HANDLER_OK;
        case EINVAL:
            return ZLINK_HANDLER_INVALID_ARGUMENT;
        case EBUSY:
            return ZLINK_HANDLER_BUSY;
        case ENOTSUP:
#if !defined(EOPNOTSUPP) || EOPNOTSUPP != ENOTSUP
        case EOPNOTSUPP:
#endif
            return ZLINK_HANDLER_NOT_SUPPORTED;
        case EDEADLK:
            return ZLINK_HANDLER_DEADLOCK;
        case EFAULT:
            return ZLINK_HANDLER_INVALID_HANDLE;
        default:
            return ZLINK_HANDLER_INTERNAL_ERROR;
    }
}

inline zlink_handler_result_t from_rc (int rc_)
{
    if (rc_ == 0)
        return ZLINK_HANDLER_OK;

    const int saved_errno = errno;
    return from_errno (saved_errno != 0 ? saved_errno : EIO);
}
}
}

#endif
