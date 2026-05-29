/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_CONFIG_RESULT_INTERNAL_HPP_INCLUDED__
#define __ZLINK_CONFIG_RESULT_INTERNAL_HPP_INCLUDED__

#include "api/message/result_errno_internal.hpp"

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
        case EMSGSIZE:
            return ZLINK_CONFIG_INVALID_ARGUMENT;
        case ENOTSUP:
#if !defined(EOPNOTSUPP) || EOPNOTSUPP != ENOTSUP
        case EOPNOTSUPP:
#endif
            return ZLINK_CONFIG_NOT_SUPPORTED;
        case EBUSY:
        case ESHUTDOWN:
            return ZLINK_CONFIG_INVALID_STATE;
        case ENOENT:
            return ZLINK_CONFIG_NOT_FOUND;
        default:
            return ZLINK_CONFIG_INTERNAL_ERROR;
    }
}

inline zlink_config_result_t from_rc (int rc_)
{
    if (rc_ == 0)
        return ZLINK_CONFIG_OK;

    return from_errno (zlink::result_errno_internal::rc_errno_or_io ());
}
}
}

#endif
