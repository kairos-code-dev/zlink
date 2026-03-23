/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "utils/err.hpp"
#include "api/monitor_api_internal.hpp"
#include "api/service_api_internal.hpp"

#include "api/socket_api_internal.hpp"
#include "services/discovery/discovery.hpp"
#include "services/gateway/gateway.hpp"
#include "services/spot/spot_internal_receiver.hpp"
#include "services/spot/spot_node_access.hpp"
#include "services/spot/spot_pub.hpp"
#include "services/spot/spot_sub.hpp"

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

bool service_monitor_events_request_pub_facet (uint32_t events_)
{
    const uint32_t pub_events =
      ZLINK_SPOT_MONITOR_EVENT_PUB_QUEUE_FULL
      | ZLINK_SPOT_MONITOR_EVENT_PUB_QUEUE_DRAINED
      | ZLINK_SPOT_MONITOR_EVENT_PUB_DELIVERY_READY_CHANGED
      | ZLINK_SPOT_MONITOR_EVENT_PUB_FIRST_DELIVERY_READY_CHANGED;
    return (events_ & pub_events) != 0;
}

bool service_monitor_events_request_sub_facet (uint32_t events_)
{
    const uint32_t sub_events =
      ZLINK_SPOT_MONITOR_EVENT_SUB_FILTER_APPLIED
      | ZLINK_SPOT_MONITOR_EVENT_SUBSCRIPTION_READY_CHANGED
      | ZLINK_SPOT_MONITOR_EVENT_SUB_DELIVERY_READY_CHANGED;
    return (events_ & sub_events) != 0;
}

int infer_spot_monitor_role (void *target_, uint32_t events_)
{
    const bool want_pub = service_monitor_events_request_pub_facet (events_);
    const bool want_sub = service_monitor_events_request_sub_facet (events_);
    if (want_pub && want_sub) {
        errno = EINVAL;
        return -1;
    }
    if (want_pub)
        return ZLINK_SPOT_ROLE_PUB;
    if (want_sub)
        return ZLINK_SPOT_ROLE_SUB;

    if (as_spot_pub_side_handle (target_))
        return ZLINK_SPOT_ROLE_PUB;
    if (as_spot_sub_side_handle (target_))
        return ZLINK_SPOT_ROLE_SUB;

    spot_handle_t *spot = as_spot_handle (target_);
    if (!spot) {
        errno = EFAULT;
        return -1;
    }
    if (spot->pub && !spot->sub)
        return ZLINK_SPOT_ROLE_PUB;
    if (spot->sub && !spot->pub)
        return ZLINK_SPOT_ROLE_SUB;

    return ZLINK_SPOT_ROLE_SUB;
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
      static_cast<zlink::discovery_t *> (discovery_);
    if (!discovery->check_tag ()) {
        errno = EFAULT;
        return NULL;
    }
    void *monitor = discovery->monitor_open (events_);
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

void *open_gateway_service_monitor_internal (
  void *gateway_,
  zlink_gateway_monitor_event_mask_t events_,
  zlink_service_monitor_handler_fn handler_,
  void *userdata_)
{
    if (!gateway_)
        return NULL;
    zlink::gateway_t *gateway = static_cast<zlink::gateway_t *> (gateway_);
    if (!gateway->check_tag ()) {
        errno = EFAULT;
        return NULL;
    }
    void *monitor = gateway->monitor_open (events_);
    if (!monitor)
        return NULL;
    socket_handle_t handle = as_socket_handle (monitor);
    if (!handle.socket
        || set_monitor_handler_state (handle.socket, NULL, handler_, true,
                                      &gateway_monitor_snapshot_provider,
                                      static_cast<void *> (gateway), NULL,
                                      userdata_)
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
    if (!node_) {
        errno = EFAULT;
        return NULL;
    }
    zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (node_);
    if (!node->check_tag ()) {
        errno = EFAULT;
        return NULL;
    }
    zlink::service_public_api_scope_t admission (node->public_api_guard ());
    if (!admission.acquired ())
        return NULL;

    if (role_ == ZLINK_SPOT_ROLE_PUB) {
        zlink::spot_pub_t *pub = node->ensure_default_pub ();
        if (!pub)
            return NULL;
        return open_spot_service_monitor (
          pub->monitor_open (events_), handler_,
          &spot_pub_monitor_snapshot_provider,
          static_cast<void *> (pub), userdata_);
    }
    if (role_ == ZLINK_SPOT_ROLE_SUB) {
        zlink::spot_internal_receiver_t *receiver =
          zlink::spot_node_access_t::ensure_internal_receiver (node);
        if (!receiver)
            return NULL;
        return open_spot_service_monitor (
          receiver->monitor_open (events_), handler_,
          &spot_internal_receiver_monitor_snapshot_provider,
          static_cast<void *> (receiver), userdata_);
    }

    errno = EINVAL;
    return NULL;
}

void *spot_pub_monitor_open_internal (
  void *spot_pub_,
  zlink_spot_monitor_event_mask_t events_,
  zlink_service_monitor_handler_fn handler_,
  void *userdata_)
{
    zlink::spot_pub_t *pub = as_spot_pub_side_handle (spot_pub_);
    if (!pub) {
        errno = EFAULT;
        return NULL;
    }
    if (pub->node ()) {
        zlink::service_public_api_scope_t admission (
          pub->node ()->public_api_guard ());
        if (!admission.acquired ())
            return NULL;
    }
    return open_spot_service_monitor (
      pub->monitor_open (events_), handler_, &spot_pub_monitor_snapshot_provider,
      static_cast<void *> (pub), userdata_);
}

void *spot_sub_monitor_open_internal (
  void *spot_sub_,
  zlink_spot_monitor_event_mask_t events_,
  zlink_service_monitor_handler_fn handler_,
  void *userdata_)
{
    zlink::spot_sub_t *sub = as_spot_sub_side_handle (spot_sub_);
    if (!sub) {
        errno = EFAULT;
        return NULL;
    }
    if (sub->node ()) {
        zlink::service_public_api_scope_t admission (
          sub->node ()->public_api_guard ());
        if (!admission.acquired ())
            return NULL;
    }
    return open_spot_service_monitor (
      sub->monitor_open (events_), handler_, &spot_sub_monitor_snapshot_provider,
      static_cast<void *> (sub), userdata_);
}

void *open_spot_service_monitor_internal (
  void *spot_,
  zlink_spot_role_t role_,
  zlink_spot_monitor_event_mask_t events_,
  zlink_service_monitor_handler_fn handler_,
  void *userdata_)
{
    if (role_ == ZLINK_SPOT_ROLE_PUB && as_spot_pub_side_handle (spot_)) {
        return spot_pub_monitor_open_internal (spot_, events_, handler_,
                                               userdata_);
    }
    if (role_ == ZLINK_SPOT_ROLE_SUB && as_spot_sub_side_handle (spot_)) {
        return spot_sub_monitor_open_internal (spot_, events_, handler_,
                                               userdata_);
    }

    spot_handle_t *spot = as_spot_handle (spot_);
    if (!spot)
        return NULL;
    zlink::service_public_api_scope_t admission (spot->public_api);
    if (!admission.acquired ())
        return NULL;

    if (role_ == ZLINK_SPOT_ROLE_PUB) {
        zlink::spot_pub_t *pub = ensure_spot_pub (spot);
        if (!pub) {
            errno = ENOTSUP;
            return NULL;
        }
        return open_spot_service_monitor (
          pub->monitor_open (events_), handler_,
          &spot_pub_monitor_snapshot_provider,
          static_cast<void *> (pub), userdata_);
    }
    if (role_ == ZLINK_SPOT_ROLE_SUB) {
        zlink::spot_sub_t *sub = ensure_spot_sub (spot);
        if (!sub) {
            errno = ENOTSUP;
            return NULL;
        }
        return open_spot_service_monitor (
          sub->monitor_open (events_), handler_,
          &spot_sub_monitor_snapshot_provider,
          static_cast<void *> (sub), userdata_);
    }

    errno = EINVAL;
    return NULL;
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

    zlink::discovery_t *discovery = static_cast<zlink::discovery_t *> (target_);
    if (discovery->check_tag ()) {
        return open_discovery_service_monitor_internal (
          target_,
          static_cast<zlink_discovery_monitor_event_mask_t> (options_->events),
          NULL, NULL);
    }

    if (is_registered_gateway_handle (target_)) {
        return open_gateway_service_monitor_internal (
          target_,
          static_cast<zlink_gateway_monitor_event_mask_t> (options_->events),
          NULL, NULL);
    }

    if (as_spot_pub_side_handle (target_)) {
        return open_spot_service_monitor_internal (
          target_, ZLINK_SPOT_ROLE_PUB,
          static_cast<zlink_spot_monitor_event_mask_t> (options_->events),
          NULL, NULL);
    }

    if (as_spot_sub_side_handle (target_)) {
        return open_spot_service_monitor_internal (
          target_, ZLINK_SPOT_ROLE_SUB,
          static_cast<zlink_spot_monitor_event_mask_t> (options_->events),
          NULL, NULL);
    }

    if (is_registered_spot_handle (target_)) {
        const int role = infer_spot_monitor_role (target_, options_->events);
        if (role < 0)
            return NULL;
        return open_spot_service_monitor_internal (
          target_, static_cast<zlink_spot_role_t> (role),
          static_cast<zlink_spot_monitor_event_mask_t> (options_->events),
          NULL, NULL);
    }

    if (is_registered_spot_node_handle (target_)) {
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
