/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_RUNTIME_HPP_INCLUDED
#define ZLINK_CPP_RUNTIME_HPP_INCLUDED

#include "common.hpp"
#include "../Errors/error.hpp"
#include "../../Runtime/Native/socket_handle.hpp"

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

template<typename FrontendSocket, typename BackendSocket>
inline void proxy (FrontendSocket &frontend_, BackendSocket &backend_)
{
    detail::throw_if_failed<config_error_t> (
      static_cast<config_result_t> (
        zlink_proxy (detail::native_handle (frontend_),
                     detail::native_handle (backend_), NULL)));
}

template<typename FrontendSocket, typename BackendSocket, typename CaptureSocket>
inline void proxy (FrontendSocket &frontend_,
                   BackendSocket &backend_,
                   CaptureSocket &capture_)
{
    detail::throw_if_failed<config_error_t> (
      static_cast<config_result_t> (
        zlink_proxy (detail::native_handle (frontend_),
                     detail::native_handle (backend_),
                     detail::native_handle (capture_))));
}

template<typename FrontendSocket,
         typename BackendSocket,
         typename CaptureSocket,
         typename ControlSocket>
inline void proxy_steerable (FrontendSocket &frontend_,
                             BackendSocket &backend_,
                             CaptureSocket &capture_,
                             ControlSocket &control_)
{
    detail::throw_if_failed<config_error_t> (
      static_cast<config_result_t> (
        zlink_proxy_steerable (detail::native_handle (frontend_),
                               detail::native_handle (backend_),
                               detail::native_handle (capture_),
                               detail::native_handle (control_))));
}

inline bool has (const std::string &capability_)
{
    return zlink_has (capability_.c_str ());
}

inline void sleep (int seconds_)
{
    zlink_sleep (seconds_);
}

} // namespace zlink

#endif
