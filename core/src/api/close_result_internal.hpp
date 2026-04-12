/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_CLOSE_RESULT_INTERNAL_HPP_INCLUDED__
#define __ZLINK_CLOSE_RESULT_INTERNAL_HPP_INCLUDED__

#include "core/internal_errno.hpp"

namespace zlink
{
namespace close_result_internal
{
inline zlink_close_result_t from_errno (int err_)
{
    switch (err_) {
        case 0:
            return ZLINK_CLOSE_OK;
        case EBUSY:
            return ZLINK_CLOSE_BUSY;
#ifdef ESHUTDOWN
        case ESHUTDOWN:
            return ZLINK_CLOSE_SHUTDOWN;
#endif
        case EFAULT:
            return ZLINK_CLOSE_INVALID_HANDLE;
        default:
            return ZLINK_CLOSE_INVALID_HANDLE;
    }
}

inline zlink_close_result_t from_rc (int rc_)
{
    if (rc_ == 0)
        return ZLINK_CLOSE_OK;

    const int saved_errno = errno;
    return from_errno (saved_errno != 0 ? saved_errno : EIO);
}
}
}

#endif
