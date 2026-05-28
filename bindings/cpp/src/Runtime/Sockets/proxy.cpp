/* SPDX-License-Identifier: MPL-2.0 */

#include <zlink/Contracts/Sockets/socket_contracts.hpp>

#include <Runtime/Sockets/socket_access.hpp>

#include <zlink.h>

namespace zlink
{

void proxy (base_socket_t &frontend_, base_socket_t &backend_)
{
    detail::throw_if_failed<config_error_t> (
      static_cast<config_result_t> (
        zlink_proxy (detail::native_handle (frontend_),
                     detail::native_handle (backend_), NULL)));
}

void proxy (base_socket_t &frontend_,
            base_socket_t &backend_,
            base_socket_t &capture_)
{
    detail::throw_if_failed<config_error_t> (
      static_cast<config_result_t> (
        zlink_proxy (detail::native_handle (frontend_),
                     detail::native_handle (backend_),
                     detail::native_handle (capture_))));
}

void proxy_steerable (base_socket_t &frontend_,
                      base_socket_t &backend_,
                      base_socket_t &capture_,
                      base_socket_t &control_)
{
    detail::throw_if_failed<config_error_t> (
      static_cast<config_result_t> (
        zlink_proxy_steerable (detail::native_handle (frontend_),
                               detail::native_handle (backend_),
                               detail::native_handle (capture_),
                               detail::native_handle (control_))));
}

} // namespace zlink
