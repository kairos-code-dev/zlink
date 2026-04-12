/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_CONFIG_RESULT_INTERNAL_HPP_INCLUDED__
#define __ZLINK_CONFIG_RESULT_INTERNAL_HPP_INCLUDED__

#include "core/internal_errno.hpp"

namespace zlink
{
namespace config_result_internal
{
inline zlink_config_result_t from_errno (int err_)
{
    switch (err_) {
        case 0:
            return ZLINK_CONFIG_OK;
        case EFAULT:
            return ZLINK_CONFIG_INVALID_HANDLE;
        case EINVAL:
            return ZLINK_CONFIG_INVALID_ARGUMENT;
        case ENOTSUP:
#if !defined(EOPNOTSUPP) || EOPNOTSUPP != ENOTSUP
        case EOPNOTSUPP:
#endif
            return ZLINK_CONFIG_NOT_SUPPORTED;
        default:
            return ZLINK_CONFIG_INVALID_ARGUMENT;
    }
}

inline zlink_config_result_t from_rc (int rc_)
{
    if (rc_ == 0)
        return ZLINK_CONFIG_OK;

    const int saved_errno = errno;
    return from_errno (saved_errno != 0 ? saved_errno : EIO);
}
}
}

#endif
