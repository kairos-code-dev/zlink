/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "utils/err.hpp"
#include "api/service_api_internal.hpp"

#include "services/gateway/gateway_access.hpp"

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

    if (is_registered_gateway_handle (handle_)) {
        zlink::gateway_t *gateway = zlink::gateway_access_t::from_handle (handle_);
        if (!gateway)
            return -1;
        zlink::service_public_api_guard_t *guard =
          zlink::gateway_access_t::public_api_guard (gateway);
        if (!guard) {
            errno = EFAULT;
            return -1;
        }
        zlink::service_public_api_scope_t admission (*guard);
        if (!admission.acquired ())
            return -1;
        bool already_active = false;
        if (gateway_activate_send_ready_mode (gateway, &already_active) != 0)
            return -1;
        const int rc = zlink::gateway_access_t::set_send_ready_handler (
          gateway, handler_, userdata_);
        if (rc != 0 && !already_active)
            gateway_revert_send_ready_mode (gateway);
        return rc;
    }

    if (is_registered_spot_handle (handle_)) {
        return spot_install_send_ready_handler (
          static_cast<spot_handle_t *> (handle_), handler_, userdata_);
    }

    if (is_registered_spot_node_handle (handle_)) {
        return spot_node_install_send_ready_handler (
          static_cast<zlink::spot_node_t *> (handle_), handler_, userdata_);
    }

    errno = EFAULT;
    return -1;
}
