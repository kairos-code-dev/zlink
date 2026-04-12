/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_RUNTIME_HPP_INCLUDED
#define ZLINK_CPP_RUNTIME_HPP_INCLUDED

#include "common.hpp"

namespace zlink
{

inline void zlink_version (int &major_, int &minor_, int &patch_)
{
    ::zlink_version (&major_, &minor_, &patch_);
}

inline int proxy (void *frontend_, void *backend_, void *capture_ = NULL)
{
    return static_cast<int> (zlink_proxy (frontend_, backend_, capture_));
}

inline int proxy_steerable (void *frontend_,
                            void *backend_,
                            void *capture_,
                            void *control_)
{
    return static_cast<int> (
      zlink_proxy_steerable (frontend_, backend_, capture_, control_));
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
