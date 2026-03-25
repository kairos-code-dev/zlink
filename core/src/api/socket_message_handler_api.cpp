/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "api/poller_api_internal.hpp"
#include "api/service_api_internal.hpp"
#include "api/socket_api_internal.hpp"

namespace
{
void discard_socket_parts (const zlink_routing_id_t *,
                           zlink_msg_t *parts_,
                           size_t part_count_,
                           void *)
{
    zlink_multipart_close (parts_, part_count_);
}

int socket_send_ready_handler_internal (
  void *s_,
  zlink_send_ready_handler_fn handler_,
  void *userdata_)
{
    socket_handle_t handle = as_socket_handle (s_);
    if (!handle.socket)
        return -1;

    if (!handler_) {
        errno = EINVAL;
        return -1;
    }

    const int type = socket_type (handle);
    switch (type) {
        case ZLINK_CORE_SOCKET_PAIR:
        case ZLINK_CORE_SOCKET_PUB:
        case ZLINK_CORE_SOCKET_XPUB:
        case ZLINK_CORE_SOCKET_DEALER:
        case ZLINK_CORE_SOCKET_ROUTER:
        case ZLINK_CORE_SOCKET_STREAM:
            break;
        default:
            errno = ENOTSUP;
            return -1;
    }

    return handle.socket->socket_set_send_ready_handler_with_userdata (
      handler_, NULL, userdata_);
}
}

int zlink_recv_handler (void *s_,
                        zlink_socket_msg_handler_fn handler_,
                        void *userdata_)
{
    if (!handler_) {
        errno = EINVAL;
        return -1;
    }

    const int service_rc =
      zlink_service_msg_recv_handler_internal (s_, handler_, userdata_);
    if (service_rc == 0 || errno != EFAULT)
        return service_rc;

    socket_handle_t handle = as_socket_handle (s_);
    if (!handle.socket)
        return -1;

    if (handler_ == &discard_socket_parts) {
        errno = EINVAL;
        return -1;
    }

    const int type = socket_type (handle);
    switch (type) {
        case ZLINK_CORE_SOCKET_PAIR:
        case ZLINK_CORE_SOCKET_DEALER:
        case ZLINK_CORE_SOCKET_ROUTER:
            return handle.socket->socket_set_msg_handler_with_userdata (
              handler_, NULL, userdata_);
        case ZLINK_CORE_SOCKET_SUB:
        case ZLINK_CORE_SOCKET_XSUB:
        case ZLINK_CORE_SOCKET_PUB:
        case ZLINK_CORE_SOCKET_XPUB:
            errno = ENOTSUP;
            return -1;
        case ZLINK_CORE_SOCKET_STREAM:
            return handle.socket->stream_set_msg_handler_with_userdata (
              handler_, userdata_);
        default:
            errno = ENOTSUP;
            return -1;
    }
}

int zlink_recv_spot_handler (void *s_,
                             zlink_subscribe_handler_fn handler_,
                             void *userdata_)
{
    if (!handler_) {
        errno = EINVAL;
        return -1;
    }
    const int service_rc =
      zlink_service_recv_handler_internal (s_, handler_, userdata_);
    if (service_rc == 0 || errno != EFAULT)
        return service_rc;

    socket_handle_t handle = as_socket_handle (s_);
    if (!handle.socket)
        return -1;

    const int type = socket_type (handle);
    switch (type) {
        case ZLINK_CORE_SOCKET_SUB:
        case ZLINK_CORE_SOCKET_XSUB:
            return handle.socket->socket_set_spot_handler_with_userdata (
              handler_, userdata_);
        default:
            errno = ENOTSUP;
            return -1;
    }
}

int zlink_subscribe_handler (void *s_,
                             zlink_subscribe_handler_fn handler_,
                             void *userdata_)
{
    return zlink_recv_spot_handler (s_, handler_, userdata_);
}

int zlink_send_ready_handler (void *s_,
                              zlink_send_ready_handler_fn handler_,
                              void *userdata_)
{
    if (!handler_) {
        errno = EINVAL;
        return -1;
    }

    const int service_rc =
      zlink_service_send_ready_handler_internal (s_, handler_, userdata_);
    if (service_rc == 0 || errno != EFAULT)
        return service_rc;

    return socket_send_ready_handler_internal (s_, handler_, userdata_);
}

int validate_socket_callback_poller_events (socket_handle_t handle_,
                                            short events_)
{
    if (!handle_.socket)
        return 0;
    const int type = socket_type (handle_);
    if ((events_ & ZLINK_POLLIN) != 0) {
        if (handle_.socket->socket_msg_dispatch_active ()
            || ((type == ZLINK_CORE_SOCKET_SUB
                 || type == ZLINK_CORE_SOCKET_XSUB)
                && handle_.socket->sub_dispatch_active ())
            || (type == ZLINK_CORE_SOCKET_STREAM
                && handle_.socket->stream_dispatch_active ())) {
            errno = EBUSY;
            return -1;
        }
    }
    if ((events_ & ZLINK_POLLOUT) != 0
        && handle_.socket->send_ready_handler_active ()) {
        errno = EBUSY;
        return -1;
    }
    return 0;
}
