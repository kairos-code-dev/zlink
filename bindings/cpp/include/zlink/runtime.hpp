/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_RUNTIME_HPP_INCLUDED
#define ZLINK_CPP_RUNTIME_HPP_INCLUDED

#include "socket.hpp"

namespace zlink
{

/**
 * @brief Semantic version triple of the linked zlink runtime.
 */
struct version_info_t
{
    int major;
    int minor;
    int patch;
};

/**
 * @brief Query linked zlink version.
 * @param major_ Output major version.
 * @param minor_ Output minor version.
 * @param patch_ Output patch version.
 */
inline void version (int *major_, int *minor_, int *patch_)
{
    zlink_version (major_, minor_, patch_);
}

/**
 * @brief Query linked zlink version.
 * @return Version structure.
 */
inline version_info_t version ()
{
    version_info_t out = {0, 0, 0};
    zlink_version (&out.major, &out.minor, &out.patch);
    return out;
}

/**
 * @brief Sleep for a whole-second duration.
 * @param seconds_ Number of seconds.
 */
inline void sleep (int seconds_)
{
    zlink_sleep (seconds_);
}

/**
 * @brief Start a builtin forwarding proxy loop.
 * @param frontend_ Frontend socket.
 * @param backend_ Backend socket.
 * @param capture_ Optional capture socket.
 * @return 0 on success, -1 on failure.
 */
inline int proxy (socket_t &frontend_,
                  socket_t &backend_,
                  socket_t *capture_ = NULL)
{
    return zlink_proxy (frontend_.handle (),
                        backend_.handle (),
                        capture_ ? capture_->handle () : NULL);
}

/**
 * @brief Start a steerable proxy loop.
 * @param frontend_ Frontend socket.
 * @param backend_ Backend socket.
 * @param capture_ Optional capture socket.
 * @param control_ Control socket for commands.
 * @return 0 on success, -1 on failure.
 */
inline int proxy (socket_t &frontend_,
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

/**
 * @brief Check whether a runtime capability is available.
 * @param capability_ Capability name.
 * @return `true` when supported.
 */
inline bool has (const std::string &capability_)
{
    return zlink_has (capability_.c_str ()) != 0;
}

} // namespace zlink

#endif
