/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "api/poller_api_internal.hpp"
#include "api/service_api_internal.hpp"
#include "api/socket_api_internal.hpp"
#include "api/handler_result_internal.hpp"

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

zlink_handler_result_t zlink_recv_handler (void *s_,
                                          zlink_socket_msg_handler_fn handler_,
                                          void *userdata_)
{
    if (!handler_) {
        errno = EINVAL;
        return ZLINK_HANDLER_INVALID_ARGUMENT;
    }

    const int service_rc =
      zlink_service_msg_recv_handler_internal (s_, handler_, userdata_);
    if (service_rc == 0 || errno != EFAULT)
        return zlink::handler_result_internal::from_rc (service_rc);

    socket_handle_t handle = as_socket_handle (s_);
    if (!handle.socket)
        return zlink::handler_result_internal::from_errno (EFAULT);

    if (handler_ == &discard_socket_parts) {
        errno = EINVAL;
        return ZLINK_HANDLER_INVALID_ARGUMENT;
    }

    const int type = socket_type (handle);
    switch (type) {
        case ZLINK_CORE_SOCKET_PAIR:
        case ZLINK_CORE_SOCKET_DEALER:
            return zlink::handler_result_internal::from_rc (
              handle.socket->socket_set_msg_handler_with_userdata (
                handler_, NULL, userdata_));
        case ZLINK_CORE_SOCKET_ROUTER:
            errno = EOPNOTSUPP;
            return ZLINK_HANDLER_NOT_SUPPORTED;
        case ZLINK_CORE_SOCKET_SUB:
        case ZLINK_CORE_SOCKET_XSUB:
        case ZLINK_CORE_SOCKET_PUB:
        case ZLINK_CORE_SOCKET_XPUB:
            errno = ENOTSUP;
            return ZLINK_HANDLER_NOT_SUPPORTED;
        case ZLINK_CORE_SOCKET_STREAM:
            return zlink::handler_result_internal::from_rc (
              handle.socket->stream_set_msg_handler_with_userdata (handler_,
                                                                   userdata_));
        default:
            errno = ENOTSUP;
            return ZLINK_HANDLER_NOT_SUPPORTED;
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

zlink_handler_result_t zlink_subscribe_handler (void *s_,
                                               zlink_subscribe_handler_fn handler_,
                                               void *userdata_)
{
    return zlink::handler_result_internal::from_rc (
      zlink_recv_spot_handler (s_, handler_, userdata_));
}

zlink_handler_result_t zlink_send_ready_handler (void *s_,
                                                zlink_send_ready_handler_fn handler_,
                                                void *userdata_)
{
    if (!handler_) {
        errno = EINVAL;
        return ZLINK_HANDLER_INVALID_ARGUMENT;
    }

    const int service_rc =
      zlink_service_send_ready_handler_internal (s_, handler_, userdata_);
    if (service_rc == 0 || errno != EFAULT)
        return zlink::handler_result_internal::from_rc (service_rc);

    return zlink::handler_result_internal::from_rc (
      socket_send_ready_handler_internal (s_, handler_, userdata_));
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
