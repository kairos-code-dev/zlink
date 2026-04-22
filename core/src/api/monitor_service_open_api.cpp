/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "utils/err.hpp"
#include "api/handler_result_internal.hpp"
#include "api/monitor_api_internal.hpp"
#include "api/service_handle_internal.hpp"

#include "api/socket_api_internal.hpp"
#include "services/discovery/discovery_access.hpp"
#include "services/spot/spot_monitor_internal.hpp"
#include "services/spot/spot_node_access.hpp"
#include "services/spot/spot_subject_access.hpp"

namespace
{
int infer_spot_service_monitor_role_local (void *target_,
                                           zlink::service_handle_kind_t kind_,
                                           uint32_t events_)
{
    if (kind_ == zlink::service_handle_spot_node) {
        const bool want_sub =
          (events_ & zlink_spot_monitor_event_sub_filter_applied) != 0;
        const bool want_pub =
          (events_ & (zlink_spot_monitor_event_pub_queue_full
                      | zlink_spot_monitor_event_pub_queue_drained))
          != 0;
        if (want_sub && want_pub) {
            errno = EINVAL;
            return -1;
        }
        return want_sub ? ZLINK_SPOT_ROLE_SUB : ZLINK_SPOT_ROLE_PUB;
    }

    return infer_spot_monitor_role (target_, events_);
}

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

void *open_spot_service_monitor_internal (
  void *target_,
  zlink::service_handle_kind_t kind_,
  zlink_service_monitor_event_mask_t events_,
  zlink_service_monitor_handler_fn handler_,
  void *userdata_)
{
    void *snapshot_subject = NULL;
    monitor_snapshot_provider_fn snapshot_provider = NULL;
    void *monitor = NULL;

    if (kind_ == zlink::service_handle_spot_pub_side) {
        monitor = spot_pub_monitor_open (target_, static_cast<int> (events_));
        snapshot_subject = as_spot_pub_side_handle (target_);
        snapshot_provider = &spot_pub_monitor_snapshot_provider;
    } else if (kind_ == zlink::service_handle_spot_sub_side) {
        monitor = spot_sub_monitor_open (target_, static_cast<int> (events_));
        snapshot_subject = as_spot_sub_side_handle (target_);
        snapshot_provider = &spot_sub_monitor_snapshot_provider;
    } else if (kind_ == zlink::service_handle_spot) {
        const int role =
          infer_spot_service_monitor_role_local (target_, kind_, events_);
        if (role < 0)
            return NULL;
        monitor = spot_handle_monitor_open (target_,
                                            static_cast<zlink_spot_role_t> (
                                              role),
                                            static_cast<int> (events_),
                                            &snapshot_subject);
        if (monitor) {
            snapshot_provider =
              role == ZLINK_SPOT_ROLE_SUB ? &spot_sub_monitor_snapshot_provider
                                          : &spot_pub_monitor_snapshot_provider;
        }
    } else if (kind_ == zlink::service_handle_spot_node) {
        zlink::spot_node_t *node = as_spot_node_handle (target_);
        const int role =
          infer_spot_service_monitor_role_local (target_, kind_, events_);
        if (!node || role < 0)
            return NULL;

        zlink::spot_node_monitor_subject_t subject_kind =
          zlink::spot_node_monitor_subject_none;
        monitor = zlink::spot_node_access_t::monitor_open (
          node, static_cast<zlink_spot_role_t> (role),
          static_cast<int> (events_), &snapshot_subject, &subject_kind);
        if (monitor) {
            if (subject_kind == zlink::spot_node_monitor_subject_internal_receiver)
                snapshot_provider =
                  &spot_internal_receiver_monitor_snapshot_provider;
            else if (subject_kind == zlink::spot_node_monitor_subject_pub)
                snapshot_provider = &spot_pub_monitor_snapshot_provider;
        }
    }

    if (!monitor)
        return NULL;

    socket_handle_t handle = as_socket_handle (monitor);
    if (!handle.socket
        || set_monitor_handler_state (handle.socket, NULL, handler_, true,
                                      snapshot_provider, snapshot_subject, NULL,
                                      userdata_)
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
        return open_spot_service_monitor_internal (target_, resolved.kind,
                                                   options_->events, NULL,
                                                   NULL);
    }

    errno = EFAULT;
    return NULL;
}
