/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "utils/err.hpp"
#include "api/service_api_internal.hpp"

#include "services/gateway/gateway.hpp"

namespace
{
} // namespace

int gateway_install_recv_handler (zlink::gateway_t *gateway_,
                                  zlink_socket_msg_handler_fn handler_,
                                  void *userdata_)
{
    if (!gateway_ || !handler_) {
        errno = EINVAL;
        return -1;
    }
    if (!gateway_->check_tag ()) {
        errno = EFAULT;
        return -1;
    }

    zlink::service_public_api_scope_t admission (gateway_->public_api_guard ());
    if (!admission.acquired ())
        return -1;
    if (gateway_transition_to_callback_mode (gateway_) != 0)
        return -1;
    const int rc = gateway_->set_handler (handler_, userdata_);
    if (rc != 0)
        gateway_revert_callback_transition (gateway_);
    return rc;
}

int gateway_install_send_ready_handler (zlink::gateway_t *gateway_,
                                        zlink_send_ready_handler_fn handler_,
                                        void *userdata_)
{
    if (!gateway_ || !handler_) {
        errno = EINVAL;
        return -1;
    }
    if (!gateway_->check_tag ()) {
        errno = EFAULT;
        return -1;
    }

    zlink::service_public_api_scope_t admission (gateway_->public_api_guard ());
    if (!admission.acquired ())
        return -1;
    bool already_active = false;
    if (gateway_activate_send_ready_mode (gateway_, &already_active) != 0)
        return -1;
    const int rc = gateway_->set_send_ready_handler (handler_, userdata_);
    if (rc != 0 && !already_active)
        gateway_revert_send_ready_mode (gateway_);
    return rc;
}

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
        return gateway_install_send_ready_handler (
          static_cast<zlink::gateway_t *> (handle_), handler_, userdata_);
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
