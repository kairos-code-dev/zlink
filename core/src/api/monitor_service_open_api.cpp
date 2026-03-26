/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "utils/err.hpp"
#include "api/monitor_api_internal.hpp"
#include "api/service_api_internal.hpp"

#include "api/socket_api_internal.hpp"
#include "services/discovery/discovery_access.hpp"
#include "services/spot/spot_node_access.hpp"

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

void *open_spot_service_monitor (void *monitor_,
                                 zlink_service_monitor_handler_fn handler_,
                                 monitor_snapshot_provider_fn snapshot_provider_,
                                 void *snapshot_subject_,
                                 void *userdata_)
{
    if (!monitor_)
        return NULL;
    socket_handle_t handle = as_socket_handle (monitor_);
    if (!handle.socket) {
        zlink_monitor_close (&monitor_);
        errno = EFAULT;
        return NULL;
    }
    if (set_monitor_handler_state (handle.socket, NULL, handler_, true,
                                   snapshot_provider_, snapshot_subject_, NULL,
                                   userdata_)
        != 0) {
        const int err = errno;
        zlink_monitor_close (&monitor_);
        errno = err;
        return NULL;
    }
    return monitor_;
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

void *open_spot_node_service_monitor_internal (
  void *node_,
  zlink_spot_role_t role_,
  zlink_spot_monitor_event_mask_t events_,
  zlink_service_monitor_handler_fn handler_,
  void *userdata_)
{
    zlink::spot_node_t *node = zlink::spot_node_access_t::from_handle (node_);
    if (!node)
        return NULL;

    void *snapshot_subject = NULL;
    zlink::spot_node_monitor_subject_t subject_kind =
      zlink::spot_node_monitor_subject_none;
    void *monitor = zlink::spot_node_access_t::monitor_open (
      node, role_, events_, &snapshot_subject, &subject_kind);
    if (!monitor)
        return NULL;

    monitor_snapshot_provider_fn snapshot_provider = NULL;
    if (subject_kind == zlink::spot_node_monitor_subject_pub)
        snapshot_provider = &spot_pub_monitor_snapshot_provider;
    else if (subject_kind
             == zlink::spot_node_monitor_subject_internal_receiver)
        snapshot_provider = &spot_internal_receiver_monitor_snapshot_provider;
    else {
        zlink_monitor_close (&monitor);
        errno = EPROTO;
        return NULL;
    }

    return open_spot_service_monitor (monitor, handler_, snapshot_provider,
                                      snapshot_subject, userdata_);
}

void *open_spot_service_monitor_internal (
  void *spot_,
  zlink_spot_role_t role_,
  zlink_spot_monitor_event_mask_t events_,
  zlink_service_monitor_handler_fn handler_,
  void *userdata_)
{
    if (role_ == ZLINK_SPOT_ROLE_PUB && as_spot_pub_side_handle (spot_)) {
        return open_spot_service_monitor (
          spot_pub_monitor_open (spot_, events_), handler_,
          &spot_pub_monitor_snapshot_provider, spot_, userdata_);
    }
    if (role_ == ZLINK_SPOT_ROLE_SUB && as_spot_sub_side_handle (spot_)) {
        return open_spot_service_monitor (
          spot_sub_monitor_open (spot_, events_), handler_,
          &spot_sub_monitor_snapshot_provider, spot_, userdata_);
    }

    void *snapshot_subject = NULL;
    void *monitor =
      spot_handle_monitor_open (spot_, role_, events_, &snapshot_subject);
    if (!monitor)
        return NULL;
    return open_spot_service_monitor (
      monitor, handler_,
      role_ == ZLINK_SPOT_ROLE_PUB ? &spot_pub_monitor_snapshot_provider
                                   : &spot_sub_monitor_snapshot_provider,
      snapshot_subject, userdata_);
}
}

int zlink_service_monitor_handler (void *monitor_,
                                   zlink_service_monitor_handler_fn handler_,
                                   void *userdata_)
{
    return attach_service_monitor_handler_state (monitor_, handler_, userdata_);
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

    if (resolved.kind == zlink::service_handle_spot_pub_side) {
        return open_spot_service_monitor_internal (
          target_, ZLINK_SPOT_ROLE_PUB,
          static_cast<zlink_spot_monitor_event_mask_t> (options_->events),
          NULL, NULL);
    }

    if (resolved.kind == zlink::service_handle_spot_sub_side) {
        return open_spot_service_monitor_internal (
          target_, ZLINK_SPOT_ROLE_SUB,
          static_cast<zlink_spot_monitor_event_mask_t> (options_->events),
          NULL, NULL);
    }

    if (resolved.kind == zlink::service_handle_spot) {
        const int role = infer_spot_monitor_role (target_, options_->events);
        if (role < 0)
            return NULL;
        return open_spot_service_monitor_internal (
          target_, static_cast<zlink_spot_role_t> (role),
          static_cast<zlink_spot_monitor_event_mask_t> (options_->events),
          NULL, NULL);
    }

    if (resolved.kind == zlink::service_handle_spot_node) {
        const int role = infer_spot_monitor_role (target_, options_->events);
        if (role < 0)
            return NULL;
        return open_spot_node_service_monitor_internal (
          target_, static_cast<zlink_spot_role_t> (role),
          static_cast<zlink_spot_monitor_event_mask_t> (options_->events),
          NULL, NULL);
    }

    errno = EFAULT;
    return NULL;
}
