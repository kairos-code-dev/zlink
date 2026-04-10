/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "utils/err.hpp"
#include "api/service_api_internal.hpp"

int validate_recv_flags (int flags_)
{
    if (flags_ != 0 && flags_ != ZLINK_DONTWAIT) {
        errno = ENOTSUP;
        return -1;
    }
    return 0;
}

int zlink_service_send_ready_handler_internal (
  void *handle_,
  zlink_send_ready_handler_fn handler_,
  void *userdata_)
{
    if (!handler_) {
        errno = EINVAL;
        return -1;
    }

    const zlink::service_handle_resolution_t resolved =
      zlink::resolve_service_handle (handle_);

    if (resolved.kind == zlink::service_handle_spot) {
        return spot_install_send_ready_handler (
          static_cast<spot_handle_t *> (handle_), handler_, userdata_);
    }

    if (resolved.kind == zlink::service_handle_spot_node) {
        return spot_node_install_send_ready_handler (
          static_cast<zlink::spot_node_t *> (handle_), handler_, userdata_);
    }

    errno = EFAULT;
    return -1;
}

int zlink_service_msg_recv_handler_internal (
  void *handle_,
  zlink_socket_msg_handler_fn handler_,
  void *userdata_)
{
    const zlink::service_handle_resolution_t resolved =
      zlink::resolve_service_handle (handle_);
    if (resolved.kind == zlink::service_handle_spot
        || resolved.kind == zlink::service_handle_spot_node) {
        errno = EOPNOTSUPP;
        return -1;
    }
    LIBZLINK_UNUSED (handler_);
    LIBZLINK_UNUSED (userdata_);
    errno = EFAULT;
    return -1;
}
