/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include <string.h>

#include "api/socket_api_internal.hpp"
#include "core/address.hpp"
#include "sockets/proxy.hpp"

int zlink_bind (void *s_, const char *addr_)
{
    socket_handle_t handle = as_socket_handle (s_);
    if (!handle.socket)
        return -1;
    return handle.socket->bind (addr_);
}

int zlink_connect (void *s_, const char *addr_)
{
    socket_handle_t handle = as_socket_handle (s_);
    if (!handle.socket)
        return -1;
    return handle.socket->connect (addr_);
}

int zlink_unbind (void *s_, const char *addr_)
{
    socket_handle_t handle = as_socket_handle (s_);
    if (!handle.socket)
        return -1;
    return handle.socket->term_endpoint (addr_);
}

int zlink_disconnect (void *s_, const char *addr_)
{
    socket_handle_t handle = as_socket_handle (s_);
    if (!handle.socket)
        return -1;
    return handle.socket->term_endpoint (addr_);
}

int zlink_stream_attach_raw (void *s_, zlink_stream_on_raw_fn on_raw_)
{
    socket_handle_t handle = as_socket_handle (s_);
    if (!handle.socket)
        return -1;
    if (!on_raw_) {
        errno = EINVAL;
        return -1;
    }
    if (!is_stream_type (handle)) {
        errno = EINVAL;
        return -1;
    }
    if (handle.socket->stream_dispatch_in_callback ()) {
        errno = EBUSY;
        return -1;
    }

    stream_api_lock_t api_lock (handle);
    return handle.socket->stream_dispatch_start_raw (on_raw_);
}

int zlink_stream_detach (void *s_)
{
    socket_handle_t handle = as_socket_handle (s_);
    if (!handle.socket)
        return -1;
    if (!is_stream_type (handle)) {
        errno = EINVAL;
        return -1;
    }
    if (handle.socket->stream_dispatch_in_callback ()) {
        errno = EBUSY;
        return -1;
    }

    stream_api_lock_t api_lock (handle);
    return handle.socket->stream_dispatch_stop ();
}

int zlink_proxy (void *frontend_, void *backend_, void *capture_)
{
    if (!frontend_ || !backend_) {
        errno = EFAULT;
        return -1;
    }

    socket_handle_t frontend = as_socket_handle (frontend_);
    if (!frontend.socket)
        return -1;
    socket_handle_t backend = as_socket_handle (backend_);
    if (!backend.socket)
        return -1;

    zlink::socket_base_t *capture_socket = NULL;
    if (capture_) {
        socket_handle_t capture = as_socket_handle (capture_);
        if (!capture.socket)
            return -1;
        capture_socket = capture.socket;
    }

    return zlink::proxy (frontend.socket, backend.socket, capture_socket);
}

int zlink_proxy_steerable (void *frontend_,
                           void *backend_,
                           void *capture_,
                           void *control_)
{
    if (!frontend_ || !backend_) {
        errno = EFAULT;
        return -1;
    }

    socket_handle_t frontend = as_socket_handle (frontend_);
    if (!frontend.socket)
        return -1;
    socket_handle_t backend = as_socket_handle (backend_);
    if (!backend.socket)
        return -1;

    zlink::socket_base_t *capture_socket = NULL;
    if (capture_) {
        socket_handle_t capture = as_socket_handle (capture_);
        if (!capture.socket)
            return -1;
        capture_socket = capture.socket;
    }

    zlink::socket_base_t *control_socket = NULL;
    if (control_) {
        socket_handle_t control = as_socket_handle (control_);
        if (!control.socket)
            return -1;
        control_socket = control.socket;
    }

    return zlink::proxy_steerable (frontend.socket, backend.socket,
                                   capture_socket, control_socket);
}

int zlink_has (const char *capability_)
{
    if (strcmp (capability_, "tcp") == 0)
        return true;
#if defined(ZLINK_HAVE_IPC)
    if (strcmp (capability_, zlink::protocol_name::ipc) == 0)
        return true;
#endif
#ifdef ZLINK_HAVE_WS
    if (strcmp (capability_, zlink::protocol_name::ws) == 0)
        return true;
#endif
#ifdef ZLINK_HAVE_WSS
    if (strcmp (capability_, zlink::protocol_name::wss) == 0)
        return true;
#endif
#ifdef ZLINK_HAVE_TLS
    if (strcmp (capability_, zlink::protocol_name::tls) == 0)
        return true;
#endif
    return false;
}
