/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "utils/err.hpp"
#include "api/handler_result_internal.hpp"
#include "api/monitor_api_internal.hpp"
#include "api/service_api_internal.hpp"

#include "api/socket_api_internal.hpp"
#include "services/discovery/discovery_access.hpp"

namespace
{
int attach_service_monitor_handler_state (void *monitor_,
                                          zlink_service_monitor_handler_fn handler_,
                                          void *userdata_)
{
    if (!monitor_) {
        errno = EFAULT;
        return -1;
    }
    if (!handler_) {
        errno = EINVAL;
        return -1;
    }

    socket_handle_t handle = as_socket_handle (monitor_);
    if (!handle.socket)
        return -1;

    monitor_handler_state_t *state = find_monitor_handler_state (handle.socket);
    if (!state || !state->service) {
        errno = EINVAL;
        return -1;
    }
    if (state->socket_handler.load (std::memory_order_acquire)
        || state->service_handler.load (std::memory_order_acquire)) {
        errno = EBUSY;
        return -1;
    }

    return set_monitor_handler_state (
      handle.socket, NULL, handler_, true,
      state->snapshot_provider.load (std::memory_order_acquire),
      state->snapshot_subject.load (std::memory_order_acquire), NULL, userdata_);
}

void *open_discovery_service_monitor_internal (
  void *discovery_,
  zlink_discovery_monitor_event_mask_t events_,
  zlink_service_monitor_handler_fn handler_,
  void *userdata_)
{
    if (!discovery_)
        return NULL;
    zlink::discovery_t *discovery =
      zlink::discovery_access_t::from_handle (discovery_);
    if (!discovery)
        return NULL;
    void *monitor = zlink::discovery_access_t::monitor_open (discovery, events_);
    if (!monitor)
        return NULL;
    socket_handle_t handle = as_socket_handle (monitor);
    if (!handle.socket
        || set_monitor_handler_state (handle.socket, NULL, handler_, true, NULL,
                                      discovery, NULL, userdata_)
             != 0) {
        const int err = errno;
        zlink_monitor_close (&monitor);
        errno = err;
        return NULL;
    }
    return monitor;
}

}

zlink_handler_result_t zlink_service_monitor_handler (
  void *monitor_,
  zlink_service_monitor_handler_fn handler_,
  void *userdata_)
{
    return zlink::handler_result_internal::from_rc (
      attach_service_monitor_handler_state (monitor_, handler_, userdata_));
}

void *zlink_service_monitor_open (
  void *target_, const zlink_service_monitor_open_options_t *options_)
{
    if (!target_) {
        errno = EFAULT;
        return NULL;
    }
    if (!options_) {
        errno = EINVAL;
        return NULL;
    }

    const zlink::service_handle_resolution_t resolved =
      zlink::resolve_service_handle (target_);

    if (resolved.kind == zlink::service_handle_discovery) {
        return open_discovery_service_monitor_internal (
          target_,
          static_cast<zlink_discovery_monitor_event_mask_t> (options_->events),
          NULL, NULL);
    }

    if (resolved.kind == zlink::service_handle_spot_pub_side
        || resolved.kind == zlink::service_handle_spot_sub_side
        || resolved.kind == zlink::service_handle_spot
        || resolved.kind == zlink::service_handle_spot_node) {
        errno = ENOTSUP;
        return NULL;
    }

    errno = EFAULT;
    return NULL;
}
