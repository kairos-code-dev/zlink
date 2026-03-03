/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_RUNTIME_HPP_INCLUDED
#define ZLINK_CPP_RUNTIME_HPP_INCLUDED

#include "socket.hpp"

namespace zlink
{

struct version_info_t
{
    int major;
    int minor;
    int patch;
};

inline void version (int *major_, int *minor_, int *patch_)
{
    zlink_version (major_, minor_, patch_);
}

inline version_info_t version ()
{
    version_info_t out = {0, 0, 0};
    zlink_version (&out.major, &out.minor, &out.patch);
    return out;
}

inline void sleep (int seconds_)
{
    zlink_sleep (seconds_);
}

inline int proxy (socket_t &frontend_,
                  socket_t &backend_,
                  socket_t *capture_ = NULL)
{
    return zlink_proxy (frontend_.handle (),
                        backend_.handle (),
                        capture_ ? capture_->handle () : NULL);
}

inline int proxy_steerable (socket_t &frontend_,
                            socket_t &backend_,
                            socket_t *capture_,
                            socket_t &control_)
{
    return zlink_proxy_steerable (
      frontend_.handle (),
      backend_.handle (),
      capture_ ? capture_->handle () : NULL,
      control_.handle ());
}

inline bool has (const char *capability_)
{
    return zlink_has (capability_) != 0;
}

} // namespace zlink

#endif
