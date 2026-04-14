/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_RUNTIME_HPP_INCLUDED
#define ZLINK_CPP_RUNTIME_HPP_INCLUDED

#include "common.hpp"
#include "error.hpp"

namespace zlink
{

inline void zlink_version (int &major_, int &minor_, int &patch_)
{
    ::zlink_version (&major_, &minor_, &patch_);
}

inline const char *zlink_strerror (int errnum_)
{
    return ::zlink_strerror (errnum_);
}

inline void proxy (void *frontend_, void *backend_, void *capture_ = NULL)
{
    if (zlink_proxy (frontend_, backend_, capture_) != 0)
        throw last_error ();
}

inline void proxy_steerable (void *frontend_,
                             void *backend_,
                             void *capture_,
                             void *control_)
{
    if (zlink_proxy_steerable (frontend_, backend_, capture_, control_) != 0)
        throw last_error ();
}

inline bool has (const std::string &capability_)
{
    return zlink_has (capability_.c_str ());
}

inline void sleep (int seconds_)
{
    zlink_sleep (seconds_);
}

inline void multipart_close (zlink_msg_t *parts_, size_t count_)
{
    zlink_multipart_close (parts_, count_);
}

} // namespace zlink

#endif
