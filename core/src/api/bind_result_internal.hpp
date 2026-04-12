/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_BIND_RESULT_INTERNAL_HPP_INCLUDED__
#define __ZLINK_BIND_RESULT_INTERNAL_HPP_INCLUDED__

#include "core/internal_errno.hpp"

namespace zlink
{
namespace bind_result_internal
{
inline zlink_bind_result_t from_errno (int err_)
{
    switch (err_) {
        case 0:
            return ZLINK_BIND_OK;
        case EINVAL:
            return ZLINK_BIND_INVALID_ARGUMENT;
        case EADDRINUSE:
            return ZLINK_BIND_ADDR_IN_USE;
        case ENOTSUP:
#if !defined(EOPNOTSUPP) || EOPNOTSUPP != ENOTSUP
        case EOPNOTSUPP:
#endif
            return ZLINK_BIND_NOT_SUPPORTED;
        case EFAULT:
            return ZLINK_BIND_INVALID_HANDLE;
        default:
            return ZLINK_BIND_INVALID_HANDLE;
    }
}

inline zlink_bind_result_t from_rc (int rc_)
{
    if (rc_ == 0)
        return ZLINK_BIND_OK;

    const int saved_errno = errno;
    return from_errno (saved_errno != 0 ? saved_errno : EIO);
}
}
}

#endif
